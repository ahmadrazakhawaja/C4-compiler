#include "Semantic.h"

#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <sstream>

#include "../helper/Symbol.h"

namespace semantic {

namespace {

using ast::SourceLocation;

struct Type {
    enum class Kind { Void, Char, Int, Struct, Pointer, Function, Error };
    Kind kind = Kind::Error;
    std::string structName;
    std::shared_ptr<Type> pointee;
    std::shared_ptr<Type> returnType;
    std::vector<Type> params;
};

static Type makeError() { return Type{Type::Kind::Error}; }
static Type makeVoid() { return Type{Type::Kind::Void}; }
static Type makeChar() { return Type{Type::Kind::Char}; }
static Type makeInt() { return Type{Type::Kind::Int}; }
static Type makeStruct(const std::string& name) { Type t; t.kind = Type::Kind::Struct; t.structName = name; return t; }
static Type makePointer(const Type& base) { Type t; t.kind = Type::Kind::Pointer; t.pointee = std::make_shared<Type>(base); return t; }
static Type makeFunction(const Type& ret, const std::vector<Type>& params) {
    Type t;
    t.kind = Type::Kind::Function;
    t.returnType = std::make_shared<Type>(ret);
    t.params = params;
    return t;
}

static bool isError(const Type& t) { return t.kind == Type::Kind::Error; }
static bool isStruct(const Type& t) { return t.kind == Type::Kind::Struct; }
static bool isPointer(const Type& t) { return t.kind == Type::Kind::Pointer; }
static bool isFunction(const Type& t) { return t.kind == Type::Kind::Function; }
static bool isInteger(const Type& t) { return t.kind == Type::Kind::Int || t.kind == Type::Kind::Char; }
static bool isScalar(const Type& t) { return isInteger(t) || isPointer(t); }

static bool typeEqual(const Type& a, const Type& b) {
    if (isError(a) || isError(b)) return true;
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case Type::Kind::Void:
        case Type::Kind::Char:
        case Type::Kind::Int:
            return true;
        case Type::Kind::Struct:
            return a.structName == b.structName;
        case Type::Kind::Pointer:
            return typeEqual(*a.pointee, *b.pointee);
        case Type::Kind::Function:
            if (!typeEqual(*a.returnType, *b.returnType)) return false;
            if (a.params.size() != b.params.size()) return false;
            for (size_t i = 0; i < a.params.size(); ++i) {
                if (!typeEqual(a.params[i], b.params[i])) return false;
            }
            return true;
        default:
            return false;
    }
}

struct SymbolInfo {
    Type type;
    bool isFunction = false;
    bool defined = false;
    SourceLocation loc;
};

struct StructInfo {
    bool defined = false;
    std::unordered_map<std::string, Type> fields;
    SourceLocation loc;
};

struct ExprInfo {
    Type type;
    bool isLvalue = false;
    bool isNullPtrConst = false;
    SourceLocation loc;
};

static SourceLocation locFromNode(const Node::Ptr& node) {
    SourceLocation loc;
    if (!node || !node->getToken().has_value()) return loc;
    loc.line = node->getToken()->getSourceLine();
    loc.column = node->getToken()->getSourceIndex();
    return loc;
}

class Analyzer {
public:
    explicit Analyzer(std::ostream& errStream, std::string file)
        : err(errStream), fileName(std::move(file)) {
        scopes.emplace_back();
    }

    bool run(const ast::TranslationUnit& tu) {
        for (const auto& ext : tu.decls) {
            analyzeExternal(ext);
        }
        return ok;
    }

private:
    std::ostream& err;
    std::string fileName;
    bool ok = true;
    std::vector<std::unordered_map<std::string, SymbolInfo>> scopes;
    std::unordered_map<std::string, StructInfo> structs;

    bool inFunction = false;
    bool inLoop = false;
    Type currentReturnType;
    std::unordered_map<std::string, SourceLocation> functionLabels;
    std::vector<std::pair<std::string, SourceLocation>> pendingGotos;

