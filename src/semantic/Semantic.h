#pragma once

#include <iosfwd>
#include <string>

#include "../ast/Ast.h"

namespace semantic {

bool analyze(const ast::TranslationUnit& tu, std::ostream& err, const std::string& fileName = "unknown");

}
