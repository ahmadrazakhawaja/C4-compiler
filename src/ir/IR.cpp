#include "IR.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include "../helper/Symbol.h"
#include "../helper/structs/Node.h"

namespace ir {

namespace {

struct TypeDesc {
    enum class Kind { Void, Char, Int, Struct, Pointer, Function, Error };
    Kind kind = Kind::Error;
    std::string structName;
    std::shared_ptr<TypeDesc> pointee;
    std::shared_ptr<TypeDesc> returnType;
    std::vector<TypeDesc> params;
};

static TypeDesc makeError() { return TypeDesc{TypeDesc::Kind::Error}; }
static TypeDesc makeVoid() { return TypeDesc{TypeDesc::Kind::Void}; }
static TypeDesc makeChar() { return TypeDesc{TypeDesc::Kind::Char}; }
static TypeDesc makeInt() { return TypeDesc{TypeDesc::Kind::Int}; }
static TypeDesc makeStruct(const std::string& name) {
    TypeDesc t;
    t.kind = TypeDesc::Kind::Struct;
    t.structName = name;
    return t;
}
static TypeDesc makePointer(const TypeDesc& base) {
    TypeDesc t;
    t.kind = TypeDesc::Kind::Pointer;
    t.pointee = std::make_shared<TypeDesc>(base);
    return t;
}
static TypeDesc makeFunction(const TypeDesc& ret, const std::vector<TypeDesc>& params) {
    TypeDesc t;
    t.kind = TypeDesc::Kind::Function;
    t.returnType = std::make_shared<TypeDesc>(ret);
    t.params = params;
    return t;
}

static bool isError(const TypeDesc& t) { return t.kind == TypeDesc::Kind::Error; }
static bool isStruct(const TypeDesc& t) { return t.kind == TypeDesc::Kind::Struct; }
static bool isPointer(const TypeDesc& t) { return t.kind == TypeDesc::Kind::Pointer; }
static bool isFunction(const TypeDesc& t) { return t.kind == TypeDesc::Kind::Function; }
static bool isVoid(const TypeDesc& t) { return t.kind == TypeDesc::Kind::Void; }
static bool isInteger(const TypeDesc& t) { return t.kind == TypeDesc::Kind::Int || t.kind == TypeDesc::Kind::Char; }

static bool typeEqual(const TypeDesc& a, const TypeDesc& b) {
    if (isError(a) || isError(b)) return true;
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case TypeDesc::Kind::Void:
        case TypeDesc::Kind::Char:
        case TypeDesc::Kind::Int:
            return true;
        case TypeDesc::Kind::Struct:
            return a.structName == b.structName;
        case TypeDesc::Kind::Pointer:
            return typeEqual(*a.pointee, *b.pointee);
        case TypeDesc::Kind::Function:
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

static bool valueCompatible(const TypeDesc& target, const TypeDesc& source) {
    if (isInteger(target) && isInteger(source)) return true;
    if (isPointer(target) && isFunction(source)) {
        return typeEqual(*target.pointee, source);
    }
    return typeEqual(target, source);
}

struct StructInfo {
    llvm::StructType* llvmType = nullptr;
    bool defined = false;
    std::vector<std::string> fieldNames;
    std::vector<TypeDesc> fieldTypes;
};

struct SymbolValue {
    TypeDesc type;
    llvm::Value* value = nullptr;
    bool isFunction = false;
};

struct ExprValue {
    llvm::Value* value = nullptr;
    TypeDesc type;
    llvm::Value* address = nullptr;
    bool isLvalue = false;
    bool isNullPtrConst = false;
};

struct LoopContext {
    llvm::BasicBlock* breakBlock = nullptr;
    llvm::BasicBlock* continueBlock = nullptr;
};

static bool isVoidOnlyParamList(const ast::ParamList& plist) {
    if (plist.isArray) return false;
    if (plist.params.size() != 1) return false;
    const auto& p = plist.params[0];
    if (p.declarator || p.abstractDeclarator) return false;
    return p.type.kind == ast::TypeSpec::Kind::Builtin && p.type.builtin == "void";
}

static bool extractDeclaratorName(const ast::Declarator& decl, std::string& name) {
    if (decl.direct.kind == ast::DirectDeclarator::Kind::Identifier) {
        name = decl.direct.identifier;
        return !name.empty();
    }
    if (decl.direct.nested) return extractDeclaratorName(*decl.direct.nested, name);
    return false;
}

static bool isVoidOnlyParamListDecl(const ast::ParamDecl& p) {
    return !p.declarator && !p.abstractDeclarator &&
        p.type.kind == ast::TypeSpec::Kind::Builtin && p.type.builtin == "void";
}

static void collectParamDecls(const ast::Declarator& decl, std::vector<const ast::ParamDecl*>& out) {
    if (!decl.direct.params.empty()) {
        const auto& plist = decl.direct.params.front();
        if (plist.params.size() == 1 && isVoidOnlyParamListDecl(plist.params[0])) {
            return;
        }
        for (const auto& param : plist.params) {
            out.push_back(&param);
        }
        return;
    }
    if (decl.direct.kind == ast::DirectDeclarator::Kind::Nested && decl.direct.nested) {
        collectParamDecls(*decl.direct.nested, out);
    }
}

static char decodeEscape(char c) {
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case 'b': return '\b';
        case 'f': return '\f';
        case 'v': return '\v';
        case 'a': return '\a';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        case '?': return '?';
        default: return c;
    }
}

static int parseCharLiteral(const std::string& text) {
    if (text.size() < 2) return 0;
    if (text.size() >= 4 && text[1] == '\\') {
        return static_cast<unsigned char>(decodeEscape(text[2]));
    }
    if (text.size() >= 3) {
        return static_cast<unsigned char>(text[1]);
    }
    return 0;
}

static std::string parseStringLiteral(const std::string& text) {
    std::string out;
    if (text.size() < 2) return out;
    for (size_t i = 1; i + 1 < text.size(); ++i) {
        char c = text[i];
        if (c == '\\' && i + 1 < text.size() - 1) {
            out.push_back(decodeEscape(text[i + 1]));
            ++i;
        } else {
            out.push_back(c);
        }
    }
    return out;
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

static void collectStructDecls(const Node::Ptr& node, std::vector<ast::Decl>& out);
static ast::TypeSpec buildTypeSpecFromNode(const Node::Ptr& node);
static ast::Declarator buildDeclaratorFromNode(const Node::Ptr& node);
static ast::AbstractDeclarator buildAbstractDeclaratorFromNode(const Node::Ptr& node);
static ast::ParamList buildParamListFromNode(const Node::Ptr& node);
static ast::ParamDecl buildParamDeclFromNode(const Node::Ptr& node);
static ast::Decl buildDeclFromDecNode(const Node::Ptr& node);

static void collectStructDecls(const Node::Ptr& node, std::vector<ast::Decl>& out) {
    if (!node || node->getChildren().empty()) return;
    out.push_back(buildDeclFromDecNode(node->getChildren().at(0)));
    collectStructDecls(node->getChildren().at(1), out);
}

static void collectParamListTail(const Node::Ptr& node, std::vector<ast::ParamDecl>& out) {
    if (!node || node->getChildren().empty()) return;
    out.push_back(buildParamDeclFromNode(node->getChildren().at(1)));
    collectParamListTail(node->getChildren().at(2), out);
}

static ast::ParamList buildArraySuffixFromNode(const Node::Ptr& node) {
    ast::ParamList list;
    list.isArray = true;
    const auto& kids = node->getChildren();
    if (kids.size() > 3 && kids.at(1)->getType() == expr && !kids.at(1)->getChildren().empty()) {
        list.arraySize = ast::Expr{kids.at(1)->getChildren().at(0)};
    }
    return list;
}

static void collectDirectDecSuffixes(const Node::Ptr& node, std::vector<ast::ParamList>& out) {
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

static void collectDirectAbstractSuffixes(const Node::Ptr& node, std::vector<ast::ParamList>& out) {
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

static ast::TypeSpec buildTypeSpecFromNode(const Node::Ptr& node) {
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

static ast::DirectDeclarator buildDirectDeclaratorFromNode(const Node::Ptr& node) {
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

static ast::Declarator buildDeclaratorFromNode(const Node::Ptr& node) {
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

static ast::DirectAbstractDeclarator buildDirectAbstractDeclaratorFromNode(const Node::Ptr& node) {
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

static ast::AbstractDeclarator buildAbstractDeclaratorFromNode(const Node::Ptr& node) {
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

static ast::ParamDecl buildParamDeclFromNode(const Node::Ptr& node) {
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

static ast::ParamList buildParamListFromNode(const Node::Ptr& node) {
    ast::ParamList list;
    const auto& kids = node->getChildren();
    list.params.push_back(buildParamDeclFromNode(kids.at(0)));
    if (kids.size() > 1) collectParamListTail(kids.at(1), list.params);
    return list;
}

static ast::Decl buildDeclFromDecNode(const Node::Ptr& node) {
    ast::Decl decl;
    const auto& kids = node->getChildren();
    decl.type = buildTypeSpecFromNode(kids.at(0));
    const auto& decTail = kids.at(1);
    if (decTail && decTail->getChildren().size() > 1) {
        decl.declarator = buildDeclaratorFromNode(decTail->getChildren().at(0));
    }
    return decl;
}

static Node::Ptr findFirstNodeOfType(const Node::Ptr& node, Symbol sym) {
    if (!node) return nullptr;
    if (node->getType() == sym) return node;
    for (const auto& child : node->getChildren()) {
        if (auto res = findFirstNodeOfType(child, sym)) return res;
    }
    return nullptr;
}

class Codegen {
public:
    Codegen(std::ostream& errStream, std::string moduleName)
        : err(errStream), module(std::make_unique<llvm::Module>(std::move(moduleName), context)), builder(context) {
        scopes.emplace_back();
    }

    bool run(const ast::TranslationUnit& tu, const std::string& inputPath) {
        module->setSourceFileName(inputPath);
        for (const auto& ext : tu.decls) {
            if (!codegenExternalDecl(ext)) return false;
        }
        return true;
    }

    bool writeToFile(const std::string& inputPath) {
        std::filesystem::path p(inputPath);
        std::string outName = p.stem().string() + ".ll";
        std::error_code ec;
        llvm::raw_fd_ostream stream(outName, ec);
        if (ec) {
            err << "error: cannot open output file " << outName << "\n";
            return false;
        }
        module->print(stream, nullptr);
        return true;
    }

private:
    std::ostream& err;
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> module;
    llvm::IRBuilder<> builder;

    std::vector<std::unordered_map<std::string, SymbolValue>> scopes;
    std::unordered_map<std::string, StructInfo> structs;
    llvm::Function* currentFunction = nullptr;
    TypeDesc currentReturnType;
    std::vector<LoopContext> loopStack;
    std::unordered_map<std::string, llvm::BasicBlock*> labelBlocks;

    void report(const std::string& msg) {
        err << "error: " << msg << "\n";
    }

    void pushScope() { scopes.emplace_back(); }
    void popScope() { scopes.pop_back(); }

    SymbolValue* lookup(const std::string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return &found->second;
        }
        return nullptr;
    }

    bool declare(const std::string& name, const SymbolValue& sym) {
        auto& scope = scopes.back();
        auto it = scope.find(name);
        if (it != scope.end()) {
            report("redeclaration of '" + name + "'");
            return false;
        }
        scope.emplace(name, sym);
        return true;
    }

    StructInfo& getStructInfo(const std::string& name) {
        auto it = structs.find(name);
        if (it != structs.end()) return it->second;
        StructInfo info;
        info.llvmType = llvm::StructType::create(context, name);
        auto res = structs.emplace(name, std::move(info));
        return res.first->second;
    }

    bool defineStruct(const ast::StructType& st) {
        if (!st.name.has_value()) {
            report("anonymous structs are not supported");
            return false;
        }
        const std::string& name = *st.name;
        StructInfo& info = getStructInfo(name);
        if (info.defined) return true;

        std::vector<llvm::Type*> fieldLLVM;
        info.fieldNames.clear();
        info.fieldTypes.clear();

        for (const auto& field : st.fields) {
            if (!field.declarator.has_value()) {
                continue;
            }
            std::string fname;
            if (!extractDeclaratorName(*field.declarator, fname)) {
                continue;
            }
            TypeDesc base = typeFromTypeSpec(field.type);
            TypeDesc ftype = applyDeclarator(base, *field.declarator);
            info.fieldNames.push_back(fname);
            info.fieldTypes.push_back(ftype);
            fieldLLVM.push_back(llvmType(ftype));
        }

        info.llvmType->setBody(fieldLLVM, false);
        info.defined = true;
        return true;
    }

    TypeDesc typeFromTypeSpec(const ast::TypeSpec& spec) {
        if (spec.kind == ast::TypeSpec::Kind::Builtin) {
            if (spec.builtin == "int") return makeInt();
            if (spec.builtin == "char") return makeChar();
            if (spec.builtin == "void") return makeVoid();
            return makeError();
        }
        if (!spec.structType.name.has_value()) {
            report("anonymous structs are not supported");
            return makeError();
        }
        const std::string& name = *spec.structType.name;
        if (!spec.structType.fields.empty()) {
            if (!defineStruct(spec.structType)) return makeError();
        } else {
            getStructInfo(name);
        }
        return makeStruct(name);
    }

    TypeDesc applySuffixes(const TypeDesc& base, const std::vector<ast::ParamList>& params) {
        TypeDesc current = base;
        for (const auto& plist : params) {
            if (plist.isArray) {
                current = makePointer(current);
                continue;
            }
            if (isVoidOnlyParamList(plist)) {
                current = makeFunction(current, {});
                continue;
            }
            std::vector<TypeDesc> paramTypes;
            for (const auto& param : plist.params) {
                TypeDesc paramType = typeFromTypeSpec(param.type);
                if (param.declarator) {
                    paramType = applyDeclarator(paramType, *param.declarator);
                } else if (param.abstractDeclarator) {
                    paramType = applyAbstractDeclarator(paramType, *param.abstractDeclarator);
                }
                if (paramType.kind == TypeDesc::Kind::Function) {
                    paramType = makePointer(paramType);
                }
                paramTypes.push_back(paramType);
            }
            current = makeFunction(current, paramTypes);
        }
        return current;
    }

    TypeDesc applyDeclarator(const TypeDesc& base, const ast::Declarator& decl) {
        TypeDesc current = base;
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

    TypeDesc applyAbstractDeclarator(const TypeDesc& base, const ast::AbstractDeclarator& decl) {
        TypeDesc current = base;
        for (int i = 0; i < decl.pointerDepth; ++i) {
            current = makePointer(current);
        }
        if (decl.hasDirect) {
            if (decl.direct.kind == ast::DirectAbstractDeclarator::Kind::ParamList) {
                std::vector<ast::ParamList> list;
                list.push_back(decl.direct.firstParamList);
                list.insert(list.end(), decl.direct.suffixes.begin(), decl.direct.suffixes.end());
                current = applySuffixes(current, list);
            } else if (!decl.direct.suffixes.empty()) {
                current = applySuffixes(current, decl.direct.suffixes);
            }
            if (decl.direct.kind == ast::DirectAbstractDeclarator::Kind::Nested && decl.direct.nested) {
                current = applyAbstractDeclarator(current, *decl.direct.nested);
            }
        }
        return current;
    }

    TypeDesc typeFromTypeNode(const Node::Ptr& typeNode) {
        if (!typeNode || typeNode->getType() != type) return makeError();
        ast::TypeSpec ts = buildTypeSpecFromNode(typeNode);
        TypeDesc base = typeFromTypeSpec(ts);
        Node::Ptr adNode = findFirstNodeOfType(typeNode, abstractdeclarator);
        if (adNode) {
            ast::AbstractDeclarator ad = buildAbstractDeclaratorFromNode(adNode);
            return applyAbstractDeclarator(base, ad);
        }
        return base;
    }

    llvm::Type* llvmType(const TypeDesc& t) {
        switch (t.kind) {
            case TypeDesc::Kind::Void:
                return builder.getVoidTy();
            case TypeDesc::Kind::Char:
                return builder.getInt8Ty();
            case TypeDesc::Kind::Int:
                return builder.getInt32Ty();
            case TypeDesc::Kind::Struct: {
                StructInfo& info = getStructInfo(t.structName);
                return info.llvmType;
            }
            case TypeDesc::Kind::Pointer:
                return llvm::PointerType::getUnqual(context);
            case TypeDesc::Kind::Function: {
                std::vector<llvm::Type*> args;
                args.reserve(t.params.size());
                for (const auto& p : t.params) {
                    args.push_back(llvmType(p));
                }
                return llvm::FunctionType::get(llvmType(*t.returnType), args, false);
            }
            default:
                return builder.getInt32Ty();
        }
    }

    llvm::Type* llvmValueType(const TypeDesc& t) {
        if (t.kind == TypeDesc::Kind::Function) {
            return llvm::PointerType::getUnqual(context);
        }
        return llvmType(t);
    }

    bool codegenExternalDecl(const ast::ExternalDecl& ext) {
        return std::visit([&](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, ast::Decl>) {
                return codegenDecl(arg, true);
            } else if constexpr (std::is_same_v<T, ast::FuncDef>) {
                return codegenFunction(arg);
            }
            return false;
        }, ext.node);
    }

    bool codegenDecl(const ast::Decl& decl, bool isGlobal) {
        TypeDesc base = typeFromTypeSpec(decl.type);
        if (!decl.declarator.has_value()) {
            if (decl.type.kind == ast::TypeSpec::Kind::Struct && !decl.type.structType.fields.empty()) {
                return defineStruct(decl.type.structType);
            }
            return true;
        }

        std::string name;
        if (!extractDeclaratorName(*decl.declarator, name)) {
            report("missing declarator name");
            return false;
        }

        TypeDesc full = applyDeclarator(base, *decl.declarator);

        if (full.kind == TypeDesc::Kind::Function) {
            auto* fty = llvm::cast<llvm::FunctionType>(llvmType(full));
            llvm::Function* fn = module->getFunction(name);
            if (!fn) {
                fn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, name, module.get());
            } else if (fn->getFunctionType() != fty) {
                report("conflicting types for function '" + name + "'");
                return false;
            }
            auto existing = scopes.back().find(name);
            if (existing != scopes.back().end()) {
                if (!existing->second.isFunction || !typeEqual(existing->second.type, full)) {
                    report("redeclaration of '" + name + "'");
                    return false;
                }
                existing->second.value = fn;
                existing->second.type = full;
                return true;
            }
            SymbolValue sym;
            sym.type = full;
            sym.value = fn;
            sym.isFunction = true;
            scopes.back().emplace(name, sym);
            return true;
        }

        llvm::Type* objTy = llvmType(full);
        if (isGlobal) {
            llvm::Constant* init = llvm::Constant::getNullValue(objTy);
            auto* gv = new llvm::GlobalVariable(*module, objTy, false,
                                                llvm::GlobalValue::ExternalLinkage,
                                                init, name);
            SymbolValue sym;
            sym.type = full;
            sym.value = gv;
            sym.isFunction = false;
            return declare(name, sym);
        }

        llvm::IRBuilder<> allocaBuilder(&currentFunction->getEntryBlock(),
                                        currentFunction->getEntryBlock().begin());
        llvm::AllocaInst* alloca = allocaBuilder.CreateAlloca(objTy, nullptr, name);
        SymbolValue sym;
        sym.type = full;
        sym.value = alloca;
        sym.isFunction = false;
        return declare(name, sym);
    }

    void collectLabels(const ast::StmtCompound& compound) {
        for (const auto& item : compound.items) {
            collectLabels(*item);
        }
    }

    void collectLabels(const ast::Statement& stmt) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, ast::StmtCompound>) {
                collectLabels(arg);
            } else if constexpr (std::is_same_v<T, ast::StmtIf>) {
                collectLabels(*arg.thenStmt);
                if (arg.elseStmt) collectLabels(*arg.elseStmt);
            } else if constexpr (std::is_same_v<T, ast::StmtWhile>) {
                collectLabels(*arg.body);
            } else if constexpr (std::is_same_v<T, ast::StmtLabel>) {
                labelBlocks[arg.label] = llvm::BasicBlock::Create(context, arg.label, currentFunction);
                collectLabels(*arg.stmt);
            }
        }, stmt.node);
    }

    void ensureBlock() {
        llvm::BasicBlock* block = builder.GetInsertBlock();
        if (!block || block->getTerminator()) {
            llvm::BasicBlock* next = llvm::BasicBlock::Create(context, "cont", currentFunction);
            builder.SetInsertPoint(next);
        }
    }

    bool codegenFunction(const ast::FuncDef& func) {
        TypeDesc base = typeFromTypeSpec(func.type);
        TypeDesc full = applyDeclarator(base, func.declarator);
        if (full.kind != TypeDesc::Kind::Function) {
            report("function declarator does not yield a function type");
            return false;
        }

        std::string name;
        if (!extractDeclaratorName(func.declarator, name)) {
            report("missing function name");
            return false;
        }

        auto* fty = llvm::cast<llvm::FunctionType>(llvmType(full));
        llvm::Function* fn = module->getFunction(name);
        if (!fn) {
            fn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, name, module.get());
        } else if (fn->getFunctionType() != fty) {
            report("conflicting types for function '" + name + "'");
            return false;
        } else if (!fn->empty()) {
            report("redefinition of function '" + name + "'");
            return false;
        }

        auto globalIt = scopes.front().find(name);
        if (globalIt == scopes.front().end()) {
            SymbolValue sym;
            sym.type = full;
            sym.value = fn;
            sym.isFunction = true;
            scopes.front().emplace(name, sym);
        } else {
            if (!globalIt->second.isFunction || !typeEqual(globalIt->second.type, full)) {
                report("conflicting types for function '" + name + "'");
                return false;
            }
            globalIt->second.value = fn;
            globalIt->second.type = full;
        }

        currentFunction = fn;
        currentReturnType = *full.returnType;
        labelBlocks.clear();

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", fn);
        builder.SetInsertPoint(entry);

        pushScope();
        {
            std::vector<const ast::ParamDecl*> params;
            collectParamDecls(func.declarator, params);

            size_t idx = 0;
            for (auto& arg : fn->args()) {
                std::string paramName = "arg" + std::to_string(idx);
                if (idx < params.size()) {
                    if (params[idx]->declarator) {
                        std::string pname;
                        if (extractDeclaratorName(*params[idx]->declarator, pname)) {
                            paramName = pname;
                        }
                    }
                }
                arg.setName(paramName);

                llvm::IRBuilder<> allocaBuilder(&fn->getEntryBlock(), fn->getEntryBlock().begin());
                llvm::Type* objTy = arg.getType();
                llvm::AllocaInst* alloca = allocaBuilder.CreateAlloca(objTy, nullptr, paramName);
                builder.CreateStore(&arg, alloca);

                SymbolValue sym;
                if (idx < full.params.size()) {
                    sym.type = full.params[idx];
                } else {
                    sym.type = makeInt();
                }
                sym.value = alloca;
                sym.isFunction = false;
                declare(paramName, sym);
                idx++;
            }
        }

        collectLabels(func.body);
        if (!codegenCompound(func.body)) {
            popScope();
            return false;
        }

        if (!builder.GetInsertBlock()->getTerminator()) {
            if (currentReturnType.kind == TypeDesc::Kind::Void) {
                builder.CreateRetVoid();
            } else if (currentReturnType.kind == TypeDesc::Kind::Pointer) {
                builder.CreateRet(llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(llvmValueType(currentReturnType))));
            } else if (currentReturnType.kind == TypeDesc::Kind::Char) {
                builder.CreateRet(llvm::ConstantInt::get(builder.getInt8Ty(), 0));
            } else {
                builder.CreateRet(llvm::ConstantInt::get(builder.getInt32Ty(), 0));
            }
        }

        popScope();
        currentFunction = nullptr;
        return true;
    }

    bool codegenCompound(const ast::StmtCompound& compound) {
        pushScope();
        for (const auto& item : compound.items) {
            if (!codegenStatement(*item)) {
                popScope();
                return false;
            }
        }
        popScope();
        return true;
    }

    bool codegenStatement(const ast::Statement& stmt) {
        ensureBlock();
        return std::visit([&](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, ast::StmtCompound>) {
                return codegenCompound(arg);
            } else if constexpr (std::is_same_v<T, ast::StmtDecl>) {
                return codegenDecl(arg.decl, false);
            } else if constexpr (std::is_same_v<T, ast::StmtExpr>) {
                return codegenStmtExpr(arg);
            } else if constexpr (std::is_same_v<T, ast::StmtIf>) {
                return codegenIf(arg);
            } else if constexpr (std::is_same_v<T, ast::StmtWhile>) {
                return codegenWhile(arg);
            } else if constexpr (std::is_same_v<T, ast::StmtLabel>) {
                return codegenLabel(arg);
            } else if constexpr (std::is_same_v<T, ast::StmtGoto>) {
                return codegenGoto(arg);
            } else if constexpr (std::is_same_v<T, ast::StmtContinue>) {
                return codegenContinue(arg);
            } else if constexpr (std::is_same_v<T, ast::StmtBreak>) {
                return codegenBreak(arg);
            } else if constexpr (std::is_same_v<T, ast::StmtReturn>) {
                return codegenReturn(arg);
            }
            return true;
        }, stmt.node);
    }

    llvm::Value* castToInt(llvm::Value* value, const TypeDesc& type) {
        if (!value) return nullptr;
        llvm::Type* targetTy = builder.getInt32Ty();
        if (value->getType() == targetTy) return value;
        if (type.kind == TypeDesc::Kind::Char && value->getType()->isIntegerTy(8)) {
            return builder.CreateSExt(value, targetTy, "sext");
        }
        if (value->getType()->isIntegerTy(1)) {
            return builder.CreateZExt(value, targetTy, "zext");
        }
        if (value->getType()->isIntegerTy()) {
            auto srcBits = llvm::cast<llvm::IntegerType>(value->getType())->getBitWidth();
            if (srcBits < 32) return builder.CreateSExt(value, targetTy, "sext");
            if (srcBits > 32) return builder.CreateTrunc(value, targetTy, "trunc");
            return value;
        }
        return value;
    }

    llvm::Value* castToBool(llvm::Value* value, const TypeDesc& type) {
        if (!value) return nullptr;
        if (isPointer(type)) {
            if (!value->getType()->isPointerTy()) {
                llvm::Value* intVal = castToInt(value, type);
                return builder.CreateICmpNE(intVal, llvm::ConstantInt::get(builder.getInt32Ty(), 0), "inttobool");
            }
            return builder.CreateICmpNE(value,
                llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(value->getType())),
                "ptrtobool");
        }
        if (value->getType()->isIntegerTy(1)) return value;
        llvm::Value* intVal = castToInt(value, type);
        return builder.CreateICmpNE(intVal, llvm::ConstantInt::get(builder.getInt32Ty(), 0), "inttobool");
    }