    void pushScope() { scopes.emplace_back(); }
    void popScope() { scopes.pop_back(); }

    void report(const SourceLocation& loc, const std::string& msg) {
        if (loc.line >= 0) {
            err << fileName << ":" << loc.line + 1 << ":" << loc.column + 1
                << ": error: " << msg << "\n";
        } else {
            err << fileName << ": error: " << msg << "\n";
        }
        ok = false;
    }

    SymbolInfo* lookup(const std::string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return &found->second;
        }
        return nullptr;
    }

    bool declare(const std::string& name, const SymbolInfo& info) {
        auto& scope = scopes.back();
        if (scope.find(name) != scope.end()) {
            report(info.loc, "redeclaration of '" + name + "'");
            return false;
        }
        scope.emplace(name, info);
        return true;
    }

    Type typeFromTypeSpec(const ast::TypeSpec& spec) {
        if (spec.kind == ast::TypeSpec::Kind::Builtin) {
            if (spec.builtin == "int") return makeInt();
            if (spec.builtin == "char") return makeChar();
            if (spec.builtin == "void") return makeVoid();
            return makeError();
        }

        if (!spec.structType.name.has_value()) {
            report(spec.loc, "anonymous structs are not supported");
            return makeError();
        }

        const std::string& name = *spec.structType.name;
        if (!spec.structType.fields.empty()) {
            auto it = structs.find(name);
            if (it != structs.end() && it->second.defined) {
                report(spec.structType.nameLoc, "redefinition of struct '" + name + "'");
            }

            StructInfo info;
            info.defined = true;
            info.loc = spec.loc;

            for (const auto& field : spec.structType.fields) {
                const ast::Declarator* decl = field.declarator ? &(*field.declarator) : nullptr;
                if (!decl) {
                    report(spec.loc, "struct field missing declarator");
                    continue;
                }
                std::string fieldName;
                SourceLocation fieldLoc;
                if (!extractDeclaratorName(*decl, fieldName, fieldLoc)) {
                    report(spec.loc, "struct field missing identifier");
                    continue;
                }

                Type fieldType = applyDeclarator(typeFromTypeSpec(field.type), *decl);
                if (info.fields.find(fieldName) != info.fields.end()) {
                    report(fieldLoc, "duplicate field '" + fieldName + "'");
                    continue;
                }
                info.fields.emplace(fieldName, fieldType);
            }

            structs[name] = std::move(info);
        } else {
            auto it = structs.find(name);
            if (it == structs.end()) {
                StructInfo info;
                info.defined = false;
                info.loc = spec.loc;
                structs.emplace(name, std::move(info));
            }
        }

        return makeStruct(name);
    }

    static bool extractDeclaratorName(const ast::Declarator& decl, std::string& name, SourceLocation& loc) {
        if (decl.direct.kind == ast::DirectDeclarator::Kind::Identifier) {
            name = decl.direct.identifier;
            loc = decl.direct.identifierLoc;
            return !name.empty();
        }
        if (decl.direct.nested) return extractDeclaratorName(*decl.direct.nested, name, loc);
        return false;
    }

    Type applySuffixes(const Type& base, const std::vector<ast::ParamList>& params) {
        Type current = base;
        for (const auto& plist : params) {
            if (plist.isArray) {
                current = makePointer(current);
                continue;
            }
            std::vector<Type> paramTypes;
            for (const auto& param : plist.params) {
                Type paramType = typeFromTypeSpec(param.type);
                if (param.declarator) {
                    paramType = applyDeclarator(paramType, *param.declarator);
                } else if (param.abstractDeclarator) {
                    paramType = applyAbstractDeclarator(paramType, *param.abstractDeclarator);
                }
                paramTypes.push_back(paramType);
            }
            current = makeFunction(current, paramTypes);
        }
        return current;
    }

