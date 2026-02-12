#include <iostream>
#include <string>
#include <exception>

#include "Tokenizer.h"
#include "./../helper/structs/Token.h"
#include "./../helper/Utils.h"
#include "./../helper/Diagnostics.h"

static std::string formatTokenType(const Token& token) {
    const std::string& type = token.getTokenType();
    if (type == "decimal-constant" || type == "character-constant") {
        return "constant";
    }
    return type;
}

void printTokens(const std::vector<Token>& tokens, const std::string& fileName) {
    for (const Token& token : tokens) {
        if (token.getTokenType() == "EOF") {
            continue;
        }
        std::cout << fileName << ":"
                  << token.getSourceLine() + 1 << ":"
                  << token.getSourceIndex() + 1 << ": "
                  << formatTokenType(token) << " "
                  << token.getValue() << std::endl;
    }
}

bool run_lexer(const std::string& fileName, const std::string& path, bool isVerbose) {
    std::string sourceCode;
    try {
        sourceCode = Utils::readSourceCode(path);
    } catch (const std::exception& ex) {
        diag::printException(std::cerr, ex);
        return false;
    }
    auto sequence = Tokenizer::tokenizeSeq(sourceCode, isVerbose);
    // error appeared
    if (sequence.second.has_value()) {
        const auto& err = *sequence.second;
        printTokens(sequence.first, fileName);
        diag::printLocatedError(std::cerr, fileName, err.line, err.column, err.message);
        return false;
    }
    // print tokens
    printTokens(sequence.first, fileName);
    return true;
}
