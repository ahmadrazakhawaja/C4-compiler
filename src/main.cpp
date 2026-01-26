#include "lexer/Lexer.h"
#include "lexer/Tokenizer.h"
#include "parser/parser_test.h"
#include <filesystem>
#include <exception>
#include <iostream>
#include "helper/Utils.h"
#include "parser/Parser.h"

static std::string display_name_from_path(const std::string& path) {
    std::filesystem::path filePath(path);
    if (filePath.has_filename()) {
        return filePath.filename().string();
    }
    return path;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--tokenize") {

        std::string fullPath = argv[2];
        std::string file = display_name_from_path(fullPath);

        return run_lexer(file, fullPath, true) ? 0 : 1;
    }

    if (argc >= 3 && std::string(argv[1]) == "--tokenize_verbose") {

        std::string fullPath = argv[2];
        std::string file = display_name_from_path(fullPath);
        return run_lexer(file, fullPath, true) ? 0 : 1;
    }
    
    if (argc >= 2 && std::string(argv[1]) == "--parser_test") {
        parser_test();
    }

    if(argc >= 3 && std::string(argv[1]) == "--parse") {
        auto filePath = std::filesystem::current_path() / "test/lexer/pretty.c";
        std::string fullPath = argv[2];
        std::string file = display_name_from_path(filePath);

        bool ok = Parser::run(file, filePath, false);
        return ok ? 0 : 1;
    }

    if(argc >= 3 && std::string(argv[1]) == "--parse_verbose") {
        std::string fullPath = argv[2];
        std::string file = display_name_from_path(fullPath);

        bool ok = Parser::run(file, fullPath, false);
        return ok ? 0 : 1;
    }

    if(argc >= 3 && std::string(argv[1]) == "--pretty") {
        std::string fullPath = argv[2];
        std::string file = display_name_from_path(fullPath);

        bool ok = Parser::run(file, fullPath, false);
        return ok ? 0 : 1;
    }

    // --- normaler Compiler-Code ---
    std::cout << "Normal compiler mode\n";

    return 0;
}