    Type applyDeclarator(const Type& base, const ast::Declarator& decl) {
        Type current = base;
        for (int i = 0; i < decl.pointerDepth; ++i) {
            current = makePointer(current);
        }

        if (decl.direct.kind == ast::DirectDeclarator::Kind::Nested) {
            current = applySuffixes(current, decl.direct.params);
            if (decl.direct.nested) {
                current = applyDeclarator(current, *decl.direct.nested);
            }
            return current;
        }

        current = applySuffixes(current, decl.direct.params);
        return current;
    }

    Type applyAbstractDeclarator(const Type& base, const ast::AbstractDeclarator& decl) {
        Type current = base;
        for (int i = 0; i < decl.pointerDepth; ++i) {
            current = makePointer(current);
        }
        if (decl.hasDirect) {
            if (decl.direct.kind == ast::DirectAbstractDeclarator::Kind::ParamList) {
                current = applySuffixes(current, prependParamList(decl.direct.firstParamList, decl.direct.suffixes));
            } else if (!decl.direct.suffixes.empty()) {
                current = applySuffixes(current, decl.direct.suffixes);
            }
            if (decl.direct.kind == ast::DirectAbstractDeclarator::Kind::Nested && decl.direct.nested) {
                current = applyAbstractDeclarator(current, *decl.direct.nested);
            }
        }
        return current;
    }

    std::vector<ast::ParamList> prependParamList(const ast::ParamList& first, const std::vector<ast::ParamList>& rest) {
        std::vector<ast::ParamList> out;
        out.push_back(first);
        out.insert(out.end(), rest.begin(), rest.end());
        return out;
    }

