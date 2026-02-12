#include "lexer/Lexer.h"
#include "lexer/Tokenizer.h"
#include <iostream>
#include <exception>
#include "helper/Utils.h"
#include "parser/Parser.h"
#include "ast/Ast.h"
#include "semantic/Semantic.h"
#include "ir/IR.h"

static bool compile_to_ir(const std::string& file, const std::string& fullPath, bool optimize = false) {
    std::string sourceCode;
    try {
        sourceCode = Utils::readSourceCode(fullPath);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return false;
    }
    sourceCode += '\0';

    auto sequence = Tokenizer::tokenizeSeq(sourceCode, false);
    if (sequence.second.has_value()) {
        const auto& err = *sequence.second;
        std::cerr << file << ":" << err.line + 1 << ":" << err.column + 1
                  << ": error: " << err.message << std::endl;
        return false;
    }

    Parser parser(sequence.first, false, file);
    if (parser.parse() != 0) return false;

    auto astTree = ast::buildFromParseTree(parser.getParseTreeRoot());
    if (!semantic::analyze(astTree, std::cerr, file)) return false;

    return ir::generate(astTree, fullPath, std::cerr, optimize);
}

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

        bool ok = Parser::run(file, fullPath, false);
        return ok ? 0 : 1;
    }

    if(argc >= 3 && std::string(argv[1]) == "--parse_verbose") {
        std::string fullPath = argv[2];
        std::string file = fullPath;

        bool ok = Parser::run(file, fullPath, true);
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

    if (argc >= 3 && std::string(argv[1]) == "--compile") {
        std::string fullPath = argv[2];
        std::string file = fullPath;
        return compile_to_ir(file, fullPath) ? 0 : 1;
    }

    if (argc >= 3 && std::string(argv[1]) == "--optimize") {
        std::string fullPath = argv[2];
        std::string file = fullPath;
        return compile_to_ir(file, fullPath, true) ? 0 : 1;
    }

    if (argc >= 2 && std::string(argv[1]).rfind("--", 0) != 0) {
        std::string fullPath = argv[1];
        std::string file = fullPath;
        return compile_to_ir(file, fullPath) ? 0 : 1;
    }

    std::cerr << "usage: c4 [--tokenize|--tokenize_verbose|--parse|--parse_verbose|--print-ast|--compile|--optimize] file\n";
    return 1;
}
