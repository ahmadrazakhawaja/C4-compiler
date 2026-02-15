#include "IRTypeNode.h"

#include <memory>
#include <string>
#include <vector>

namespace ir::detail {
namespace {

bool isTerminalValue(const Node::Ptr& node, const std::string& value) {
    if (!node || node->getType() != terminal) return false;
    if (!node->getToken().has_value()) return false;
    return node->getToken()->getValue() == value;
}

std::string tokenValue(const Node::Ptr& node) {
    if (!node || !node->getToken().has_value()) return "";
    return node->getToken()->getValue();
}

void collectStructDecls(const Node::Ptr& node, std::vector<ast::Decl>& out);
ast::Declarator buildDeclaratorFromNode(const Node::Ptr& node);
ast::ParamList buildParamListFromNode(const Node::Ptr& node);
ast::ParamDecl buildParamDeclFromNode(const Node::Ptr& node);
ast::Decl buildDeclFromDecNode(const Node::Ptr& node);

void collectStructDecls(const Node::Ptr& node, std::vector<ast::Decl>& out) {
    if (!node || node->getChildren().empty()) return;
    out.push_back(buildDeclFromDecNode(node->getChildren().at(0)));
    collectStructDecls(node->getChildren().at(1), out);
}

void collectParamListTail(const Node::Ptr& node, std::vector<ast::ParamDecl>& out) {
    if (!node || node->getChildren().empty()) return;
    out.push_back(buildParamDeclFromNode(node->getChildren().at(1)));
    collectParamListTail(node->getChildren().at(2), out);
}

ast::ParamList buildArraySuffixFromNode(const Node::Ptr& node) {
    ast::ParamList list;
    list.isArray = true;
    const auto& kids = node->getChildren();
    if (kids.size() > 3 && kids.at(1)->getType() == expr && !kids.at(1)->getChildren().empty()) {
        list.arraySize = ast::Expr{kids.at(1)->getChildren().at(0)};
    }
    return list;
}

void collectDirectDecSuffixes(const Node::Ptr& node, std::vector<ast::ParamList>& out) {
    if (!node || node->getChildren().empty()) return;
    const auto& kids = node->getChildren();
    if (isTerminalValue(kids.at(0), "[")) {
        out.push_back(buildArraySuffixFromNode(node));
        collectDirectDecSuffixes(kids.back(), out);
    } else if (kids.at(1)->getType() == paramlist) {
        out.push_back(buildParamListFromNode(kids.at(1)));
        collectDirectDecSuffixes(kids.back(), out);
    } else {
        out.push_back(ast::ParamList{});
        collectDirectDecSuffixes(kids.back(), out);
    }
}

void collectDirectAbstractSuffixes(const Node::Ptr& node, std::vector<ast::ParamList>& out) {
    if (!node || node->getChildren().empty()) return;
    const auto& kids = node->getChildren();
    if (isTerminalValue(kids.at(0), "[")) {
        out.push_back(buildArraySuffixFromNode(node));
        collectDirectAbstractSuffixes(kids.back(), out);
    } else if (kids.at(1)->getType() == paramlist) {
        out.push_back(buildParamListFromNode(kids.at(1)));
        collectDirectAbstractSuffixes(kids.back(), out);
    } else {
        out.push_back(ast::ParamList{});
        collectDirectAbstractSuffixes(kids.back(), out);
    }
}

int countPointerDepth(const Node::Ptr& node) {
    if (!node || node->getType() != pointer) return 0;
    int count = 1;
    Node::Ptr current = node->getChildren().at(1);
    while (current && !current->getChildren().empty()) {
        count++;
        current = current->getChildren().at(1);
    }
    return count;
}

ast::DirectDeclarator buildDirectDeclaratorFromNode(const Node::Ptr& node) {
    ast::DirectDeclarator direct;
    const auto& kids = node->getChildren();
    if (!kids.empty() && isTerminalValue(kids.at(0), "(")) {
        direct.kind = ast::DirectDeclarator::Kind::Nested;
        direct.nested = std::make_shared<ast::Declarator>(buildDeclaratorFromNode(kids.at(1)));
    } else {
        direct.kind = ast::DirectDeclarator::Kind::Identifier;
        direct.identifier = tokenValue(kids.at(0));
    }

    const auto& suffix = kids.at(kids.size() - 1);
    collectDirectDecSuffixes(suffix, direct.params);
    return direct;
}

ast::Declarator buildDeclaratorFromNode(const Node::Ptr& node) {
    ast::Declarator decl;
    const auto& kids = node->getChildren();
    size_t idx = 0;
    if (!kids.empty() && kids.at(0)->getType() == pointer) {
        decl.pointerDepth = countPointerDepth(kids.at(0));
        idx = 1;
    }
    decl.direct = buildDirectDeclaratorFromNode(kids.at(idx));
    return decl;
}

ast::DirectAbstractDeclarator buildDirectAbstractDeclaratorFromNode(const Node::Ptr& node) {
    ast::DirectAbstractDeclarator direct;
    const auto& kids = node->getChildren();
    if (kids.size() < 3) return direct;

    if (kids.at(1)->getType() == paramlist || isTerminalValue(kids.at(1), ")")) {
        direct.kind = ast::DirectAbstractDeclarator::Kind::ParamList;
        if (kids.at(1)->getType() == paramlist) {
            direct.firstParamList = buildParamListFromNode(kids.at(1));
        }
    } else {
        direct.kind = ast::DirectAbstractDeclarator::Kind::Nested;
        direct.nested = std::make_shared<ast::AbstractDeclarator>(buildAbstractDeclaratorFromNode(kids.at(1)));
    }

    const size_t suffixIndex = kids.size() > 3 ? 3 : 2;
    if (suffixIndex < kids.size()) {
        collectDirectAbstractSuffixes(kids.at(suffixIndex), direct.suffixes);
    }
    return direct;
}

ast::ParamDecl buildParamDeclFromNode(const Node::Ptr& node) {
    ast::ParamDecl param;
    const auto& kids = node->getChildren();
    param.type = buildTypeSpecFromNode(kids.at(0));
    if (kids.size() > 1 && !kids.at(1)->getChildren().empty()) {
        const auto& inner = kids.at(1)->getChildren().at(0);
        if (inner->getType() == declarator) {
            param.declarator = std::make_shared<ast::Declarator>(buildDeclaratorFromNode(inner));
        } else if (inner->getType() == abstractdeclarator) {
            param.abstractDeclarator = std::make_shared<ast::AbstractDeclarator>(buildAbstractDeclaratorFromNode(inner));
        }
    }
    return param;
}

ast::ParamList buildParamListFromNode(const Node::Ptr& node) {
    ast::ParamList list;
    const auto& kids = node->getChildren();
    list.params.push_back(buildParamDeclFromNode(kids.at(0)));
    if (kids.size() > 1) collectParamListTail(kids.at(1), list.params);
    return list;
}

ast::Decl buildDeclFromDecNode(const Node::Ptr& node) {
    ast::Decl decl;
    const auto& kids = node->getChildren();
    size_t typeIndex = 0;
    if (!kids.empty() && isTerminalValue(kids.at(0), "extern")) {
        decl.isExtern = true;
        typeIndex = 1;
    }
    decl.type = buildTypeSpecFromNode(kids.at(typeIndex));
    const auto& decTail = kids.at(typeIndex + 1);
    if (decTail && decTail->getChildren().size() > 1) {
        decl.declarator = buildDeclaratorFromNode(decTail->getChildren().at(0));
    }
    return decl;
}

} // namespace

ast::TypeSpec buildTypeSpecFromNode(const Node::Ptr& node) {
    ast::TypeSpec typeSpec;
    if (!node || node->getChildren().empty()) return typeSpec;
    const auto& kids = node->getChildren();
    if (kids.at(0)->getType() == structtype) {
        typeSpec.kind = ast::TypeSpec::Kind::Struct;
        const auto& skids = kids.at(0)->getChildren();
        ast::StructType st;
        if (skids.size() >= 2 && skids.at(1)->getType() == id) {
            st.name = tokenValue(skids.at(1));
        }
        int brace = -1;
        for (int i = 0; i < (int)skids.size(); ++i) {
            if (isTerminalValue(skids.at(i), "{")) {
                brace = i;
                break;
            }
        }
        if (brace != -1 && brace + 1 < (int)skids.size()) {
            collectStructDecls(skids.at(brace + 1), st.fields);
        }
        typeSpec.structType = std::move(st);
        return typeSpec;
    }

    typeSpec.kind = ast::TypeSpec::Kind::Builtin;
    typeSpec.builtin = tokenValue(kids.at(0));
    return typeSpec;
}

ast::AbstractDeclarator buildAbstractDeclaratorFromNode(const Node::Ptr& node) {
    ast::AbstractDeclarator decl;
    const auto& kids = node->getChildren();
    if (!kids.empty() && kids.at(0)->getType() == pointer) {
        decl.pointerDepth = countPointerDepth(kids.at(0));
        if (kids.size() > 1 && !kids.at(1)->getChildren().empty()) {
            decl.hasDirect = true;
            decl.direct = buildDirectAbstractDeclaratorFromNode(kids.at(1)->getChildren().at(0));
        }
    } else {
        decl.hasDirect = true;
        decl.direct = buildDirectAbstractDeclaratorFromNode(kids.at(0));
    }
    return decl;
}

Node::Ptr findFirstNodeOfType(const Node::Ptr& node, Symbol sym) {
    if (!node) return nullptr;
    if (node->getType() == sym) return node;
    for (const auto& child : node->getChildren()) {
        if (auto res = findFirstNodeOfType(child, sym)) return res;
    }
    return nullptr;
}

} // namespace ir::detail

