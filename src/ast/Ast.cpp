#include "Ast.h"

#include <ostream>
#include <sstream>
#include <type_traits>

#include "../helper/Symbol.h"

namespace ast {

namespace {

struct RenderResult {
    std::string text;
    bool isAtomic = true;
};

static void indent(std::ostream& os, int level) {
    for (int i = 0; i < level; ++i) os << "\t";
}

static bool isTerminalValue(const Node::Ptr& node, const std::string& value) {
    if (!node || node->getType() != terminal) return false;
    if (!node->getToken().has_value()) return false;
    return node->getToken()->getValue() == value;
}

static std::string tokenValue(const Node::Ptr& node) {
    if (!node || !node->getToken().has_value()) return "";
    return node->getToken()->getValue();
}

static SourceLocation tokenLocation(const Node::Ptr& node) {
    SourceLocation loc;
    if (!node || !node->getToken().has_value()) return loc;
    loc.line = node->getToken()->getSourceLine();
    loc.column = node->getToken()->getSourceIndex();
    return loc;
}

static std::string renderTypeInline(const TypeSpec& type);
static std::string renderTypeInlineFull(const TypeSpec& type);
static std::string renderExpr(const Node::Ptr& node);

static RenderResult renderDeclarator(const Declarator& decl);
static RenderResult renderAbstractDeclarator(const AbstractDeclarator& decl);
static RenderResult renderDeclaratorCore(const Declarator& decl);
static RenderResult renderAbstractDeclaratorCore(const AbstractDeclarator& decl);

static TypeSpec buildType(const Node::Ptr& node);
static AbstractDeclarator buildAbstractDeclarator(const Node::Ptr& node);

static std::string renderParamDecl(const ParamDecl& param) {
    std::ostringstream ss;
    ss << renderTypeInlineFull(param.type);
    if (param.declarator) {
        ss << " " << renderDeclarator(*param.declarator).text;
    } else if (param.abstractDeclarator) {
        ss << " " << renderAbstractDeclarator(*param.abstractDeclarator).text;
    }
    return ss.str();
}

static std::string renderParamList(const ParamList& list) {
    std::ostringstream ss;
    for (size_t i = 0; i < list.params.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << renderParamDecl(list.params[i]);
    }
    return ss.str();
}

static RenderResult renderDirectDeclaratorCore(const DirectDeclarator& direct) {
    RenderResult res;
    if (direct.kind == DirectDeclarator::Kind::Identifier) {
        res.text = direct.identifier;
        res.isAtomic = true;
    } else {
        RenderResult inner = renderDeclaratorCore(*direct.nested);
        res = inner;
    }
    return res;
}

static void applyDirectDeclaratorSuffixes(RenderResult& res, const std::vector<ParamList>& suffixes) {
    for (const auto& params : suffixes) {
        if (params.isArray) {
            std::string inside = res.text;
            res.text = "(" + inside + "[";
            if (params.arraySize) {
                res.text += renderExpr(params.arraySize->root);
            }
            res.text += "])";
        } else {
            std::string inside = res.text;
            res.text = "(" + inside + "(" + renderParamList(params) + "))";
        }
        res.isAtomic = false;
    }
}

static RenderResult renderDeclaratorCore(const Declarator& decl) {
    RenderResult res;
    res = renderDirectDeclaratorCore(decl.direct);
    for (int i = 0; i < decl.pointerDepth; ++i) {
        res.text = "(*" + res.text + ")";
        res.isAtomic = false;
    }
    applyDirectDeclaratorSuffixes(res, decl.direct.params);
    return res;
}

static RenderResult renderDeclarator(const Declarator& decl) {
    return renderDeclaratorCore(decl);
}

static RenderResult renderDirectAbstractDeclaratorCore(const DirectAbstractDeclarator& direct) {
    RenderResult res;
    if (direct.kind == DirectAbstractDeclarator::Kind::ParamList) {
        res.text = "(" + renderParamList(direct.firstParamList) + ")";
        res.isAtomic = false;
    } else {
        RenderResult inner = renderAbstractDeclaratorCore(*direct.nested);
        res = inner;
    }
    return res;
}

static void applyDirectAbstractDeclaratorSuffixes(RenderResult& res, const std::vector<ParamList>& suffixes) {
    for (const auto& params : suffixes) {
        if (params.isArray) {
            std::string inside = res.text;
            res.text = "(" + inside + "[";
            if (params.arraySize) {
                res.text += renderExpr(params.arraySize->root);
            }
            res.text += "])";
        } else {
            std::string inside = res.text;
            res.text = "(" + inside + "(" + renderParamList(params) + "))";
        }
        res.isAtomic = false;
    }
}

static RenderResult renderAbstractDeclaratorCore(const AbstractDeclarator& decl) {
    RenderResult res;
    if (decl.hasDirect) {
        res = renderDirectAbstractDeclaratorCore(decl.direct);
    } else {
        res.text = "";
        res.isAtomic = true;
    }

    for (int i = 0; i < decl.pointerDepth; ++i) {
        res.text = "(*" + res.text + ")";
        res.isAtomic = false;
    }
    if (decl.hasDirect) {
        applyDirectAbstractDeclaratorSuffixes(res, decl.direct.suffixes);
    }
    return res;
}

static RenderResult renderAbstractDeclarator(const AbstractDeclarator& decl) {
    return renderAbstractDeclaratorCore(decl);
}

static std::string renderTypeInline(const TypeSpec& type) {
    if (type.kind == TypeSpec::Kind::Builtin) {
        return type.builtin;
    }
    std::ostringstream ss;
    ss << "struct";
    if (type.structType.name.has_value()) {
        ss << " " << *type.structType.name;
    }
    return ss.str();
}

static std::string renderTypeInlineFull(const TypeSpec& type) {
    if (type.kind != TypeSpec::Kind::Struct || type.structType.fields.empty()) {
        return renderTypeInline(type);
    }
    std::ostringstream ss;
    ss << "struct";
    if (type.structType.name.has_value()) {
        ss << " " << *type.structType.name;
    }
    ss << "\n{\n";
    for (const auto& field : type.structType.fields) {
        indent(ss, 1);
        ss << renderTypeInlineFull(field.type);
        if (field.declarator.has_value()) {
            ss << " " << renderDeclarator(*field.declarator).text;
        }
        ss << ";\n";
    }
    ss << "}";
    return ss.str();
}

static Node::Ptr findFirstNodeOfType(const Node::Ptr& node, Symbol sym) {
    if (!node) return nullptr;
    if (node->getType() == sym) return node;
    for (const auto& child : node->getChildren()) {
        if (auto res = findFirstNodeOfType(child, sym)) return res;
    }
    return nullptr;
}

static std::string renderTypeNameFromTypeNode(const Node::Ptr& typeNode) {
    if (!typeNode || typeNode->getType() != type) return "";
    TypeSpec ts = buildType(typeNode);
    std::ostringstream ss;
    ss << renderTypeInlineFull(ts);
    if (auto adNode = findFirstNodeOfType(typeNode, abstractdeclarator)) {
        auto ad = buildAbstractDeclarator(adNode);
        auto adText = renderAbstractDeclarator(ad).text;
        if (!adText.empty()) ss << " " << adText;
    }
    return ss.str();
}

static bool isAtomicExprNode(const Node::Ptr& node) {
    if (!node) return false;
    switch (node->getType()) {
        case id:
        case stringliteral:
        case charconst:
        case decimalconst:
            return true;
        default:
            return false;
    }
}

static std::string renderExpr(const Node::Ptr& node) {
    if (!node) return "";

    switch (node->getType()) {
        case id:
        case stringliteral:
        case charconst:
        case decimalconst:
            return tokenValue(node);
        case parenthesizedexpr:
            if (!node->getChildren().empty()) {
                return renderExpr(node->getChildren().front());
            }
            return "";
        case arrayaccess: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + "[" + renderExpr(kids.at(1)) + "])";
        }
        case functioncall: {
            const auto& kids = node->getChildren();
            std::ostringstream ss;
            ss << "(" << renderExpr(kids.at(0)) << "(";
            for (size_t i = 1; i < kids.size(); ++i) {
                if (i > 1) ss << ", ";
                ss << renderExpr(kids.at(i));
            }
            ss << "))";
            return ss.str();
        }
        case memberaccess: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + "." + renderExpr(kids.at(1)) + ")";
        }
        case pointermemberaccess: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + "->" + renderExpr(kids.at(1)) + ")";
        }
        case reference:
            return "(&" + renderExpr(node->getChildren().at(0)) + ")";
        case dereference:
            return "(*" + renderExpr(node->getChildren().at(0)) + ")";
        case negationarithmetic:
            return "(-" + renderExpr(node->getChildren().at(0)) + ")";
        case negationlogical:
            return "(!" + renderExpr(node->getChildren().at(0)) + ")";
        case preincrement:
            return "(++" + renderExpr(node->getChildren().at(0)) + ")";
        case predecrement:
            return "(--" + renderExpr(node->getChildren().at(0)) + ")";
        case postincrement:
        {
            const auto& child = node->getChildren().at(0);
            std::string op = renderExpr(child);
            if (isAtomicExprNode(child)) op = "(" + op + ")";
            return "(" + op + "++)";
        }
        case postdecrement:
        {
            const auto& child = node->getChildren().at(0);
            std::string op = renderExpr(child);
            if (isAtomicExprNode(child)) op = "(" + op + ")";
            return "(" + op + "--)";
        }
        case sizeoperator: {
            const auto& kids = node->getChildren();
            if (!kids.empty() && kids.front()->getType() == type) {
                return "(sizeof(" + renderTypeNameFromTypeNode(kids.front()) + "))";
            }
            return "(sizeof " + renderExpr(kids.at(0)) + ")";
        }
        case product: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + " * " + renderExpr(kids.at(1)) + ")";
        }
        case sum: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + " + " + renderExpr(kids.at(1)) + ")";
        }
        case difference: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + " - " + renderExpr(kids.at(1)) + ")";
        }
        case comparison: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + " < " + renderExpr(kids.at(1)) + ")";
        }
        case equality: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + " == " + renderExpr(kids.at(1)) + ")";
        }
        case inequality: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + " != " + renderExpr(kids.at(1)) + ")";
        }
        case conjunction: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + " && " + renderExpr(kids.at(1)) + ")";
        }
        case disjunction: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + " || " + renderExpr(kids.at(1)) + ")";
        }
        case ternary: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + " ? " + renderExpr(kids.at(1)) + " : " + renderExpr(kids.at(2)) + ")";
        }
        case assignment: {
            const auto& kids = node->getChildren();
            return "(" + renderExpr(kids.at(0)) + " = " + renderExpr(kids.at(1)) + ")";
        }
        default:
            return "";
    }
}

