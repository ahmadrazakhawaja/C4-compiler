#pragma once

#include <exception>
#include <ostream>
#include <string>

namespace diag {

inline void printException(std::ostream& err, const std::exception& ex) {
    err << ex.what() << std::endl;
}

inline void printLocatedError(std::ostream& err,
                              const std::string& fileName,
                              int line,
                              int column,
                              const std::string& message) {
    err << fileName << ":" << line + 1 << ":" << column + 1
        << ": error: " << message << std::endl;
}

inline void printUnlocatedError(std::ostream& err,
                                const std::string& fileName,
                                const std::string& message) {
    err << fileName << ": error: " << message << std::endl;
}

} // namespace diag