    llvm::Value* castToIndex(llvm::Value* value, const TypeDesc& type) {
        llvm::Value* intVal = castToInt(value, type);
        if (!intVal) return nullptr;
        if (intVal->getType()->isIntegerTy(64)) return intVal;
        return builder.CreateSExt(intVal, builder.getInt64Ty(), "idxext");
    }

    llvm::Value* coerceValue(llvm::Value* value, const TypeDesc& from, const TypeDesc& to, bool isNullPtrConst) {
        if (!value) return nullptr;
        if (typeEqual(from, to)) return value;
        if (isInteger(to) && isInteger(from)) {
            if (to.kind == TypeDesc::Kind::Char) {
                if (value->getType()->isIntegerTy(8)) return value;
                if (value->getType()->isIntegerTy()) {
                    return builder.CreateIntCast(value, builder.getInt8Ty(), true, "int8cast");
                }
                return value;
            }
            return castToInt(value, from);
        }
        if (isPointer(to)) {
            llvm::Type* targetTy = llvmValueType(to);
            if (isNullPtrConst) {
                return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(targetTy));
            }
            if (value->getType()->isPointerTy()) {
                return builder.CreateBitCast(value, targetTy, "ptrcast");
            }
            if (value->getType()->isIntegerTy()) {
                return builder.CreateIntToPtr(value, targetTy, "inttoptr");
            }
        }
        return value;
    }