static void printDecl(const Decl& decl, std::ostream& os, int level);
static void printStatement(const Statement& stmt, std::ostream& os, int level);
static bool printIf(const StmtIf& stmt, std::ostream& os, int level, bool inlineHead);

static void printCompoundInline(const StmtCompound& stmt, std::ostream& os, int level) {
    os << "{\n";
    for (const auto& item : stmt.items) {
        printStatement(*item, os, level + 1);
    }
    indent(os, level);
    os << "}";
}

static void printCompound(const StmtCompound& stmt, std::ostream& os, int level) {
    indent(os, level);
    printCompoundInline(stmt, os, level);
    os << "\n";
}

static bool printIf(const StmtIf& stmt, std::ostream& os, int level, bool inlineHead) {
    if (!inlineHead) indent(os, level);
    os << "if (" << renderExpr(stmt.condition.root) << ")";
    bool thenCompound = std::holds_alternative<StmtCompound>(stmt.thenStmt->node);
    if (thenCompound) {
        os << " ";
        printCompoundInline(std::get<StmtCompound>(stmt.thenStmt->node), os, level);
    } else {
        os << "\n";
        printStatement(*stmt.thenStmt, os, level + 1);
    }

    bool endsWithNewline = !thenCompound;

    if (stmt.elseStmt) {
        if (thenCompound) {
            os << " else";
        } else {
            indent(os, level);
            os << "else";
        }

        if (std::holds_alternative<StmtIf>(stmt.elseStmt->node)) {
            os << " ";
            endsWithNewline = printIf(std::get<StmtIf>(stmt.elseStmt->node), os, level, true);
        } else if (std::holds_alternative<StmtCompound>(stmt.elseStmt->node)) {
            os << " ";
            printCompoundInline(std::get<StmtCompound>(stmt.elseStmt->node), os, level);
            endsWithNewline = false;
        } else {
            os << "\n";
            printStatement(*stmt.elseStmt, os, level + 1);
            endsWithNewline = true;
        }
    } else if (thenCompound) {
        endsWithNewline = false;
    }

    if (!endsWithNewline) {
        os << "\n";
        endsWithNewline = true;
    }
    return endsWithNewline;
}

