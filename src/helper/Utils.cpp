#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "Utils.h"
#include <filesystem>

std::string Utils::readSourceCode(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error(
            "File couldn't be opened: " + path.string()
        );
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}