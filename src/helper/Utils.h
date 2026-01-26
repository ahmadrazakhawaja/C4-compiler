#pragma once
#include <string>
#include <filesystem>

class Utils {
public:
    static std::string readSourceCode(const std::filesystem::path& path);
};