static void printWhile(const StmtWhile& stmt, std::ostream& os, int level) {
    indent(os, level);
    os << "while (" << renderExpr(stmt.condition.root) << ")";
    if (std::holds_alternative<StmtCompound>(stmt.body->node)) {
        os << " ";
        printCompoundInline(std::get<StmtCompound>(stmt.body->node), os, level);
        os << "\n";
    } else {
        os << "\n";
        printStatement(*stmt.body, os, level + 1);
    }
}

static void printStatement(const Statement& stmt, std::ostream& os, int level) {
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, StmtCompound>) {
            printCompound(arg, os, level);
        } else if constexpr (std::is_same_v<T, StmtDecl>) {
            printDecl(arg.decl, os, level);
        } else if constexpr (std::is_same_v<T, StmtExpr>) {
            indent(os, level);
            if (arg.expr.has_value()) {
                os << renderExpr(arg.expr->root);
            }
            os << ";\n";
        } else if constexpr (std::is_same_v<T, StmtIf>) {
            printIf(arg, os, level, false);
        } else if constexpr (std::is_same_v<T, StmtWhile>) {
            printWhile(arg, os, level);
        } else if constexpr (std::is_same_v<T, StmtLabel>) {
            os << arg.label << ":\n";
            printStatement(*arg.stmt, os, level);
        } else if constexpr (std::is_same_v<T, StmtGoto>) {
            indent(os, level);
            os << "goto " << arg.label << ";\n";
        } else if constexpr (std::is_same_v<T, StmtContinue>) {
            indent(os, level);
            os << "continue;\n";
        } else if constexpr (std::is_same_v<T, StmtBreak>) {
            indent(os, level);
            os << "break;\n";
        } else if constexpr (std::is_same_v<T, StmtReturn>) {
            indent(os, level);
            os << "return";
            if (arg.expr.has_value()) {
                os << " " << renderExpr(arg.expr->root);
            }
            os << ";\n";
        }
    }, stmt.node);
}

