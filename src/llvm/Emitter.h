#pragma once

#include "../ast/Ast.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/BasicBlock.h>

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <optional>
#include <map>

class Emitter {
public:
    struct DeclInfo {
        std::string name;
        llvm::Type* type;
        std::optional<ast::Expr> initializer;
    };

    struct StructInfo {
        llvm::StructType* type;
        std::map<std::string, int> fieldIndices;      
        std::map<std::string, llvm::Type*> fieldTypes;
    };

    llvm::LLVMContext ctx;
    llvm::IRBuilder<> builder;
    std::unique_ptr<llvm::Module> module;

    // stores {value, type} pairs for proper LLVM 17+ type tracking
    std::vector<std::unordered_map<std::string, std::pair<llvm::Value*, llvm::Type*>>> scopes;
    std::unordered_map<std::string, llvm::BasicBlock*> labelMap;
    std::vector<llvm::BasicBlock*> breakStack;
    std::vector<llvm::BasicBlock*> continueStack;
    std::map<std::string, StructInfo> structTable;  // struct name -> info

    std::string currentDeclaratorName;
    std::vector<llvm::Type*> currentFunctionParamTypes;
    int currentPointerDepth = 0;
    std::vector<std::optional<llvm::Value*>> arraySuffixStack;

    explicit Emitter(const std::string& name);

    void emitFromParseTree(const ast::TranslationUnit& tu);
    void emitExternalDecl(const ast::ExternalDecl& ext);
    void emitFuncDef(const ast::FuncDef& fn);
    void emitDecl(const ast::Decl& decl);

    // Statement dispatcher
    void emitStatement(const ast::Statement& stmt);

    // Statement overloads — covers every variant member
    void emitStatement(const ast::StmtCompound& compound);
    void emitStatement(const ast::StmtDecl& decl);
    void emitStatement(const ast::StmtExpr& expr);
    void emitStatement(const ast::StmtIf& ifStmt);
    void emitStatement(const ast::StmtWhile& whileStmt);
    void emitStatement(const ast::StmtLabel& label);
    void emitStatement(const ast::StmtGoto& gotoStmt);
    void emitStatement(const ast::StmtContinue& cont);
    void emitStatement(const ast::StmtBreak& brk);
    void emitStatement(const ast::StmtReturn& ret);

    void emitBreak();
    void emitContinue();

    llvm::Value* emitExpr(const ast::Expr& expr);
    llvm::Value* emitExprNode(const Node::Ptr& node);
    llvm::Value* emitCall(const Node::Ptr& node);
    llvm::Value* emitStructAccess(llvm::Value* base, llvm::Type* structTy, int fieldIndex);
    llvm::Type*  emitType(const ast::TypeSpec& type);

    DeclInfo buildDecl(const ast::Decl& decl);
    DeclInfo buildDeclarator(const ast::Declarator& decl, llvm::Type* baseType);
    void bindDecl(const ast::Decl& decl);
    void bindParameters(llvm::Function* fn, const std::vector<ast::ParamDecl>& params);
    void emitDirectDeclarator(const ast::DirectDeclarator& direct);
    void emitAbstractDeclarator(const ast::AbstractDeclarator& abstract);
    void emitDirectAbstractDeclarator(const ast::DirectAbstractDeclarator& directAbstract);
    void emitParamList(const ast::ParamList& paramList);
    void emitParamListTail(const std::vector<ast::ParamDecl>& tail);
    void emitDirectDecSuffixes(const ast::Declarator& decl);

    int getFieldIndex(llvm::StructType* structTy, const std::string& fieldName);
    llvm::Value* resolveAddress(const Node::Ptr& node);
    llvm::Type*  resolveType(const Node::Ptr& node);
    llvm::Value* allocateStorage(const DeclInfo& info);
    void emitStorageInitialization(const DeclInfo& info, llvm::Value* storage);
    llvm::Value* lookup(const std::string& name);
    llvm::Type*  lookupType(const std::string& name);
    llvm::AllocaInst* createAlloca(llvm::Function* fn, const std::string& name, llvm::Type* type);
};