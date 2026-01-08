#include "lexer/Lexer.h"
#include "lexer/Tokenizer.h"
#include "parser/parser_test.h"
#include <iostream>
#include "helper/Utils.h"
#include "parser/Parser.h"
#include "ast/Ast.h"

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--tokenize") {

        std::string file = argv[2];
        std::string fullPath = "test/lexer/" + file;

        run_lexer(file, fullPath, false);
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "--tokenize_verbose") {

        std::string file = argv[2];
        std::string fullPath = "test/lexer/" + file;

        run_lexer(file, fullPath, true);
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "--parser_test") {
        parser_test();
    }


    if(argc >= 3 && std::string(argv[1]) == "--parse") {
        std::string file = argv[2];
        std::string fullPath = "test/lexer/" + file;

        Parser::run(file, fullPath, false);
        return 0;
    }

    if(argc >= 3 && std::string(argv[1]) == "--parse_verbose") {
        std::string file = argv[2];
        std::string fullPath = "test/lexer/" + file;

        Parser::run(file, fullPath, true);
        return 0;
    }

    if (argc >= 3 && std::string(argv[1]) == "--print-ast") {
        std::string file = argv[2];
        std::string fullPath = "test/lexer/" + file;

        std::string sourceCode = Utils::readSourceCode(fullPath);
        sourceCode += '\0';
        auto sequence = Tokenizer::tokenizeSeq(sourceCode, false);

        if (sequence.second.has_value()) {
            int a = sequence.second->first;
            int b = sequence.second->second;
            std::cerr << "Lexer Error at line:" << a+1 << ":" << b+1 << std::endl;
            return 1;
        }

        Parser parser(sequence.first, false);
        if (parser.parse() != 0) return 1;

        auto astTree = ast::buildFromParseTree(parser.getParseTreeRoot());
        ast::printAst(astTree, std::cout);
        return 0;
    }

    // --- normaler Compiler-Code ---
    std::cout << "Normal compiler mode\n";

    return 0;
}
