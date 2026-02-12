#include "lexer/Lexer.h"
#include "lexer/Tokenizer.h"
#include <iostream>
#include <exception>
#include <utility>
#include "helper/Utils.h"
#include "parser/Parser.h"
#include "ast/Ast.h"
#include "semantic/Semantic.h"
#include "ir/IR.h"

static bool build_ast(const std::string& file, const std::string& fullPath, ast::TranslationUnit& outAst) {
    std::string sourceCode;
    try {
        sourceCode = Utils::readSourceCode(fullPath);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return false;
    }
    auto sequence = Tokenizer::tokenizeSeq(sourceCode, false);
    if (sequence.second.has_value()) {
        const auto& err = *sequence.second;
        std::cerr << file << ":" << err.line + 1 << ":" << err.column + 1
                  << ": error: " << err.message << std::endl;
        return false;
    }

    Parser parser(std::move(sequence.first), false, file);
    if (parser.parse() != 0) return false;

    outAst = ast::buildFromParseTree(parser.getParseTreeRoot());
    return semantic::analyze(outAst, std::cerr, file);
}

static bool compile_to_ir(const std::string& file, const std::string& fullPath) {
    ast::TranslationUnit astTree;
    if (!build_ast(file, fullPath, astTree)) return false;

    return ir::generate(astTree, fullPath, std::cerr);
}

int main(int argc, char** argv) {
    auto printUsageAndFail = []() {
        std::cerr << "usage: c4 [--tokenize|--tokenize_verbose|--parse|--parse_verbose|--print-ast|--compile] file\n";
        return 1;
    };

    if (argc < 2) return printUsageAndFail();

    const std::string arg1 = argv[1];
    std::string mode;
    std::string fullPath;
    if (argc >= 3 && arg1.rfind("--", 0) == 0) {
        mode = arg1;
        fullPath = argv[2];
    } else if (arg1.rfind("--", 0) != 0) {
        mode = "--compile";
        fullPath = arg1;
    } else {
        return printUsageAndFail();
    }

    const std::string file = fullPath;

    if (mode == "--tokenize") return run_lexer(file, fullPath, false) ? 0 : 1;
    if (mode == "--tokenize_verbose") return run_lexer(file, fullPath, true) ? 0 : 1;
    if (mode == "--parse") return Parser::run(file, fullPath, false) ? 0 : 1;
    if (mode == "--parse_verbose") return Parser::run(file, fullPath, true) ? 0 : 1;
    if (mode == "--compile") return compile_to_ir(file, fullPath) ? 0 : 1;

    if (mode == "--print-ast") {
        ast::TranslationUnit astTree;
        if (!build_ast(file, fullPath, astTree)) return 1;
        ast::printAst(astTree, std::cout);
        return 0;
    }

    return printUsageAndFail();
}
