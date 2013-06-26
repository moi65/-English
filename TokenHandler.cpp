#include "TokenHandler.h"
#include <stdexcept>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <iostream>

Parser::~Parser()
{
    delete program;
}


void Parser::setupHandlers()
{
    handlers[TokenType::Declaration] = &Parser::handle_declaration;
    handlers[TokenType::SetVar] = &Parser::handle_setvar;
    handlers[TokenType::If] = &Parser::handle_if;
    handlers[TokenType::While] = &Parser::handle_while;
    handlers[TokenType::Identifier] = &Parser::handleIdentifier;
}

Parser::Parser(const TokenStream& tokens)
    : ts(tokens), current(), lexer(""), data_handler(DataHandler()), handlers(),
      program(new Ast::Block)
{
    setupHandlers();
}

Parser::Parser(const char* filename)
    : ts(), current(), lexer(filename), data_handler(DataHandler()), handlers(),
      program(new Ast::Block)
{
    setupHandlers();
}

Ast::Block* Parser::run()
{
    if(ts.empty())
        ts = lexer.tokenize();
    current = ts.begin();
    while(handleToken());
    return program;
}

void Parser::skipOptional(TokenType type)
{
    ++current;
    if(current->type != type)
        --current;
}

void Parser::insert(const Token& t, int offset)
{
    current = ts.insert(current + offset, t);
    current -= offset;
}

void Parser::readBlock(std::vector<Token>& tokens, TokenType begin)
{
    ++current;
    if(current->type != begin)
        error("expecting a 'then:' or perhaps 'do:' as a block beginning", current->line);

    int to_find = 1;
    while(to_find > 0) {
        ++current;
        if(current->type == TokenType::BlockEnd)
            --to_find;
        else if(current->type == TokenType::BlockBegin)// || current->type == TokenType::BlockBeginW)
            ++to_find;
        tokens.push_back(*current);
    }
    // We read a TokenType::BlockEnd that we do not need(other ones are needed for nesting though)
    tokens.pop_back();
    // Skip the following TokenType::Dot
    ++current;
}


void Parser::handleUnexpectedToken()
{
    error(
        "unexpected token (id " + boost::lexical_cast<std::string>(
        static_cast<int>(current->type)) + ")", current->line
    );
}

bool Parser::handleToken() {
    switch(current->type) {
        case TokenType::Begin:
        case TokenType::End:
        case TokenType::Error:
            return false;
        case TokenType::FuncName:
            handleFunctionCall();
            break;
        default: {
            auto handler = handlers.find(current->type);
            if(handler == handlers.end())
                handleUnexpectedToken();
            else
                (handler->second)(this);
        }
    }
    ++current;
    if(current->type != TokenType::Dot)
        error("sentences are usually ended with a dot", current->line);
    ++current;
    return true;
}

void Parser::handleIdentifier()
{
    program->attach(handleFunctionCall(false));
}


void Parser::handle_declaration() {
    // Assuming an article here
    ++current;
    if(current->type != TokenType::Article)
        error("article required before this variable noun", current->line);
    // Expecting a variable now
    ++current;
    if(current->type != TokenType::Identifier)
        return error("expecting a name on declaration", current->line);
    program->attach(new Ast::VarDeclaration(
        current->getValue<std::string>(), &data_handler
    ));
}

void Parser::handle_setvar() {
    // Assuming an (optional) article here
    skipOptional(TokenType::Article);
    // Expecting an (optional) value now
    skipOptional(TokenType::ValueOf);
    // Expecting a name OR an (optional) article
    skipOptional(TokenType::Article);
    ++current;
    if(current->type != TokenType::Identifier)
        error("expecting a name that contains the value", current->line);
    const std::string name = current->getValue<std::string>();
    // Expecting a to now
    ++current;
    if(current->type != TokenType::To)
        error("expecting to after the name", current->line);
    // Now we need to read an expression and set the name with it
    program->attach(new Ast::Assignment(name, &data_handler, expression()));
}