    void analyzeExternal(const ast::ExternalDecl& ext) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, ast::Decl>) {
                analyzeDecl(arg);
            } else if constexpr (std::is_same_v<T, ast::FuncDef>) {
                analyzeFunction(arg);
            }
        }, ext.node);
    }

    void analyzeDecl(const ast::Decl& decl) {
        Type base = typeFromTypeSpec(decl.type);
        if (!decl.declarator) {
            return;
        }

        std::string name;
        SourceLocation nameLoc;
        if (!extractDeclaratorName(*decl.declarator, name, nameLoc)) {
            report(decl.type.loc, "declarator missing identifier");
            return;
        }

        Type full = applyDeclarator(base, *decl.declarator);
        if (full.kind == Type::Kind::Void) {
            report(nameLoc, "variable '" + name + "' has void type");
        }

        SymbolInfo info;
        info.type = full;
        info.isFunction = isFunction(full);
        info.defined = false;
        info.loc = nameLoc;
        declare(name, info);
    }

    void analyzeFunction(const ast::FuncDef& func) {
        Type base = typeFromTypeSpec(func.type);
        std::string name;
        SourceLocation nameLoc;
        if (!extractDeclaratorName(func.declarator, name, nameLoc)) {
            report(func.type.loc, "function missing identifier");
            return;
        }

        Type funcType = applyDeclarator(base, func.declarator);
        if (!isFunction(funcType)) {
            report(nameLoc, "function definition is not a function type");
            return;
        }

        if (isStruct(*funcType.returnType)) {
            report(nameLoc, "functions returning struct are not supported");
        }
        for (const auto& param : funcType.params) {
            if (isStruct(param)) {
                report(nameLoc, "struct parameters are not supported");
            }
        }

        SymbolInfo info;
        info.type = funcType;
        info.isFunction = true;
        info.defined = true;
        info.loc = nameLoc;

        auto existing = lookup(name);
        if (existing && existing->isFunction) {
            if (!typeEqual(existing->type, funcType)) {
                report(nameLoc, "conflicting types for function '" + name + "'");
            } else if (existing->defined) {
                report(nameLoc, "redefinition of function '" + name + "'");
            } else {
                existing->defined = true;
            }
        } else {
            declare(name, info);
        }

        inFunction = true;
        currentReturnType = *funcType.returnType;
        functionLabels.clear();
        pendingGotos.clear();

        pushScope();
        addParameters(func.declarator, funcType);
        analyzeCompound(func.body);
        popScope();

        for (const auto& gotoPair : pendingGotos) {
            if (functionLabels.find(gotoPair.first) == functionLabels.end()) {
                report(gotoPair.second, "goto to undefined label '" + gotoPair.first + "'");
            }
        }

        inFunction = false;
    }

    void addParameters(const ast::Declarator& decl, const Type& funcType) {
        std::vector<const ast::ParamDecl*> params;
        collectParamDecls(decl, params);

        size_t paramIndex = 0;
        for (const auto* param : params) {
            if (paramIndex >= funcType.params.size()) break;
            if (param->declarator) {
                std::string name;
                SourceLocation loc;
                if (extractDeclaratorName(*param->declarator, name, loc)) {
                    SymbolInfo info;
                    info.type = funcType.params.at(paramIndex);
                    info.loc = loc;
                    declare(name, info);
                }
            }
            paramIndex++;
        }
    }

    static void collectParamDecls(const ast::Declarator& decl, std::vector<const ast::ParamDecl*>& out) {
        if (!decl.direct.params.empty()) {
            for (const auto& param : decl.direct.params.front().params) {
                out.push_back(&param);
            }
            return;
        }
        if (decl.direct.kind == ast::DirectDeclarator::Kind::Nested && decl.direct.nested) {
            collectParamDecls(*decl.direct.nested, out);
        }
    }

    void analyzeCompound(const ast::StmtCompound& compound) {
        pushScope();
        for (const auto& item : compound.items) {
            analyzeStatement(*item);
        }
        popScope();
    }

    void analyzeStatement(const ast::Statement& stmt) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, ast::StmtCompound>) {
                analyzeCompound(arg);
            } else if constexpr (std::is_same_v<T, ast::StmtDecl>) {
                analyzeDecl(arg.decl);
            } else if constexpr (std::is_same_v<T, ast::StmtExpr>) {
                if (arg.expr.has_value()) analyzeExpr(arg.expr->root);
            } else if constexpr (std::is_same_v<T, ast::StmtIf>) {
                ExprInfo cond = analyzeExpr(arg.condition.root);
                if (!isScalar(cond.type)) {
                    report(arg.loc, "if condition is not scalar");
                }
                analyzeStatement(*arg.thenStmt);
                if (arg.elseStmt) analyzeStatement(*arg.elseStmt);
            } else if constexpr (std::is_same_v<T, ast::StmtWhile>) {
                ExprInfo cond = analyzeExpr(arg.condition.root);
                if (!isScalar(cond.type)) {
                    report(arg.loc, "while condition is not scalar");
                }
                bool prev = inLoop;
                inLoop = true;
                analyzeStatement(*arg.body);
                inLoop = prev;
            } else if constexpr (std::is_same_v<T, ast::StmtLabel>) {
                if (functionLabels.find(arg.label) != functionLabels.end()) {
                    report(arg.loc, "duplicate label '" + arg.label + "'");
                } else {
                    functionLabels[arg.label] = arg.loc;
                }
                analyzeStatement(*arg.stmt);
            } else if constexpr (std::is_same_v<T, ast::StmtGoto>) {
                pendingGotos.emplace_back(arg.label, arg.loc);
            } else if constexpr (std::is_same_v<T, ast::StmtContinue>) {
                if (!inLoop) report(arg.loc, "continue not within a loop");
            } else if constexpr (std::is_same_v<T, ast::StmtBreak>) {
                if (!inLoop) report(arg.loc, "break not within a loop");
            } else if constexpr (std::is_same_v<T, ast::StmtReturn>) {
                if (!inFunction) {
                    report(arg.loc, "return outside of function");
                    return;
                }
                if (arg.expr.has_value()) {
                    ExprInfo value = analyzeExpr(arg.expr->root);
                    if (isStruct(value.type)) {
                        report(arg.loc, "returning struct values is not supported");
                    }
                    if (!typeEqual(currentReturnType, value.type) &&
                        !(isPointer(currentReturnType) && value.isNullPtrConst)) {
                        report(arg.loc, "return type mismatch");
                    }
                } else {
                    if (currentReturnType.kind != Type::Kind::Void) {
                        report(arg.loc, "returning void in non-void function");
                    }
                }
            }
        }, stmt.node);
    }

    ExprInfo analyzeExpr(const Node::Ptr& node) {
        ExprInfo info;
        info.loc = locFromNode(node);
        if (!node) {
            info.type = makeError();
            return info;
        }

        switch (node->getType()) {
            case id: {
                std::string name = node->getToken()->getValue();
                auto sym = lookup(name);
                if (!sym) {
                    report(info.loc, "use of undeclared identifier '" + name + "'");
                    info.type = makeError();
                } else {
                    info.type = sym->type;
                    info.isLvalue = !isFunction(info.type);
                }
                return info;
            }
            case stringliteral:
                info.type = makePointer(makeChar());
                return info;
            case charconst:
                info.type = makeChar();
                return info;
            case decimalconst: {
                info.type = makeInt();
                std::string val = node->getToken()->getValue();
                if (val == "0") info.isNullPtrConst = true;
                return info;
            }
            case parenthesizedexpr:
                if (!node->getChildren().empty()) return analyzeExpr(node->getChildren().front());
                info.type = makeError();
                return info;
            case arrayaccess: {
                auto base = analyzeExpr(node->getChildren().at(0));
                auto idx = analyzeExpr(node->getChildren().at(1));
                if (!isPointer(base.type) && isPointer(idx.type)) {
                    std::swap(base, idx);
                }
                if (!isPointer(base.type)) report(info.loc, "array base is not a pointer");
                if (!isInteger(idx.type)) report(info.loc, "array index is not integer");
                if (isPointer(base.type)) {
                    info.type = *base.type.pointee;
                } else {
                    info.type = makeError();
                }
                info.isLvalue = true;
                return info;
            }
            case functioncall: {
                auto callee = analyzeExpr(node->getChildren().at(0));
                Type funcType = callee.type;
                if (isPointer(funcType) && isFunction(*funcType.pointee)) {
                    funcType = *funcType.pointee;
                }
                if (!isFunction(funcType)) {
                    report(info.loc, "call to non-function");
                    info.type = makeError();
                    return info;
                }
                if (node->getChildren().size() - 1 != funcType.params.size()) {
                    report(info.loc, "argument count mismatch");
                }
                for (size_t i = 1; i < node->getChildren().size(); ++i) {
                    ExprInfo arg = analyzeExpr(node->getChildren().at(i));
                    if (i - 1 < funcType.params.size()) {
                        const Type& paramType = funcType.params[i - 1];
                        if (!typeEqual(paramType, arg.type) &&
                            !(isPointer(paramType) && arg.isNullPtrConst)) {
                            report(info.loc, "argument type mismatch");
                        }
                    }
                }
                info.type = *funcType.returnType;
                return info;
            }
            case memberaccess: {
                auto base = analyzeExpr(node->getChildren().at(0));
                std::string fieldName = node->getChildren().at(1)->getToken()->getValue();
                if (!isStruct(base.type)) {
                    report(info.loc, "member access on non-struct");
                    info.type = makeError();
                    return info;
                }
                auto it = structs.find(base.type.structName);
                if (it == structs.end() || !it->second.defined) {
                    report(info.loc, "use of incomplete struct '" + base.type.structName + "'");
                    info.type = makeError();
                    return info;
                }
                auto field = it->second.fields.find(fieldName);
                if (field == it->second.fields.end()) {
                    report(info.loc, "unknown field '" + fieldName + "'");
                    info.type = makeError();
                    return info;
                }
                info.type = field->second;
                info.isLvalue = true;
                return info;
            }
            case pointermemberaccess: {
                auto base = analyzeExpr(node->getChildren().at(0));
                if (!isPointer(base.type) || !isStruct(*base.type.pointee)) {
                    report(info.loc, "pointer member access on non-struct pointer");
                    info.type = makeError();
                    return info;
                }
                std::string fieldName = node->getChildren().at(1)->getToken()->getValue();
                auto it = structs.find(base.type.pointee->structName);
                if (it == structs.end() || !it->second.defined) {
                    report(info.loc, "use of incomplete struct '" + base.type.pointee->structName + "'");
                    info.type = makeError();
                    return info;
                }
                auto field = it->second.fields.find(fieldName);
                if (field == it->second.fields.end()) {
                    report(info.loc, "unknown field '" + fieldName + "'");
                    info.type = makeError();
                    return info;
                }
                info.type = field->second;
                info.isLvalue = true;
                return info;
            }
            case reference: {
                auto operand = analyzeExpr(node->getChildren().at(0));
                if (!operand.isLvalue && !isFunction(operand.type)) {
                    report(info.loc, "cannot take address of rvalue");
                }
                info.type = makePointer(operand.type);
                return info;
            }
            case dereference: {
                auto operand = analyzeExpr(node->getChildren().at(0));
                if (!isPointer(operand.type)) {
                    report(info.loc, "dereference of non-pointer");
                    info.type = makeError();
                    return info;
                }
                info.type = *operand.type.pointee;
                info.isLvalue = !isFunction(info.type);
                return info;
            }
            case negationarithmetic: {
                auto operand = analyzeExpr(node->getChildren().at(0));
                if (!isInteger(operand.type)) {
                    report(info.loc, "arithmetic negation on non-integer");
                }
                info.type = makeInt();
                return info;
            }
            case negationlogical: {
                auto operand = analyzeExpr(node->getChildren().at(0));
                if (!isScalar(operand.type)) {
                    report(info.loc, "logical negation on non-scalar");
                }
                info.type = makeInt();
                return info;
            }
            case preincrement:
            case predecrement:
            case postincrement:
            case postdecrement: {
                auto operand = analyzeExpr(node->getChildren().at(0));
                if (!operand.isLvalue) {
                    report(info.loc, "increment/decrement of non-lvalue");
                }
                if (!isScalar(operand.type)) {
                    report(info.loc, "increment/decrement of non-scalar");
                }
                info.type = operand.type;
                info.isLvalue = (node->getType() == preincrement || node->getType() == predecrement);
                return info;
            }
            case sizeoperator: {
                if (!node->getChildren().empty() && node->getChildren().at(0)->getType() == type) {
                    checkTypeNode(node->getChildren().at(0));
                }
                info.type = makeInt();
                return info;
            }
            case product:
            case sum:
            case difference: {
                auto lhs = analyzeExpr(node->getChildren().at(0));
                auto rhs = analyzeExpr(node->getChildren().at(1));
                if (node->getType() == product) {
                    if (!isInteger(lhs.type) || !isInteger(rhs.type)) {
                        report(info.loc, "invalid operands to '*'");
                    }
                    info.type = makeInt();
                    return info;
                }

                if (isPointer(lhs.type) && isPointer(rhs.type)) {
                    if (node->getType() == sum) {
                        report(info.loc, "cannot add two pointers");
                        info.type = makeError();
                        return info;
                    }
                    if (!typeEqual(*lhs.type.pointee, *rhs.type.pointee)) {
                        report(info.loc, "pointer subtraction on incompatible types");
                    }
                    info.type = makeInt();
                    return info;
                }

                if (isPointer(lhs.type) && isInteger(rhs.type)) {
                    info.type = lhs.type;
                    return info;
                }
                if (isPointer(rhs.type) && isInteger(lhs.type) && node->getType() == sum) {
                    info.type = rhs.type;
                    return info;
                }
                if (!isInteger(lhs.type) || !isInteger(rhs.type)) {
                    report(info.loc, "invalid operands to arithmetic operator");
                }
                info.type = makeInt();
                return info;
            }
            case comparison: {
                auto lhs = analyzeExpr(node->getChildren().at(0));
                auto rhs = analyzeExpr(node->getChildren().at(1));
                if (!(isInteger(lhs.type) && isInteger(rhs.type)) &&
                    !(isPointer(lhs.type) && isPointer(rhs.type) && typeEqual(*lhs.type.pointee, *rhs.type.pointee))) {
                    report(info.loc, "invalid operands to '<'");
                }
                info.type = makeInt();
                return info;
            }
            case equality:
            case inequality: {
                auto lhs = analyzeExpr(node->getChildren().at(0));
                auto rhs = analyzeExpr(node->getChildren().at(1));
                bool okTypes = false;
                if (isInteger(lhs.type) && isInteger(rhs.type)) okTypes = true;
                if (isPointer(lhs.type) && isPointer(rhs.type) &&
                    typeEqual(*lhs.type.pointee, *rhs.type.pointee)) okTypes = true;
                if (isPointer(lhs.type) && rhs.isNullPtrConst) okTypes = true;
                if (isPointer(rhs.type) && lhs.isNullPtrConst) okTypes = true;
                if (!okTypes) report(info.loc, "invalid operands to equality operator");
                info.type = makeInt();
                return info;
            }
            case conjunction:
            case disjunction: {
                auto lhs = analyzeExpr(node->getChildren().at(0));
                auto rhs = analyzeExpr(node->getChildren().at(1));
                if (!isScalar(lhs.type) || !isScalar(rhs.type)) {
                    report(info.loc, "logical operator on non-scalar");
                }
                info.type = makeInt();
                return info;
            }
            case ternary: {
                auto cond = analyzeExpr(node->getChildren().at(0));
                auto tval = analyzeExpr(node->getChildren().at(1));
                auto fval = analyzeExpr(node->getChildren().at(2));
                if (!isScalar(cond.type)) report(info.loc, "ternary condition not scalar");
                if (typeEqual(tval.type, fval.type)) {
                    info.type = tval.type;
                    return info;
                }
                if (isPointer(tval.type) && fval.isNullPtrConst) {
                    info.type = tval.type;
                    return info;
                }
                if (isPointer(fval.type) && tval.isNullPtrConst) {
                    info.type = fval.type;
                    return info;
                }
                report(info.loc, "ternary operands have incompatible types");
                info.type = makeError();
                return info;
            }
            case assignment: {
                auto lhs = analyzeExpr(node->getChildren().at(0));
                auto rhs = analyzeExpr(node->getChildren().at(1));
                if (!lhs.isLvalue) {
                    report(info.loc, "assignment to non-lvalue");
                }
                if (isStruct(lhs.type) || isStruct(rhs.type)) {
                    report(info.loc, "assignments of struct type are not supported");
                }
                if (!typeEqual(lhs.type, rhs.type) &&
                    !(isPointer(lhs.type) && rhs.isNullPtrConst)) {
                    report(info.loc, "assignment type mismatch");
                }
                info.type = lhs.type;
                return info;
            }
            default:
                info.type = makeError();
                return info;
        }
    }

    void checkTypeNode(const Node::Ptr& typeNode) {
        if (!typeNode || typeNode->getType() != type || typeNode->getChildren().empty()) return;
        const auto& child = typeNode->getChildren().front();
        if (child->getType() != structtype) return;

        const auto& skids = child->getChildren();
        SourceLocation loc = locFromNode(skids.empty() ? child : skids.at(0));
        if (skids.size() < 2 || skids.at(1)->getType() != id) {
            report(loc, "anonymous structs are not supported");
            return;
        }
        std::string name = skids.at(1)->getToken()->getValue();
        auto it = structs.find(name);
        if (it == structs.end()) {
            StructInfo info;
            info.defined = false;
            info.loc = loc;
            it = structs.emplace(name, std::move(info)).first;
        }
        if (!it->second.defined) {
            report(loc, "use of incomplete struct '" + name + "'");
        }
    }
};

} // namespace

bool analyze(const ast::TranslationUnit& tu, std::ostream& err, const std::string& fileName) {
    Analyzer analyzer(err, fileName.empty() ? "unknown" : fileName);
    return analyzer.run(tu);
}

} // namespace semantic
