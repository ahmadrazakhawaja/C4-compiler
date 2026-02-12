#pragma once

#include "../ast/Ast.h"
#include "../helper/Symbol.h"
#include "../helper/structs/Node.h"

namespace ir::detail {

ast::TypeSpec buildTypeSpecFromNode(const Node::Ptr& node);
ast::AbstractDeclarator buildAbstractDeclaratorFromNode(const Node::Ptr& node);
Node::Ptr findFirstNodeOfType(const Node::Ptr& node, Symbol sym);

} // namespace ir::detail

