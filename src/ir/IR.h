#pragma once

#include <ostream>
#include <string>

#include "../ast/Ast.h"

namespace ir {

bool generate(const ast::TranslationUnit& tu, const std::string& inputPath, std::ostream& err);

} // namespace ir