static void printDecl(const Decl& decl, std::ostream& os, int level) {
    if (decl.type.kind == TypeSpec::Kind::Struct && !decl.type.structType.fields.empty()) {
        indent(os, level);
        os << "struct";
        if (decl.type.structType.name.has_value()) {
            os << " " << *decl.type.structType.name;
        }
        os << "\n";
        indent(os, level);
        os << "{\n";
        for (const auto& field : decl.type.structType.fields) {
            printDecl(field, os, level + 1);
        }
        indent(os, level);
        os << "}";
        if (decl.declarator.has_value()) {
            os << " " << renderDeclarator(*decl.declarator).text;
        }
        os << ";\n";
        return;
    }

    indent(os, level);
    os << renderTypeInlineFull(decl.type);
    if (decl.declarator.has_value()) {
        os << " " << renderDeclarator(*decl.declarator).text;
    }
    os << ";\n";
}

} // namespace

// ---------------------------
// AST Builder
// ---------------------------

namespace {

static TypeSpec buildType(const Node::Ptr& node);
static Decl buildDeclFromDec(const Node::Ptr& node);
static Declarator buildDeclarator(const Node::Ptr& node);
static AbstractDeclarator buildAbstractDeclarator(const Node::Ptr& node);
static ParamList buildParamList(const Node::Ptr& node);
static ParamDecl buildParamDecl(const Node::Ptr& node);
static std::shared_ptr<Statement> buildStatement(const Node::Ptr& node);

static void collectStructDecls(const Node::Ptr& node, std::vector<Decl>& out) {
    if (!node || node->getChildren().empty()) return;
    out.push_back(buildDeclFromDec(node->getChildren().at(0)));
    collectStructDecls(node->getChildren().at(1), out);
}

static void collectBlockItems(const Node::Ptr& node, std::vector<std::shared_ptr<Statement>>& out) {
    if (!node || node->getChildren().empty()) return;
    out.push_back(buildStatement(node->getChildren().at(0)));
    collectBlockItems(node->getChildren().at(1), out);
}

static void collectParamListTail(const Node::Ptr& node, std::vector<ParamDecl>& out) {
    if (!node || node->getChildren().empty()) return;
    out.push_back(buildParamDecl(node->getChildren().at(1)));
    collectParamListTail(node->getChildren().at(2), out);
}

static ParamList buildArraySuffix(const Node::Ptr& node) {
    ParamList list;
    list.isArray = true;
    const auto& kids = node->getChildren();
    if (kids.size() > 3 && kids.at(1)->getType() == expr && !kids.at(1)->getChildren().empty()) {
        list.arraySize = Expr{kids.at(1)->getChildren().at(0)};
    }
    return list;
}

static void collectDirectDecSuffixes(const Node::Ptr& node, std::vector<ParamList>& out) {
    if (!node || node->getChildren().empty()) return;
    const auto& kids = node->getChildren();
    if (isTerminalValue(kids.at(0), "[")) {
        out.push_back(buildArraySuffix(node));
        collectDirectDecSuffixes(kids.back(), out);
    } else if (kids.at(1)->getType() == paramlist) {
        out.push_back(buildParamList(kids.at(1)));
        collectDirectDecSuffixes(kids.back(), out);
    } else {
        out.push_back(ParamList{});
        collectDirectDecSuffixes(kids.back(), out);
    }
}

static void collectDirectAbstractSuffixes(const Node::Ptr& node, std::vector<ParamList>& out) {
    if (!node || node->getChildren().empty()) return;
    const auto& kids = node->getChildren();
    if (isTerminalValue(kids.at(0), "[")) {
        out.push_back(buildArraySuffix(node));
        collectDirectAbstractSuffixes(kids.back(), out);
    } else if (kids.at(1)->getType() == paramlist) {
        out.push_back(buildParamList(kids.at(1)));
        collectDirectAbstractSuffixes(kids.back(), out);
    } else {
        out.push_back(ParamList{});
        collectDirectAbstractSuffixes(kids.back(), out);
    }
}

static int countPointerDepth(const Node::Ptr& node) {
    if (!node || node->getType() != pointer) return 0;
    int count = 1;
    Node::Ptr current = node->getChildren().at(1);
    while (current && !current->getChildren().empty()) {
        count++;
        current = current->getChildren().at(1);
    }
    return count;
}

static TypeSpec buildType(const Node::Ptr& node) {
    TypeSpec typeSpec;
    const auto& kids = node->getChildren();
    if (kids.empty()) return typeSpec;

    if (kids.at(0)->getType() == structtype) {
        typeSpec.kind = TypeSpec::Kind::Struct;
        typeSpec.loc = tokenLocation(kids.at(0)->getChildren().at(0));
        const auto& skids = kids.at(0)->getChildren();
        StructType st;
        if (skids.size() >= 2 && skids.at(1)->getType() == id) {
            st.name = tokenValue(skids.at(1));
            st.nameLoc = tokenLocation(skids.at(1));
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

    typeSpec.kind = TypeSpec::Kind::Builtin;
    typeSpec.builtin = tokenValue(kids.at(0));
    typeSpec.loc = tokenLocation(kids.at(0));
    return typeSpec;
}

static DirectDeclarator buildDirectDeclarator(const Node::Ptr& node) {
    DirectDeclarator direct;
    const auto& kids = node->getChildren();
    if (!kids.empty() && isTerminalValue(kids.at(0), "(")) {
        direct.kind = DirectDeclarator::Kind::Nested;
        direct.nested = std::make_shared<Declarator>(buildDeclarator(kids.at(1)));
    } else {
        direct.kind = DirectDeclarator::Kind::Identifier;
        direct.identifier = tokenValue(kids.at(0));
        direct.identifierLoc = tokenLocation(kids.at(0));
    }

    const auto& suffix = kids.at(kids.size() - 1);
    collectDirectDecSuffixes(suffix, direct.params);

    return direct;
}

static Declarator buildDeclarator(const Node::Ptr& node) {
    Declarator decl;
    const auto& kids = node->getChildren();
    size_t idx = 0;
    if (!kids.empty() && kids.at(0)->getType() == pointer) {
        decl.pointerDepth = countPointerDepth(kids.at(0));
        idx = 1;
    }
    decl.direct = buildDirectDeclarator(kids.at(idx));
    return decl;
}

static DirectAbstractDeclarator buildDirectAbstractDeclarator(const Node::Ptr& node) {
    DirectAbstractDeclarator direct;
    const auto& kids = node->getChildren();
    if (kids.size() < 3) return direct;

    if (kids.at(1)->getType() == paramlist || isTerminalValue(kids.at(1), ")")) {
        direct.kind = DirectAbstractDeclarator::Kind::ParamList;
        if (kids.at(1)->getType() == paramlist) {
            direct.firstParamList = buildParamList(kids.at(1));
        }
    } else {
        direct.kind = DirectAbstractDeclarator::Kind::Nested;
        direct.nested = std::make_shared<AbstractDeclarator>(buildAbstractDeclarator(kids.at(1)));
    }

    const size_t suffixIndex = kids.size() > 3 ? 3 : 2;
    if (suffixIndex < kids.size()) {
        collectDirectAbstractSuffixes(kids.at(suffixIndex), direct.suffixes);
    }
    return direct;
}

static AbstractDeclarator buildAbstractDeclarator(const Node::Ptr& node) {
    AbstractDeclarator decl;
    const auto& kids = node->getChildren();
    if (!kids.empty() && kids.at(0)->getType() == pointer) {
        decl.pointerDepth = countPointerDepth(kids.at(0));
        if (kids.size() > 1 && !kids.at(1)->getChildren().empty()) {
            decl.hasDirect = true;
            decl.direct = buildDirectAbstractDeclarator(kids.at(1)->getChildren().at(0));
        }
    } else {
        decl.hasDirect = true;
        decl.direct = buildDirectAbstractDeclarator(kids.at(0));
    }
    return decl;
}

static ParamDecl buildParamDecl(const Node::Ptr& node) {
    ParamDecl param;
    const auto& kids = node->getChildren();
    param.type = buildType(kids.at(0));
    if (kids.size() > 1 && !kids.at(1)->getChildren().empty()) {
        const auto& inner = kids.at(1)->getChildren().at(0);
        if (inner->getType() == declarator) {
            param.declarator = std::make_shared<Declarator>(buildDeclarator(inner));
        } else if (inner->getType() == abstractdeclarator) {
            param.abstractDeclarator = std::make_shared<AbstractDeclarator>(buildAbstractDeclarator(inner));
        }
    }
    return param;
}

static ParamList buildParamList(const Node::Ptr& node) {
    ParamList list;
    const auto& kids = node->getChildren();
    list.params.push_back(buildParamDecl(kids.at(0)));
    if (kids.size() > 1) collectParamListTail(kids.at(1), list.params);
    return list;
}

static Decl buildDeclFromDec(const Node::Ptr& node) {
    Decl decl;
    const auto& kids = node->getChildren();
    decl.type = buildType(kids.at(0));
    const auto& decTail = kids.at(1);
    if (decTail && decTail->getChildren().size() > 1) {
        decl.declarator = buildDeclarator(decTail->getChildren().at(0));
    }
    return decl;
}

static StmtCompound buildCompoundStatement(const Node::Ptr& node) {
    StmtCompound compound;
    const auto& kids = node->getChildren();
    if (kids.size() >= 2) {
        collectBlockItems(kids.at(1), compound.items);
    }
    return compound;
}

static std::shared_ptr<Statement> buildStatement(const Node::Ptr& node) {
    auto stmt = std::make_shared<Statement>();
    const auto& kids = node->getChildren();
    if (kids.empty()) {
        stmt->node = StmtExpr{};
        return stmt;
    }
    const auto& child = kids.at(0);
    switch (child->getType()) {
        case compoundstatement: {
            stmt->node = buildCompoundStatement(child);
            return stmt;
        }
        case blockitem: {
            return buildStatement(child);
        }
        case dec: {
            stmt->node = StmtDecl{buildDeclFromDec(child)};
            return stmt;
        }
        case exprstatement: {
            StmtExpr exprStmt;
            const auto& ekids = child->getChildren();
            if (!ekids.empty() && ekids.at(0)->getType() == expr) {
                exprStmt.expr = Expr{ekids.at(0)->getChildren().at(0)};
            }
            stmt->node = exprStmt;
            return stmt;
        }
        case labelstatement: {
            StmtLabel labelStmt;
            labelStmt.label = tokenValue(child->getChildren().at(0));
            labelStmt.loc = tokenLocation(child->getChildren().at(0));
            labelStmt.stmt = buildStatement(child->getChildren().at(2));
            stmt->node = labelStmt;
            return stmt;
        }
        case selectstatement: {
            StmtIf ifStmt;
            ifStmt.loc = tokenLocation(child->getChildren().at(0));
            ifStmt.condition = Expr{child->getChildren().at(2)->getChildren().at(0)};
            ifStmt.thenStmt = buildStatement(child->getChildren().at(4));
            const auto& elseNode = child->getChildren().at(5);
            if (!elseNode->getChildren().empty()) {
                ifStmt.elseStmt = buildStatement(elseNode->getChildren().at(1));
            }
            stmt->node = ifStmt;
            return stmt;
        }
        case iterstatement: {
            StmtWhile whileStmt;
            whileStmt.loc = tokenLocation(child->getChildren().at(0));
            whileStmt.condition = Expr{child->getChildren().at(2)->getChildren().at(0)};
            whileStmt.body = buildStatement(child->getChildren().at(4));
            stmt->node = whileStmt;
            return stmt;
        }
        case jumpstatement: {
            const auto& jkids = child->getChildren();
            if (isTerminalValue(jkids.at(0), "goto")) {
                stmt->node = StmtGoto{tokenValue(jkids.at(1)), tokenLocation(jkids.at(0))};
            } else if (isTerminalValue(jkids.at(0), "continue")) {
                stmt->node = StmtContinue{tokenLocation(jkids.at(0))};
            } else if (isTerminalValue(jkids.at(0), "break")) {
                stmt->node = StmtBreak{tokenLocation(jkids.at(0))};
            } else if (isTerminalValue(jkids.at(0), "return") && jkids.size() > 2) {
                StmtReturn ret;
                ret.loc = tokenLocation(jkids.at(0));
                ret.expr = Expr{jkids.at(1)->getChildren().at(0)};
                stmt->node = ret;
            } else {
                StmtReturn ret;
                ret.loc = tokenLocation(jkids.at(0));
                stmt->node = ret;
            }
            return stmt;
        }
        case statement:
            return buildStatement(child);
        default:
            stmt->node = StmtExpr{};
            return stmt;
    }
}

static ExternalDecl buildExternalDecl(const Node::Ptr& node) {
    ExternalDecl ext;
    const auto& kids = node->getChildren();
    TypeSpec typeSpec = buildType(kids.at(0));
    const auto& tail = kids.at(1);
    if (tail->getChildren().size() == 1) {
        Decl decl{typeSpec, std::nullopt};
        ext.node = decl;
        return ext;
    }

    Declarator decl = buildDeclarator(tail->getChildren().at(0));
    const auto& tail2 = tail->getChildren().at(1);
    const auto& tail2Child = tail2->getChildren().at(0);
    if (tail2Child->getType() == decEnd) {
        Decl declNode{typeSpec, decl};
        ext.node = declNode;
        return ext;
    }

    FuncDef func;
    func.type = typeSpec;
    func.declarator = decl;
    func.body = buildCompoundStatement(tail2Child->getChildren().at(0));
    ext.node = func;
    return ext;
}

static void collectTransUnit(const Node::Ptr& node, std::vector<ExternalDecl>& out) {
    if (!node || node->getChildren().empty()) return;
    out.push_back(buildExternalDecl(node->getChildren().at(0)));
    collectTransUnit(node->getChildren().at(1), out);
}

} // namespace

TranslationUnit buildFromParseTree(const Node::Ptr& root) {
    TranslationUnit tu;
    if (!root || root->getChildren().empty()) return tu;
    const auto& trans = root->getChildren().at(0);
    if (!trans->getChildren().empty()) {
        tu.decls.push_back(buildExternalDecl(trans->getChildren().at(0)));
        collectTransUnit(trans->getChildren().at(1), tu.decls);
    }
    return tu;
}

void printAst(const TranslationUnit& tu, std::ostream& os) {
    for (size_t i = 0; i < tu.decls.size(); ++i) {
        const auto& ext = tu.decls[i];
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Decl>) {
                printDecl(arg, os, 0);
            } else if constexpr (std::is_same_v<T, FuncDef>) {
                os << renderTypeInlineFull(arg.type) << " " << renderDeclarator(arg.declarator).text << "\n";
                printCompound(arg.body, os, 0);
            }
        }, ext.node);

        if (i + 1 < tu.decls.size()) os << "\n";
    }
}

} // namespace ast
