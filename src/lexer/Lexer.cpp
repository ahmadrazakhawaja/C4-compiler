#include <iostream>
#include <fstream>
#include <string>
#include <exception>

#include "Tokenizer.h"
#include "./../helper/structs/Token.h"
#include "./../helper/Utils.h"

#define TEST_PATH "./test/lexer/"

void printTokens(std::vector<Token> tokens, std::string fileName) {
    for (const Token& token : tokens) {
        if(token.getTokenType() == "EOF") {
            continue;
        }
        std::cout << fileName << ":"
            << token.getSourceLine()+1 << ":"
            << token.getSourceIndex()+1 << ": "
            << token.getTokenType() << " "
            << token.getValue() << std::endl;
        }
}

bool run_lexer(const std::string& fileName, const std::string& path, bool isVerbose) {
    std::string sourceCode;
    try {
        sourceCode = Utils::readSourceCode(path);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return false;
    }
    sourceCode += '\0';
    auto sequence = Tokenizer::tokenizeSeq(sourceCode, isVerbose);
    // error appeared
    if (sequence.second.has_value()) {
        const auto& err = *sequence.second;
        printTokens(sequence.first, fileName);
        std::cerr << fileName << ":" << err.line + 1 << ":" << err.column + 1
                  << ": Lexer Error: " << err.message << std::endl;
        return false;
    }
    // print tokens
    printTokens(sequence.first, fileName);
    return true;
}
