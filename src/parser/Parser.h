#ifndef COMPILER_LAB_PARSER_H
#define COMPILER_LAB_PARSER_H

#include "../helper/structs/Token.h"
#include "../helper/structs/Node.h"
#include <optional>
#include <vector>
#include <string>

class Parser {
public:
    Parser(std::vector<Token> tokens, bool isVerbose, std::string fileName = {});
    static bool run(const std::string& fileName, const std::string& path, bool isVerbose);

    Node::Ptr peekSymbol(int k);
    Token peek(int k);
    Token peekExpr(int k);

    void dump_state();
    int parse();
    std::optional<Node::Ptr> parseSymbol();

    const std::vector<Token>& getRemTokens() const { return remTokens; }
    const std::vector<Node::Ptr>& getRemSymbols() const { return remSymbols; }
    Node::Ptr getParseTreeRoot() const { return parseTreeRoot; }

    std::optional<Node::Ptr> evilShuntingYard(std::string limit, std::string limit2, bool isOutermost);
    std::optional<Node::Ptr> evilShuntingYard(std::string limit, bool isOutermost);

private:
    std::string parseFileName;
    std::vector<Token> remTokens;
    std::vector<Node::Ptr> remSymbols;
    std::optional<Token> lastTokenSeen;

    int remTokensExpressionIndex = 0;
    std::vector<Node::Ptr> remRevExprSymbols;

    Node::Ptr parseTreeRoot;
    bool isVerbose = false;
};

#endif
