#include "lexer/Lexer.h"
#include "lexer/Tokenizer.h"
#include <iostream>
#include <exception>
#include "helper/Utils.h"
#include "parser/Parser.h"
#include "ast/Ast.h"
#include "semantic/Semantic.h"

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--tokenize") {

        std::string fullPath = argv[2];
        std::string file = fullPath;

        return run_lexer(file, fullPath, false) ? 0 : 1;
    }
    if (argc >= 3 && std::string(argv[1]) == "--tokenize_verbose") {

        std::string fullPath = argv[2];
        std::string file = fullPath;

        return run_lexer(file, fullPath, true) ? 0 : 1;
    }
    if(argc >= 3 && std::string(argv[1]) == "--parse") {
        std::string fullPath = argv[2];
        std::string file = fullPath;

        bool ok = Parser::run(file, fullPath, false, false);
        return ok ? 0 : 1;
    }

    if(argc >= 3 && std::string(argv[1]) == "--parse_verbose") {
        std::string fullPath = argv[2];
        std::string file = fullPath;

        bool ok = Parser::run(file, fullPath, true, false);
        return ok ? 0 : 1;
    }

    if (argc >= 3 && std::string(argv[1]) == "--print-ast") {
        std::string fullPath = argv[2];
        std::string file = fullPath;

        std::string sourceCode;
        try {
            sourceCode = Utils::readSourceCode(fullPath);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << std::endl;
            return 1;
        }
        sourceCode += '\0';
        auto sequence = Tokenizer::tokenizeSeq(sourceCode, false);

        if (sequence.second.has_value()) {
            const auto& err = *sequence.second;
            std::cerr << file << ":" << err.line + 1 << ":" << err.column + 1
                      << ": error: " << err.message << std::endl;
            return 1;
        }

        Parser parser(sequence.first, false);
        if (parser.parse() != 0) return 1;

        auto astTree = ast::buildFromParseTree(parser.getParseTreeRoot());
        if (!semantic::analyze(astTree, std::cerr, file)) return 1;
        ast::printAst(astTree, std::cout);
        return 0;
    }

    // --- normaler Compiler-Code ---
    std::cout << "Normal compiler mode\n";

    return 0;
}
