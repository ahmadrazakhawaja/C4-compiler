#ifndef COMPILER_LAB_PARSER_H
#define COMPILER_LAB_PARSER_H
#include "../helper/structs/Token.h"
#include "../helper/structs/Node.h"
#include <vector>

class Parser {
public:
    Parser(std::vector<Token> tokens, bool isVerbose); //remaining symbols
    static void run(const std::string& fileName, const std::string& path, bool isVerbose);

    Node peekSymbol(int k);
    Token peek(int k);
    Token peekExpr(int k);
    void dump_state();
    int parse();
    std::optional<Node> parseSymbol();

    std::vector<Token> getRemTokens() const { return remTokens;}
    std::vector<Node> getRemSymbols() const {return remSymbols;}
    Node getParseTreeRoot() const { return parseTreeRoot;}

    std::optional<Node> evilShuntingYard(std::string limit, std::string limit2, bool isOutermost);
    std::optional<Node> evilShuntingYard(std::string limit, bool isOutermost);

private:
    std::vector<Token> remTokens;
    std::vector<Node> remSymbols;
    int remTokensExpressionIndex;
    std::vector<Node> remRevExprSymbols;
    Node parseTreeRoot;
    bool isVerbose;
};
#endif //COMPILER_LAB_PARSER_H
