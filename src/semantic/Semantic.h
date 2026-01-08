#pragma once

#include <iosfwd>

#include "../ast/Ast.h"

namespace semantic {

bool analyze(const ast::TranslationUnit& tu, std::ostream& err);

}