    ExprValue makeLValue(llvm::Value* addr, const TypeDesc& type) {
        ExprValue ev;
        if (!addr) {
            ev.type = makeError();
            return ev;
        }
        ev.address = addr;
        ev.type = type;
        ev.isLvalue = true;
        if (!isVoid(type)) {
            ev.value = builder.CreateLoad(llvmType(type), addr, "loadtmp");
        }
        return ev;
    }

    llvm::Value* createStringLiteral(const std::string& value) {
        llvm::Constant* data = llvm::ConstantDataArray::getString(context, value, true);
        auto* gv = new llvm::GlobalVariable(*module, data->getType(), true,
                                            llvm::GlobalValue::PrivateLinkage,
                                            data, "str");
        gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        llvm::Constant* zero = llvm::ConstantInt::get(builder.getInt32Ty(), 0);
        llvm::Constant* indices[] = {zero, zero};
        return llvm::ConstantExpr::getInBoundsGetElementPtr(data->getType(), gv, indices);
    }

    llvm::Value* sizeOfType(const TypeDesc& type) {
        if (type.kind == TypeDesc::Kind::Void || type.kind == TypeDesc::Kind::Function) {
            return llvm::ConstantInt::get(builder.getInt64Ty(), 0);
        }
        llvm::Type* objTy = llvmType(type);
        llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(context));
        llvm::Constant* idx = llvm::ConstantInt::get(builder.getInt32Ty(), 1);
        llvm::Constant* gep = llvm::ConstantExpr::getGetElementPtr(objTy, nullPtr, {idx});
        return llvm::ConstantExpr::getPtrToInt(gep, builder.getInt64Ty());
    }

    ExprValue codegenExpr(const Node::Ptr& node) {
        ExprValue ev;
        if (!node) {
            ev.type = makeError();
            return ev;
        }

        switch (node->getType()) {
            case id: {
                std::string name = node->getToken()->getValue();
                auto* sym = lookup(name);
                if (!sym) {
                    report("use of undeclared identifier '" + name + "'");
                    ev.type = makeError();
                    return ev;
                }
                ev.type = sym->type;
                if (sym->isFunction) {
                    ev.value = sym->value;
                    ev.isLvalue = false;
                    return ev;
                }
                ev.address = sym->value;
                ev.isLvalue = true;
                ev.value = builder.CreateLoad(llvmType(sym->type), sym->value, name + "_val");
                return ev;
            }
            case stringliteral: {
                std::string raw = node->getToken()->getValue();
                std::string parsed = parseStringLiteral(raw);
                llvm::Value* str = createStringLiteral(parsed);
                ev.value = str;
                ev.type = makePointer(makeChar());
                return ev;
            }
            case charconst: {
                int v = parseCharLiteral(node->getToken()->getValue());
                ev.value = llvm::ConstantInt::get(builder.getInt32Ty(), v);
                ev.type = makeInt();
                return ev;
            }
            case decimalconst: {
                long long v = 0;
                try {
                    v = std::stoll(node->getToken()->getValue());
                } catch (...) {
                    v = 0;
                }
                ev.value = llvm::ConstantInt::get(builder.getInt32Ty(), v);
                ev.type = makeInt();
                ev.isNullPtrConst = (v == 0);
                return ev;
            }
            case parenthesizedexpr:
                if (!node->getChildren().empty()) return codegenExpr(node->getChildren().front());
                ev.type = makeError();
                return ev;
            case arrayaccess: {
                const auto& kids = node->getChildren();
                ExprValue base = codegenExpr(kids.at(0));
                ExprValue idx = codegenExpr(kids.at(1));
                if (!base.value || !idx.value) {
                    ev.type = makeError();
                    return ev;
                }
                if (!isPointer(base.type) && isPointer(idx.type)) {
                    std::swap(base, idx);
                }
                if (!isPointer(base.type)) {
                    report("array base is not a pointer");
                    ev.type = makeError();
                    return ev;
                }
                TypeDesc elemType = *base.type.pointee;
                llvm::Value* indexVal = castToIndex(idx.value, idx.type);
                llvm::Type* gepElemTy = (elemType.kind == TypeDesc::Kind::Void || elemType.kind == TypeDesc::Kind::Function)
                    ? builder.getInt8Ty()
                    : llvmType(elemType);
                llvm::Value* addr = builder.CreateGEP(gepElemTy, base.value, indexVal, "elemaddr");
                return makeLValue(addr, elemType);
            }
            case functioncall: {
                const auto& kids = node->getChildren();
                ExprValue callee = codegenExpr(kids.at(0));
                TypeDesc funcType = callee.type;
                llvm::Value* calleeVal = callee.value;
                if (!calleeVal) {
                    ev.type = makeError();
                    return ev;
                }

                if (isPointer(funcType) && isFunction(*funcType.pointee)) {
                    funcType = *funcType.pointee;
                } else if (!isFunction(funcType)) {
                    report("call to non-function");
                    ev.type = makeError();
                    return ev;
                }

                auto* fty = llvm::cast<llvm::FunctionType>(llvmType(funcType));
                if (funcType.params.size() != fty->getNumParams()) {
                    report("internal error: function parameter mismatch");
                    ev.type = makeError();
                    return ev;
                }
                std::vector<llvm::Value*> args;
                for (size_t i = 1; i < kids.size(); ++i) {
                    ExprValue arg = codegenExpr(kids.at(i));
                    if (!arg.value) {
                        ev.type = makeError();
                        return ev;
                    }
                    if (i - 1 < funcType.params.size()) {
                        llvm::Value* coerced = coerceValue(arg.value, arg.type, funcType.params[i - 1],
                                                           arg.isNullPtrConst);
                        if (!coerced || coerced->getType()->isVoidTy()) {
                            report("invalid function argument");
                            ev.type = makeError();
                            return ev;
                        }
                        args.push_back(coerced);
                    } else {
                        args.push_back(arg.value);
                    }
                }
                if (args.size() != fty->getNumParams()) {
                    report("argument count mismatch");
                    ev.type = makeError();
                    return ev;
                }

                llvm::CallInst* call = builder.CreateCall(fty, calleeVal, args,
                                                          fty->getReturnType()->isVoidTy() ? "" : "calltmp");
                ev.type = *funcType.returnType;
                ev.value = call;
                return ev;
            }
            case memberaccess: {
                const auto& kids = node->getChildren();
                TypeDesc baseType;
                llvm::Value* baseAddr = codegenLValue(kids.at(0), baseType);
                if (!baseAddr) {
                    ev.type = makeError();
                    return ev;
                }
                if (!isStruct(baseType)) {
                    report("member access on non-struct");
                    ev.type = makeError();
                    return ev;
                }
                std::string fieldName = kids.at(1)->getToken()->getValue();
                StructInfo& info = getStructInfo(baseType.structName);
                auto it = std::find(info.fieldNames.begin(), info.fieldNames.end(), fieldName);
                if (it == info.fieldNames.end()) {
                    report("unknown field '" + fieldName + "'");
                    ev.type = makeError();
                    return ev;
                }
                size_t index = static_cast<size_t>(std::distance(info.fieldNames.begin(), it));
                TypeDesc fieldType = info.fieldTypes.at(index);
                llvm::Value* addr = builder.CreateStructGEP(llvmType(baseType), baseAddr,
                                                           static_cast<unsigned>(index), "fieldaddr");
                return makeLValue(addr, fieldType);
            }
            case pointermemberaccess: {
                const auto& kids = node->getChildren();
                ExprValue base = codegenExpr(kids.at(0));
                if (!base.value) {
                    ev.type = makeError();
                    return ev;
                }
                if (!isPointer(base.type) || !isStruct(*base.type.pointee)) {
                    report("pointer member access on non-struct pointer");
                    ev.type = makeError();
                    return ev;
                }
                TypeDesc structType = *base.type.pointee;
                std::string fieldName = kids.at(1)->getToken()->getValue();
                StructInfo& info = getStructInfo(structType.structName);
                auto it = std::find(info.fieldNames.begin(), info.fieldNames.end(), fieldName);
                if (it == info.fieldNames.end()) {
                    report("unknown field '" + fieldName + "'");
                    ev.type = makeError();
                    return ev;
                }
                size_t index = static_cast<size_t>(std::distance(info.fieldNames.begin(), it));
                TypeDesc fieldType = info.fieldTypes.at(index);
                llvm::Value* addr = builder.CreateStructGEP(llvmType(structType), base.value,
                                                           static_cast<unsigned>(index), "fieldaddr");
                return makeLValue(addr, fieldType);
            }
            case reference: {
                const auto& child = node->getChildren().at(0);
                if (child->getType() == id) {
                    std::string name = child->getToken()->getValue();
                    auto* sym = lookup(name);
                    if (sym && sym->isFunction) {
                        ev.type = makePointer(sym->type);
                        ev.value = sym->value;
                        return ev;
                    }
                }
                TypeDesc ltype;
                llvm::Value* addr = codegenLValue(child, ltype);
                if (!addr) {
                    ev.type = makeError();
                    return ev;
                }
                ev.type = makePointer(ltype);
                ev.value = addr;
                return ev;
            }
            case dereference: {
                ExprValue operand = codegenExpr(node->getChildren().at(0));
                if (!operand.value) {
                    ev.type = makeError();
                    return ev;
                }
                if (!isPointer(operand.type)) {
                    report("dereference of non-pointer");
                    ev.type = makeError();
                    return ev;
                }
                TypeDesc pointee = *operand.type.pointee;
                if (pointee.kind == TypeDesc::Kind::Function) {
                    ev.type = pointee;
                    ev.value = operand.value;
                    return ev;
                }
                llvm::Value* addr = operand.value;
                return makeLValue(addr, pointee);
            }
            case negationarithmetic: {
                ExprValue operand = codegenExpr(node->getChildren().at(0));
                llvm::Value* val = castToInt(operand.value, operand.type);
                ev.value = builder.CreateNeg(val, "negtmp");
                ev.type = makeInt();
                return ev;
            }
            case negationlogical: {
                ExprValue operand = codegenExpr(node->getChildren().at(0));
                llvm::Value* cond = castToBool(operand.value, operand.type);
                llvm::Value* notv = builder.CreateNot(cond, "lnot");
                ev.value = builder.CreateZExt(notv, builder.getInt32Ty(), "lnotext");
                ev.type = makeInt();
                return ev;
            }
            case preincrement:
            case predecrement:
            case postincrement:
            case postdecrement: {
                bool isPost = (node->getType() == postincrement || node->getType() == postdecrement);
                int delta = (node->getType() == preincrement || node->getType() == postincrement) ? 1 : -1;
                TypeDesc ltype;
                llvm::Value* addr = codegenLValue(node->getChildren().at(0), ltype);
                if (!addr) {
                    ev.type = makeError();
                    return ev;
                }
                if (isPointer(ltype)) {
                    llvm::Value* oldVal = builder.CreateLoad(llvmValueType(ltype), addr, "oldptr");
                    llvm::Value* idxVal = llvm::ConstantInt::get(builder.getInt64Ty(), delta);
                    llvm::Type* gepElemTy =
                        (ltype.pointee->kind == TypeDesc::Kind::Void || ltype.pointee->kind == TypeDesc::Kind::Function)
                            ? builder.getInt8Ty()
                            : llvmType(*ltype.pointee);
                    llvm::Value* newVal = builder.CreateGEP(gepElemTy, oldVal, idxVal, "ptrinc");
                    builder.CreateStore(newVal, addr);
                    ev.type = ltype;
                    ev.value = isPost ? oldVal : newVal;
                    return ev;
                }
                llvm::Value* oldVal = builder.CreateLoad(llvmType(ltype), addr, "oldval");
                llvm::Value* one = llvm::ConstantInt::get(oldVal->getType(), 1);
                llvm::Value* newVal = delta > 0 ? builder.CreateAdd(oldVal, one, "inc")
                                                : builder.CreateSub(oldVal, one, "dec");
                builder.CreateStore(newVal, addr);
                ev.type = ltype;
                ev.value = isPost ? oldVal : newVal;
                return ev;
            }
            case sizeoperator: {
                if (!node->getChildren().empty() && node->getChildren().at(0)->getType() == type) {
                    TypeDesc tdesc = typeFromTypeNode(node->getChildren().at(0));
                    llvm::Value* sz = sizeOfType(tdesc);
                    ev.value = builder.CreateTrunc(sz, builder.getInt32Ty(), "sizeof");
                    ev.type = makeInt();
                    return ev;
                }
                ExprValue operand = codegenExpr(node->getChildren().at(0));
                llvm::Value* sz = sizeOfType(operand.type);
                ev.value = builder.CreateTrunc(sz, builder.getInt32Ty(), "sizeof");
                ev.type = makeInt();
                return ev;
            }
            case product: {
                const auto& kids = node->getChildren();
                std::string op = node->getToken()->getValue();
                ExprValue lhs = codegenExpr(kids.at(0));
                ExprValue rhs = codegenExpr(kids.at(1));
                llvm::Value* lval = castToInt(lhs.value, lhs.type);
                llvm::Value* rval = castToInt(rhs.value, rhs.type);
                if (op == "/") {
                    ev.value = builder.CreateSDiv(lval, rval, "divtmp");
                } else if (op == "%") {
                    ev.value = builder.CreateSRem(lval, rval, "modtmp");
                } else {
                    ev.value = builder.CreateMul(lval, rval, "multmp");
                }
                ev.type = makeInt();
                return ev;
            }
            case sum:
            case difference: {
                const auto& kids = node->getChildren();
                ExprValue lhs = codegenExpr(kids.at(0));
                ExprValue rhs = codegenExpr(kids.at(1));
                bool isSub = (node->getType() == difference);

                if (isPointer(lhs.type) && isInteger(rhs.type)) {
                    llvm::Value* idx = castToIndex(rhs.value, rhs.type);
                    if (isSub) idx = builder.CreateNeg(idx, "idxneg");
                    llvm::Type* gepElemTy =
                        (lhs.type.pointee->kind == TypeDesc::Kind::Void || lhs.type.pointee->kind == TypeDesc::Kind::Function)
                            ? builder.getInt8Ty()
                            : llvmType(*lhs.type.pointee);
                    ev.value = builder.CreateGEP(gepElemTy, lhs.value, idx, "ptrarith");
                    ev.type = lhs.type;
                    return ev;
                }
                if (!isSub && isPointer(rhs.type) && isInteger(lhs.type)) {
                    llvm::Value* idx = castToIndex(lhs.value, lhs.type);
                    llvm::Type* gepElemTy =
                        (rhs.type.pointee->kind == TypeDesc::Kind::Void || rhs.type.pointee->kind == TypeDesc::Kind::Function)
                            ? builder.getInt8Ty()
                            : llvmType(*rhs.type.pointee);
                    ev.value = builder.CreateGEP(gepElemTy, rhs.value, idx, "ptrarith");
                    ev.type = rhs.type;
                    return ev;
                }
                if (isPointer(lhs.type) && isPointer(rhs.type) && isSub) {
                    llvm::Value* lptr = builder.CreatePtrToInt(lhs.value, builder.getInt64Ty(), "lptr");
                    llvm::Value* rptr = builder.CreatePtrToInt(rhs.value, builder.getInt64Ty(), "rptr");
                    llvm::Value* diff = builder.CreateSub(lptr, rptr, "ptrdiff");
                    llvm::Value* elemSize =
                        (lhs.type.pointee->kind == TypeDesc::Kind::Void || lhs.type.pointee->kind == TypeDesc::Kind::Function)
                            ? llvm::ConstantInt::get(builder.getInt64Ty(), 1)
                            : sizeOfType(*lhs.type.pointee);
                    llvm::Value* div = builder.CreateSDiv(diff, elemSize, "ptrdiv");
                    ev.value = builder.CreateTrunc(div, builder.getInt32Ty(), "ptrdiff32");
                    ev.type = makeInt();
                    return ev;
                }
                llvm::Value* lval = castToInt(lhs.value, lhs.type);
                llvm::Value* rval = castToInt(rhs.value, rhs.type);
                ev.value = isSub ? builder.CreateSub(lval, rval, "subtmp")
                                 : builder.CreateAdd(lval, rval, "addtmp");
                ev.type = makeInt();
                return ev;
            }
            case comparison: {
                const auto& kids = node->getChildren();
                std::string op = node->getToken()->getValue();
                ExprValue lhs = codegenExpr(kids.at(0));
                ExprValue rhs = codegenExpr(kids.at(1));
                llvm::Value* lval = nullptr;
                llvm::Value* rval = nullptr;
                bool ptrCompare = isPointer(lhs.type) && isPointer(rhs.type);
                if (ptrCompare) {
                    lval = builder.CreatePtrToInt(lhs.value, builder.getInt64Ty(), "lptr");
                    rval = builder.CreatePtrToInt(rhs.value, builder.getInt64Ty(), "rptr");
                } else {
                    lval = castToInt(lhs.value, lhs.type);
                    rval = castToInt(rhs.value, rhs.type);
                }
                llvm::Value* cmp = nullptr;
                if (op == "<") cmp = builder.CreateICmpSLT(lval, rval, "cmplt");
                else if (op == "<=") cmp = builder.CreateICmpSLE(lval, rval, "cmple");
                else if (op == ">") cmp = builder.CreateICmpSGT(lval, rval, "cmpgt");
                else if (op == ">=") cmp = builder.CreateICmpSGE(lval, rval, "cmpge");
                else cmp = builder.CreateICmpSLT(lval, rval, "cmplt");
                ev.value = builder.CreateZExt(cmp, builder.getInt32Ty(), "cmpext");
                ev.type = makeInt();
                return ev;
            }
            case equality:
            case inequality: {
                const auto& kids = node->getChildren();
                ExprValue lhs = codegenExpr(kids.at(0));
                ExprValue rhs = codegenExpr(kids.at(1));
                bool isEq = (node->getType() == equality);
                llvm::Value* cmp = nullptr;
                if (isPointer(lhs.type) || isPointer(rhs.type)) {
                    TypeDesc ptrType = isPointer(lhs.type) ? lhs.type : rhs.type;
                    llvm::Value* lval = coerceValue(lhs.value, lhs.type, ptrType, lhs.isNullPtrConst);
                    llvm::Value* rval = coerceValue(rhs.value, rhs.type, ptrType, rhs.isNullPtrConst);
                    cmp = isEq ? builder.CreateICmpEQ(lval, rval, "cmpeq")
                               : builder.CreateICmpNE(lval, rval, "cmpne");
                } else {
                    llvm::Value* lval = castToInt(lhs.value, lhs.type);
                    llvm::Value* rval = castToInt(rhs.value, rhs.type);
                    cmp = isEq ? builder.CreateICmpEQ(lval, rval, "cmpeq")
                               : builder.CreateICmpNE(lval, rval, "cmpne");
                }
                ev.value = builder.CreateZExt(cmp, builder.getInt32Ty(), "cmpext");
                ev.type = makeInt();
                return ev;
            }
            case conjunction:
            case disjunction: {
                const auto& kids = node->getChildren();
                ExprValue lhs = codegenExpr(kids.at(0));
                llvm::Value* lhsBool = castToBool(lhs.value, lhs.type);
                if (!lhsBool) {
                    ev.type = makeError();
                    return ev;
                }
                llvm::BasicBlock* lhsBlock = builder.GetInsertBlock();
                llvm::BasicBlock* rhsBlock = llvm::BasicBlock::Create(context, "logic.rhs", currentFunction);
                llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(context, "logic.end", currentFunction);
                if (node->getType() == conjunction) {
                    builder.CreateCondBr(lhsBool, rhsBlock, endBlock);
                } else {
                    builder.CreateCondBr(lhsBool, endBlock, rhsBlock);
                }

                builder.SetInsertPoint(rhsBlock);
                ExprValue rhs = codegenExpr(kids.at(1));
                llvm::Value* rhsBool = castToBool(rhs.value, rhs.type);
                if (!rhsBool) {
                    ev.type = makeError();
                    return ev;
                }
                llvm::BasicBlock* rhsEnd = builder.GetInsertBlock();
                if (!rhsEnd->getTerminator()) {
                    builder.CreateBr(endBlock);
                }

                builder.SetInsertPoint(endBlock);
                llvm::PHINode* phi = builder.CreatePHI(builder.getInt1Ty(), 2, "logicphi");
                if (node->getType() == conjunction) {
                    phi->addIncoming(builder.getFalse(), lhsBlock);
                    phi->addIncoming(rhsBool, rhsEnd);
                } else {
                    phi->addIncoming(builder.getTrue(), lhsBlock);
                    phi->addIncoming(rhsBool, rhsEnd);
                }
                ev.value = builder.CreateZExt(phi, builder.getInt32Ty(), "logicext");
                ev.type = makeInt();
                return ev;
            }
            case ternary: {
                const auto& kids = node->getChildren();
                ExprValue cond = codegenExpr(kids.at(0));
                llvm::Value* condBool = castToBool(cond.value, cond.type);
                if (!condBool) {
                    ev.type = makeError();
                    return ev;
                }
                llvm::BasicBlock* thenBlock = llvm::BasicBlock::Create(context, "ternary.then", currentFunction);
                llvm::BasicBlock* elseBlock = llvm::BasicBlock::Create(context, "ternary.else", currentFunction);
                llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(context, "ternary.end", currentFunction);

                builder.CreateCondBr(condBool, thenBlock, elseBlock);

                builder.SetInsertPoint(thenBlock);
                ExprValue tval = codegenExpr(kids.at(1));
                llvm::BasicBlock* thenEnd = builder.GetInsertBlock();

                builder.SetInsertPoint(elseBlock);
                ExprValue fval = codegenExpr(kids.at(2));
                llvm::BasicBlock* elseEnd = builder.GetInsertBlock();

                TypeDesc resultType = tval.type;
                if (valueCompatible(tval.type, fval.type) && valueCompatible(fval.type, tval.type)) {
                    if (isInteger(tval.type) && isInteger(fval.type)) {
                        resultType = makeInt();
                    }
                } else if (isPointer(tval.type) && fval.isNullPtrConst) {
                    resultType = tval.type;
                } else if (isPointer(fval.type) && tval.isNullPtrConst) {
                    resultType = fval.type;
                }

                builder.SetInsertPoint(thenEnd);
                llvm::Value* tcoerced = nullptr;
                if (resultType.kind != TypeDesc::Kind::Void) {
                    tcoerced = coerceValue(tval.value, tval.type, resultType, tval.isNullPtrConst);
                }
                if (!thenEnd->getTerminator()) builder.CreateBr(endBlock);

                builder.SetInsertPoint(elseEnd);
                llvm::Value* fcoerced = nullptr;
                if (resultType.kind != TypeDesc::Kind::Void) {
                    fcoerced = coerceValue(fval.value, fval.type, resultType, fval.isNullPtrConst);
                }
                if (!elseEnd->getTerminator()) builder.CreateBr(endBlock);

                builder.SetInsertPoint(endBlock);
                if (resultType.kind == TypeDesc::Kind::Void) {
                    ev.type = makeVoid();
                    ev.value = nullptr;
                    return ev;
                }
                llvm::PHINode* phi = builder.CreatePHI(llvmValueType(resultType), 2, "ternphi");
                phi->addIncoming(tcoerced, thenEnd);
                phi->addIncoming(fcoerced, elseEnd);
                ev.value = phi;
                ev.type = resultType;
                return ev;
            }
            case assignment: {
                const auto& kids = node->getChildren();
                TypeDesc ltype;
                llvm::Value* addr = codegenLValue(kids.at(0), ltype);
                if (!addr) {
                    ev.type = makeError();
                    return ev;
                }
                ExprValue rhs = codegenExpr(kids.at(1));
                llvm::Value* rhsVal = coerceValue(rhs.value, rhs.type, ltype, rhs.isNullPtrConst);
                if (!rhsVal || rhsVal->getType()->isVoidTy()) {
                    report("invalid assignment value");
                    ev.type = makeError();
                    return ev;
                }
                builder.CreateStore(rhsVal, addr);
                ev.type = ltype;
                ev.value = rhsVal;
                return ev;
            }
            default:
                ev.type = makeError();
                return ev;
        }
    }

    llvm::Value* codegenLValue(const Node::Ptr& node, TypeDesc& outType) {
        if (!node) return nullptr;
        switch (node->getType()) {
            case id: {
                std::string name = node->getToken()->getValue();
                auto* sym = lookup(name);
                if (!sym || sym->isFunction) {
                    report("lvalue is not addressable");
                    return nullptr;
                }
                outType = sym->type;
                return sym->value;
            }
            case parenthesizedexpr:
                if (!node->getChildren().empty()) {
                    return codegenLValue(node->getChildren().front(), outType);
                }
                return nullptr;
            case dereference: {
                ExprValue operand = codegenExpr(node->getChildren().at(0));
                if (!operand.value) {
                    return nullptr;
                }
                if (!isPointer(operand.type)) {
                    report("dereference of non-pointer");
                    return nullptr;
                }
                outType = *operand.type.pointee;
                if (outType.kind == TypeDesc::Kind::Function) {
                    report("cannot take lvalue of function type");
                    return nullptr;
                }
                return operand.value;
            }
            case arrayaccess: {
                const auto& kids = node->getChildren();
                ExprValue base = codegenExpr(kids.at(0));
                ExprValue idx = codegenExpr(kids.at(1));
                if (!base.value || !idx.value) {
                    return nullptr;
                }
                if (!isPointer(base.type) && isPointer(idx.type)) {
                    std::swap(base, idx);
                }
                if (!isPointer(base.type)) {
                    report("array base is not a pointer");
                    return nullptr;
                }
                outType = *base.type.pointee;
                llvm::Value* indexVal = castToIndex(idx.value, idx.type);
                llvm::Type* gepElemTy = (outType.kind == TypeDesc::Kind::Void || outType.kind == TypeDesc::Kind::Function)
                    ? builder.getInt8Ty()
                    : llvmType(outType);
                return builder.CreateGEP(gepElemTy, base.value, indexVal, "elemaddr");
            }
            case memberaccess: {
                const auto& kids = node->getChildren();
                TypeDesc baseType;
                llvm::Value* baseAddr = codegenLValue(kids.at(0), baseType);
                if (!isStruct(baseType)) {
                    report("member access on non-struct");
                    return nullptr;
                }
                std::string fieldName = kids.at(1)->getToken()->getValue();
                StructInfo& info = getStructInfo(baseType.structName);
                auto it = std::find(info.fieldNames.begin(), info.fieldNames.end(), fieldName);
                if (it == info.fieldNames.end()) {
                    report("unknown field '" + fieldName + "'");
                    return nullptr;
                }
                size_t index = static_cast<size_t>(std::distance(info.fieldNames.begin(), it));
                outType = info.fieldTypes.at(index);
                return builder.CreateStructGEP(llvmType(baseType), baseAddr,
                                               static_cast<unsigned>(index), "fieldaddr");
            }
            case pointermemberaccess: {
                const auto& kids = node->getChildren();
                ExprValue base = codegenExpr(kids.at(0));
                if (!base.value) {
                    return nullptr;
                }
                if (!isPointer(base.type) || !isStruct(*base.type.pointee)) {
                    report("pointer member access on non-struct pointer");
                    return nullptr;
                }
                TypeDesc structType = *base.type.pointee;
                std::string fieldName = kids.at(1)->getToken()->getValue();
                StructInfo& info = getStructInfo(structType.structName);
                auto it = std::find(info.fieldNames.begin(), info.fieldNames.end(), fieldName);
                if (it == info.fieldNames.end()) {
                    report("unknown field '" + fieldName + "'");
                    return nullptr;
                }
                size_t index = static_cast<size_t>(std::distance(info.fieldNames.begin(), it));
                outType = info.fieldTypes.at(index);
                return builder.CreateStructGEP(llvmType(structType), base.value,
                                               static_cast<unsigned>(index), "fieldaddr");
            }
            default:
                report("not an lvalue");
                return nullptr;
        }
    }

    bool codegenStmtExpr(const ast::StmtExpr& stmt) {
        if (stmt.expr.has_value()) {
            ExprValue value = codegenExpr(stmt.expr->root);
            if (isError(value.type)) return false;
        }
        return true;
    }

    bool codegenIf(const ast::StmtIf& stmt) {
        ExprValue cond = codegenExpr(stmt.condition.root);
        llvm::Value* condBool = castToBool(cond.value, cond.type);
        if (!condBool) return false;
        llvm::BasicBlock* thenBlock = llvm::BasicBlock::Create(context, "if.then", currentFunction);
        llvm::BasicBlock* elseBlock = nullptr;
        llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(context, "if.end", currentFunction);

        if (stmt.elseStmt) {
            elseBlock = llvm::BasicBlock::Create(context, "if.else", currentFunction);
            builder.CreateCondBr(condBool, thenBlock, elseBlock);
        } else {
            builder.CreateCondBr(condBool, thenBlock, mergeBlock);
        }

        builder.SetInsertPoint(thenBlock);
        if (!codegenStatement(*stmt.thenStmt)) return false;
        if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(mergeBlock);

        if (stmt.elseStmt) {
            builder.SetInsertPoint(elseBlock);
            if (!codegenStatement(*stmt.elseStmt)) return false;
            if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(mergeBlock);
        }

        builder.SetInsertPoint(mergeBlock);
        return true;
    }

    bool codegenWhile(const ast::StmtWhile& stmt) {
        llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(context, "while.cond", currentFunction);
        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(context, "while.body", currentFunction);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(context, "while.end", currentFunction);

        builder.CreateBr(condBlock);
        builder.SetInsertPoint(condBlock);
        ExprValue cond = codegenExpr(stmt.condition.root);
        llvm::Value* condBool = castToBool(cond.value, cond.type);
        if (!condBool) return false;
        builder.CreateCondBr(condBool, bodyBlock, endBlock);

        loopStack.push_back(LoopContext{endBlock, condBlock});
        builder.SetInsertPoint(bodyBlock);
        if (!codegenStatement(*stmt.body)) return false;
        if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(condBlock);
        loopStack.pop_back();

        builder.SetInsertPoint(endBlock);
        return true;
    }

    bool codegenLabel(const ast::StmtLabel& stmt) {
        auto it = labelBlocks.find(stmt.label);
        if (it == labelBlocks.end()) {
            report("label block missing for '" + stmt.label + "'");
            return false;
        }
        llvm::BasicBlock* target = it->second;
        if (!builder.GetInsertBlock()->getTerminator()) {
            builder.CreateBr(target);
        }
        builder.SetInsertPoint(target);
        return codegenStatement(*stmt.stmt);
    }

    bool codegenGoto(const ast::StmtGoto& stmt) {
        auto it = labelBlocks.find(stmt.label);
        if (it == labelBlocks.end()) {
            report("unknown label '" + stmt.label + "'");
            return false;
        }
        builder.CreateBr(it->second);
        return true;
    }

    bool codegenContinue(const ast::StmtContinue&) {
        if (loopStack.empty()) {
            report("continue not within a loop");
            return false;
        }
        builder.CreateBr(loopStack.back().continueBlock);
        return true;
    }

    bool codegenBreak(const ast::StmtBreak&) {
        if (loopStack.empty()) {
            report("break not within a loop");
            return false;
        }
        builder.CreateBr(loopStack.back().breakBlock);
        return true;
    }

    bool codegenReturn(const ast::StmtReturn& stmt) {
        if (currentReturnType.kind == TypeDesc::Kind::Void) {
            if (stmt.expr.has_value()) {
                (void)codegenExpr(stmt.expr->root); // evaluate side effects
            }
            builder.CreateRetVoid();
        } else {
            if (stmt.expr.has_value()) {
                ExprValue value = codegenExpr(stmt.expr->root);
                llvm::Value* coerced = coerceValue(value.value, value.type, currentReturnType,
                                                   value.isNullPtrConst);
                if (!coerced || coerced->getType()->isVoidTy()) {
                    report("invalid return value");
                    return false;
                }
                builder.CreateRet(coerced);
            } else if (currentReturnType.kind == TypeDesc::Kind::Pointer) {
                builder.CreateRet(llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(llvmValueType(currentReturnType))));
            } else if (currentReturnType.kind == TypeDesc::Kind::Char) {
                builder.CreateRet(llvm::ConstantInt::get(builder.getInt8Ty(), 0));
            } else {
                builder.CreateRet(llvm::ConstantInt::get(builder.getInt32Ty(), 0));
            }
        }
        return true;
    }
};

} // namespace

bool generate(const ast::TranslationUnit& tu, const std::string& inputPath, std::ostream& err) {
    std::filesystem::path p(inputPath);
    std::string moduleName = p.filename().string();
    Codegen cg(err, moduleName.empty() ? "module" : moduleName);
    if (!cg.run(tu, inputPath)) return false;
    return cg.writeToFile(inputPath);
}

} // namespace ir