void Parser::handle_if() {
    // Read the condition first
    Ast::Condition* if_cond = condition();
    // Read a block
    std::vector<Token> tokens;
    readBlock(tokens);
    Ast::Block* if_body = Parser(tokens).run();
    Ast::Block* else_body;
    // Read a possible else
    if((current + 1)->type == TokenType::Else) {
        ++current;
        std::vector<Token> tokens2;
        readBlock(tokens2);
        else_body = Parser(tokens2).run();
    }
    insert(Token(TokenType::Dot));
    program->attach(new Ast::IfStatement(if_cond, if_body, else_body));
}

void Parser::handle_while() {
    // Read the condition first
    Ast::Condition* cond = condition();
    // Read the body tokens
    std::vector<Token> tokens;
    readBlock(tokens, TokenType::BlockBegin);
    Ast::Block* body = Parser(tokens).run();
    insert(Token(TokenType::Dot));
    program->attach(new Ast::WhileStatement(cond, body));
}

Ast::FunctionCall* Parser::handleFunctionCall(bool in_expr)
{
    // Get the function name
    const std::string name = current->getValue<std::string>();
    Ast::FunctionCall* call = new Ast::FunctionCall(name, &data_handler);
    if(in_expr) {
        // If we don't find a TokenType::On now, we return the result
        if((current + 1)->type != TokenType::On)
            return call;
        ++current;
    }
    // We found a TokenType::On, read all arguments(separated by "and")
    while(true) {
        call->addArgument(expression());
        ++current;
        if(current->type != TokenType::Operator || current->getValue<char>() != '&') {
            --current;
            break;
        }
    }
    return call;
}
// handlers(handle_*)
// // // // // // // // //


// // // // // // //
// expression handling
// // // // // // //

Ast::UnaryOp* Parser::primary()
{
    ++current;
    switch(current->type) {
        case TokenType::String:
            return new Ast::UnaryOp(new Ast::Literal(Variable(current->getValue<Variable::StringType>())));
        case TokenType::Number:
            return new Ast::UnaryOp(new Ast::Literal(Variable(current->getValue<Variable::NumberType>())));
        case TokenType::Article:
            // We actually expect another primary now
            // because we allow an optional article before a primary
            return primary();
        case TokenType::Identifier:
            return new Ast::UnaryOp(new Ast::VarNode(current->getValue<std::string>(), &data_handler));
        //case TokenType::FuncName:
        //    return handleFunctionCall();
        case TokenType::Operator: {
            char op = current->getValue<char>();
            if(op == '(') {
                Ast::UnaryOp* uop = new Ast::UnaryOp(expression());
                ++current;
                if(current->getValue<char>() != ')')
                    error("expected ')' after '('", current->line);
                return uop;
            } else if(op == '-')
                return new Ast::UnaryOp(primary(), op);
            else
                error("unexpected operator in primary", current->line);
        }
        default:
            error("primary expected", current->line);
    }
    return nullptr;
}

Ast::Expression* Parser::term() {
    Ast::UnaryOp* left = primary();
    ++current;
    if(current->type != TokenType::Operator) {
        --current;
        return new Ast::Expression(left);
    }
    const char op = current->getValue<char>();
    if(op != '*' && op != '/') {
        --current;
        return new Ast::Expression(left);
    }
    return new Ast::Expression(left, term(), op);
}

Ast::Expression* Parser::expression() {
    Ast::Expression* left = term();
    ++current;
    if(current->type != TokenType::Operator) {
        --current;
        return left;
    }
    const char op = current->getValue<char>();
    if(op != '+' && op != '-') {
        --current;
        return left;
    }
    return new Ast::Expression(left, expression(), op);
}

Ast::Condition* Parser::condition_term() {
    Ast::Expression* left = expression();
    ++current;
    if(current->type != TokenType::Operator)
        error("expecting operator in the condition", current->line);

    const char op = current->getValue<char>();
    if(op != '=' && op != '!' && op != '<' && op != '>')
        error("unsupported operator in the condition", current->line);

    return new Ast::Condition(left, expression(), op);
}

Ast::Condition* Parser::condition()
{
    Ast::Condition* left = condition_term();
    ++current;
    if(current->type != TokenType::Operator) {
        --current;
        return left;
    }
    const char op = current->getValue<char>();
    if(op != '&' && op != '|') {
        --current;
        return left;
    }
    return new Ast::Condition(left, condition(), op);
}
