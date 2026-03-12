#include "Emitter.h"

#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/Support/raw_ostream.h>

Emitter::Emitter(const std::string& name)
    : builder(ctx),
      module(std::make_unique<llvm::Module>(name, ctx)) {
    scopes.push_back({});
}

void Emitter::emitFromParseTree(const ast::TranslationUnit& tu) {
    llvm::outs() << "\n=== IR Dump ===\n";

    if (module) {
        module->print(llvm::outs(), nullptr);
    }

    for (const auto& ext : tu.decls) {
        emitExternalDecl(ext);
    }
}

void Emitter::emitExternalDecl(const ast::ExternalDecl& ext) {
    llvm::outs() << "[ExternalDecl] Emitting...\n";
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, ast::Decl>) {
            emitDecl(arg);
        } else if constexpr (std::is_same_v<T, ast::FuncDef>) {
            emitFuncDef(arg);
        }
    }, ext.node);
    llvm::outs() << "[ExternalDecl] Successfully emitted\n";
}

Emitter::DeclInfo Emitter::buildDecl(const ast::Decl& decl) {
    llvm::outs() << "[Decl]         Building...\n";
    llvm::Type* baseType = emitType(decl.type);
    llvm::outs() << "[Decl]         Successfully emitted type\n";
    std::string name;
    llvm::Type* finalType = baseType;

    if (decl.declarator.has_value()) {
        llvm::outs() << "[Decl]         Has declarator\n";
        auto info = buildDeclarator(*decl.declarator, baseType);
        name = info.name;
        finalType = info.type;
    }

    return {name, finalType};
}

void Emitter::bindDecl(const ast::Decl& decl) {
    llvm::outs() << "[Decl]         Binding...\n";
    DeclInfo info = buildDecl(decl);
    llvm::outs() << "[Decl]         Name: '" << info.name << "'\n";
    llvm::Value* storage = allocateStorage(info);
    llvm::outs() << "[Decl]         Storage: " << storage << "\n";
    scopes.back()[info.name] = {storage, info.type};
    llvm::outs() << "[Decl]         Scopes size: " << scopes.size() << "\n";
    emitStorageInitialization(info, storage);
}

llvm::Value* Emitter::allocateStorage(const DeclInfo& info) {
    llvm::BasicBlock* currentBlock = builder.GetInsertBlock();
    llvm::outs() << "[Storage]      Allocating storage for '" << info.name << "'\n";
    llvm::outs() << "[Storage]      Current block: " << currentBlock << "\n";
    
    if (currentBlock) {
        llvm::outs() << "[Storage]      Parent: " << currentBlock->getParent() << "\n";
    }

    if (!currentBlock || !currentBlock->getParent()) {
        llvm::outs() << "[Storage]      No basic block - creating global variable\n";
        auto* gv = new llvm::GlobalVariable(
            *module,
            info.type,
            false,
            llvm::GlobalValue::ExternalLinkage,
            nullptr,
            info.name
        );
        return gv;
    }

    llvm::Function* fn = currentBlock->getParent();
    llvm::outs() << "[Storage]      Function: " << fn << "\n";
    
    // Save current insert point
    llvm::BasicBlock* savedBlock = builder.GetInsertBlock();
    llvm::BasicBlock::iterator savedPoint = builder.GetInsertPoint();

    // Insert alloca at top of entry block
    builder.SetInsertPoint(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    llvm::outs() << "[Storage]      Creating alloca: name='" << info.name 
                 << "', type=" << *info.type << "\n";
    llvm::AllocaInst* alloca = builder.CreateAlloca(info.type, nullptr, info.name);

    // Restore insert point
    builder.SetInsertPoint(savedBlock, savedPoint);
    
    return alloca;
}

void Emitter::emitStorageInitialization(const DeclInfo& info, llvm::Value* storage) {
    llvm::outs() << "[Storage]      Initializing storage\n";
    if (info.initializer.has_value()) {
        llvm::Value* value = emitExpr(*info.initializer);
        builder.CreateStore(value, storage);
    }
    llvm::outs() << "[Storage]      Successfully initialized\n";
}

llvm::Value* Emitter::emitExpr(const ast::Expr& expr) {
    llvm::outs() << "[Expr]         Emitting expression\n";
    return emitExprNode(expr.root);
}

llvm::Value* Emitter::emitExprNode(const Node::Ptr& node) {
    llvm::outs() << "[ExprNode]     Processing node\n";
    if (!node) return nullptr;

    auto kids = node->getChildren();

    switch (node->getType()) {
        case id: {
            llvm::outs() << "[ExprNode]     Identifier\n";
            auto name = node->getToken()->getValue();
            llvm::outs() << "[ExprNode]     Name: '" << name << "'\n";
            auto* addr = lookup(name);
            auto* ty   = lookupType(name);
            return builder.CreateLoad(ty, addr);
        }

        case decimalconst:
            llvm::outs() << "[ExprNode]     Decimal constant: " 
                         << node->getToken()->getValue() << "\n";
            return llvm::ConstantInt::get(
                llvm::Type::getInt32Ty(ctx),
                std::stoi(node->getToken()->getValue())
            );

        case stringliteral: {
            llvm::outs() << "[ExprNode]     String literal\n";
            std::string strValue = node->getToken()->getValue();
            
            // Remove quotes
            if (strValue.size() >= 2 && strValue.front() == '"' && strValue.back() == '"') {
                strValue = strValue.substr(1, strValue.size() - 2);
            }
            
            llvm::outs() << "[ExprNode]     String value: '" << strValue << "'\n";
            
            // Create global string literal
            llvm::Constant* strConstant = llvm::ConstantDataArray::getString(ctx, strValue, true);
            
            llvm::GlobalVariable* globalStr = new llvm::GlobalVariable(
                *module,
                strConstant->getType(),
                true,
                llvm::GlobalValue::PrivateLinkage,
                strConstant,
                ".str"
            );
            
            // Return pointer to first character (i8*)
            llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0);
            llvm::Value* indices[] = {zero, zero};
            
            return builder.CreateInBoundsGEP(
                strConstant->getType(),
                globalStr,
                indices,
                "str"
            );
        }

        case assignment: {
            llvm::outs() << "[ExprNode]     Assignment\n";
            auto* lhs = resolveAddress(kids.at(0));
            auto* rhs = emitExpr(ast::Expr{kids.at(1)});
            builder.CreateStore(rhs, lhs);
            return rhs;
        }

        case product:
            llvm::outs() << "[ExprNode]     Product\n";
            return builder.CreateMul(
                emitExpr(ast::Expr{kids.at(0)}),
                emitExpr(ast::Expr{kids.at(1)})
            );

        case sum:
            llvm::outs() << "[ExprNode]     Sum\n";
            return builder.CreateAdd(
                emitExpr(ast::Expr{kids.at(0)}),
                emitExpr(ast::Expr{kids.at(1)})
            );

        case difference:
            llvm::outs() << "[ExprNode]     Difference\n";
            return builder.CreateSub(
                emitExpr(ast::Expr{kids.at(0)}),
                emitExpr(ast::Expr{kids.at(1)})
            );

        case comparison:
            llvm::outs() << "[ExprNode]     Comparison\n";
            return builder.CreateICmpSLT(
                emitExpr(ast::Expr{kids.at(0)}),
                emitExpr(ast::Expr{kids.at(1)})
            );

        case equality:
            llvm::outs() << "[ExprNode]     Equality\n";
            return builder.CreateICmpEQ(
                emitExpr(ast::Expr{kids.at(0)}),
                emitExpr(ast::Expr{kids.at(1)})
            );

        case conjunction:
            llvm::outs() << "[ExprNode]     Conjunction\n";
            return builder.CreateAnd(
                emitExpr(ast::Expr{kids.at(0)}),
                emitExpr(ast::Expr{kids.at(1)})
            );

        case disjunction:
            llvm::outs() << "[ExprNode]     Disjunction\n";
            return builder.CreateOr(
                emitExpr(ast::Expr{kids.at(0)}),
                emitExpr(ast::Expr{kids.at(1)})
            );

        case reference:
            llvm::outs() << "[ExprNode]     Reference (&)\n";
            return resolveAddress(kids.at(0));

        case dereference: {
            llvm::outs() << "[ExprNode]     Dereference (*)\n";
            auto* ptr = emitExpr(ast::Expr{kids.at(0)});
            auto* pointeeTy = resolveType(kids.at(0));
            return builder.CreateLoad(pointeeTy, ptr);
        }

        case preincrement: {
            llvm::outs() << "[ExprNode]     Pre-increment\n";
            auto* addr = resolveAddress(kids.at(0));
            auto* ty   = resolveType(kids.at(0));
            auto* val  = builder.CreateLoad(ty, addr);
            auto* inc  = builder.CreateAdd(val, llvm::ConstantInt::get(ty, 1));
            builder.CreateStore(inc, addr);
            return inc;
        }

        case postincrement: {
            llvm::outs() << "[ExprNode]     Post-increment\n";
            auto* addr   = resolveAddress(kids.at(0));
            auto* ty     = resolveType(kids.at(0));
            auto* oldVal = builder.CreateLoad(ty, addr);
            auto* newVal = builder.CreateAdd(oldVal, llvm::ConstantInt::get(ty, 1));
            builder.CreateStore(newVal, addr);
            return oldVal;
        }

        case memberaccess: {
            llvm::outs() << "[ExprNode]     Member access (.)\n";
            auto* addr     = resolveAddress(kids.at(0));
            auto* structTy = resolveType(kids.at(0));

            if (!llvm::isa<llvm::StructType>(structTy)) {
                llvm::outs() << "[ExprNode]     ERROR: Not a struct type\n";
                return nullptr;
            }

            llvm::StructType* structType = llvm::cast<llvm::StructType>(structTy);
            std::string fieldName = kids.at(1)->getToken()->getValue();
            llvm::outs() << "[ExprNode]     Field: '" << fieldName << "'\n";
            int fieldIndex = getFieldIndex(structType, fieldName);

            return emitStructAccess(addr, structTy, fieldIndex);
        }

        case pointermemberaccess: {
            llvm::outs() << "[ExprNode]     Pointer member access (->)\n";
            
            auto* ptrValue = emitExpr(ast::Expr{kids.at(0)});
            llvm::Type* ptrType = ptrValue->getType();
            llvm::outs() << "[ExprNode]     Pointer type: " << *ptrType << "\n";
            
            llvm::StructType* structTy = nullptr;
            if (auto* pt = llvm::dyn_cast<llvm::PointerType>(ptrType)) {
                structTy = llvm::dyn_cast<llvm::StructType>(pt->getPointerElementType());
            }
            
            if (!structTy) {
                llvm::outs() << "[ExprNode]     ERROR: Not a pointer to struct\n";
                return nullptr;
            }
            
            std::string fieldName = kids.at(1)->getToken()->getValue();
            llvm::outs() << "[ExprNode]     Field: '" << fieldName << "'\n";
            int fieldIndex = getFieldIndex(structTy, fieldName);
            
            return emitStructAccess(ptrValue, structTy, fieldIndex);
        }

        case arrayaccess: {
            llvm::outs() << "[ExprNode]     Array access\n";
            auto* base  = resolveAddress(kids.at(0));
            auto* elemTy = resolveType(kids.at(0));
            auto* index = emitExpr(ast::Expr{kids.at(1)});
            return builder.CreateGEP(elemTy, base, index);
        }

        case negationlogical:
            llvm::outs() << "[ExprNode]     Logical negation (!)\n";
            return builder.CreateNot(emitExpr(ast::Expr{kids.at(0)}));

        case negationarithmetic:
            llvm::outs() << "[ExprNode]     Arithmetic negation (-)\n";
            return builder.CreateNeg(emitExpr(ast::Expr{kids.at(0)}));

        case functioncall:
            llvm::outs() << "[ExprNode]     Function call\n";
            return emitCall(node);
            
        case parenthesizedexpr:
            llvm::outs() << "[ExprNode]     Parenthesized expression\n";
            return emitExpr(ast::Expr{kids.at(0)});
            
        case sizeoperator: {
            llvm::outs() << "[ExprNode]     sizeof operator\n";
            llvm::Type* currentType = resolveType(kids.at(0));
            
            llvm::DataLayout dataLayout(module.get());
            uint64_t sizeInBytes = dataLayout.getTypeAllocSize(currentType);
            llvm::outs() << "[ExprNode]     Size: " << sizeInBytes << " bytes\n";
            
            return llvm::ConstantInt::get(
                llvm::Type::getInt32Ty(ctx), 
                sizeInBytes
            );
        }
        
        default:
            llvm::outs() << "[ExprNode]     ERROR: Unresolved node type: " << node->getType() << "\n";
            if (node->getToken()) {
                llvm::outs() << "[ExprNode]     Token value: '" << node->getToken()->getValue() << "'\n";
            }
            return nullptr;
    }
}

llvm::Value* Emitter::resolveAddress(const Node::Ptr& node) {
    llvm::outs() << "[Resolve]      Resolving address\n";
    if (node->getType() == id) {
        return lookup(node->getToken()->getValue());
    }
    if (node->getType() == dereference) {
        return emitExpr(ast::Expr{node->getChildren().at(0)});
    }
    return nullptr;
}

llvm::Type* Emitter::resolveType(const Node::Ptr& node) {
    llvm::outs() << "[Resolve]      Resolving type\n";
    if (node->getType() == id) {
        return lookupType(node->getToken()->getValue());
    }
    return llvm::Type::getInt32Ty(ctx);
}

void Emitter::emitContinue() {
    llvm::outs() << "[Control]      Continue statement\n";
    if (continueStack.empty()) return;
    builder.CreateBr(continueStack.back());
}

void Emitter::emitBreak() {
    llvm::outs() << "[Control]      Break statement\n";
    if (breakStack.empty()) return;
    builder.CreateBr(breakStack.back());
}

llvm::Value* Emitter::emitStructAccess(llvm::Value* base, llvm::Type* structTy, int fieldIndex) {
    llvm::outs() << "[Struct]       Accessing field at index " << fieldIndex << "\n";
    return builder.CreateStructGEP(structTy, base, fieldIndex);
}

llvm::Value* Emitter::emitCall(const Node::Ptr& node) {
    llvm::outs() << "[Call]         Emitting function call\n";
    auto kids = node->getChildren();
    std::string name = kids.at(0)->getToken()->getValue();
    llvm::outs() << "[Call]         Function name: '" << name << "'\n";
    llvm::Function* fn = module->getFunction(name);

    std::vector<llvm::Value*> args;
    for (size_t i = 1; i < kids.size(); i++) {
        args.push_back(emitExpr(ast::Expr{kids.at(i)}));
    }
    llvm::outs() << "[Call]         " << args.size() << " arguments\n";
    return builder.CreateCall(fn, args);
}

void Emitter::emitFuncDef(const ast::FuncDef& funcDef) {
    llvm::outs() << "[FuncDef]      Emitting function definition...\n";
    llvm::Type* returnType = emitType(funcDef.type);
    llvm::outs() << "[FuncDef]      Return type emitted\n";
    
    DeclInfo info = buildDeclarator(funcDef.declarator, returnType);
    std::string name = info.name;
    
    llvm::outs() << "[FuncDef]      Function name: '" << name << "'\n";
    
    llvm::FunctionType* fnType = nullptr;
    if (auto* funcTy = llvm::dyn_cast<llvm::FunctionType>(info.type)) {
        fnType = funcTy;
    } else {
        fnType = llvm::FunctionType::get(returnType, {}, false);
    }
    
    llvm::Function* fn = llvm::Function::Create(
        fnType, llvm::Function::ExternalLinkage, name, module.get()
    );

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx, "entry", fn);
    assert(entry->getParent() && "BasicBlock has no parent!");
    builder.SetInsertPoint(entry);
    scopes.push_back({});

    const ast::DirectDeclarator* directDecl = &funcDef.declarator.direct;
    while (directDecl->kind == ast::DirectDeclarator::Kind::Nested) {
        directDecl = &directDecl->nested->direct;
    }

    size_t i = 0;
    if (!directDecl->params.empty()) {
        llvm::outs() << "[FuncDef]      Processing parameters\n";    
        const auto& params = directDecl->params.front().params;

        for (auto& arg : fn->args()) {
            if (i >= params.size()) break;

            if (arg.getType()->isVoidTy()) {
                llvm::outs() << "[FuncDef]      Skipping void parameter\n";
                i++;
                continue;
            }

            std::string paramName;
            if (params[i].declarator)
                paramName = params[i].declarator->direct.identifier;
            
            llvm::outs() << "[FuncDef]      Parameter: '" << paramName << "'\n";
            llvm::AllocaInst* alloca = builder.CreateAlloca(arg.getType(), nullptr, paramName);
            
            builder.CreateStore(&arg, alloca);
            scopes.back()[paramName] = {alloca, arg.getType()};
            i++;
        }
        llvm::outs() << "[FuncDef]      Parameters bound\n";    
    }

    llvm::outs() << "[FuncDef]      Function scope symbols:\n";
    for (auto& [k, v] : scopes.back())
        llvm::outs() << "[FuncDef]        - " << k << "\n";

    emitStatement(funcDef.body);
    scopes.pop_back();
    llvm::outs() << "[FuncDef]      Function definition complete\n";
}

Emitter::DeclInfo Emitter::buildDeclarator(const ast::Declarator& decl, llvm::Type* baseType) {
    std::string name;
    llvm::Type* currentType = baseType;
    llvm::outs() << "[Declarator]   Building declarator\n";

    if (decl.direct.kind == ast::DirectDeclarator::Kind::Identifier) {
        llvm::outs() << "[Declarator]   Direct identifier\n";
        name = decl.direct.identifier;
    } else if (decl.direct.kind == ast::DirectDeclarator::Kind::Nested) {
        llvm::outs() << "[Declarator]   Nested declarator\n";
        llvm::outs() << "[Declarator]   Nested identifier: '" 
                     << decl.direct.nested->direct.identifier << "'\n";
        
        auto nestedInfo = buildDeclarator(*decl.direct.nested, baseType);
        name = nestedInfo.name;
        currentType = nestedInfo.type;
    }
    
    if (!decl.direct.params.empty()) {
        std::vector<llvm::Type*> paramTypes;

        for (const auto& param : decl.direct.params.front().params) {
            llvm::Type* paramType = emitType(param.type);
            
            if (!paramType->isVoidTy()) {
                paramTypes.push_back(paramType);
                llvm::outs() << "[Declarator]   Added parameter type\n";
            } else {
                llvm::outs() << "[Declarator]   Skipped void parameter\n";
            }
        }

        currentType = llvm::FunctionType::get(currentType, paramTypes, false);
    }

    for (int i = 0; i < decl.pointerDepth; i++) {
        currentType = llvm::PointerType::get(currentType, 0);
    }

    return {name, currentType};
}

llvm::Value* Emitter::lookup(const std::string& name) {
    llvm::outs() << "[Lookup]       Looking up '" << name << "'\n";
    llvm::outs() << "[Lookup]       Scopes: " << scopes.size() << "\n";

    for (int i = (int)scopes.size() - 1; i >= 0; --i) {
        llvm::outs() << "[Lookup]       Scope level " << i << ":\n";
        for (auto& [k, v] : scopes[i]) {
            llvm::outs() << "[Lookup]         - " << k << "\n";
        }
    }

    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            llvm::outs() << "[Lookup]       Found: " << found->second.first << "\n";
            return found->second.first;
        }
    }

    llvm::outs() << "[Lookup]       ERROR: Not found\n";
    return nullptr;
}

llvm::Type* Emitter::lookupType(const std::string& name) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second.second;
    }
    return nullptr;
}

void Emitter::bindParameters(llvm::Function* fn, const std::vector<ast::ParamDecl>& params) {
    llvm::outs() << "[Params]       Binding parameters\n";
    scopes.push_back({});
    size_t i = 0;
    for (auto& arg : fn->args()) {
        std::string paramName;
        
        if (i < params.size() && params[i].declarator) {
            paramName = params[i].declarator->direct.identifier;
        }
        
        llvm::outs() << "[Params]       Parameter: '" << paramName << "'\n";

        auto* alloca = createAlloca(fn, paramName, arg.getType());
        builder.CreateStore(&arg, alloca);
        scopes.back()[paramName] = {alloca, arg.getType()};
        i++;
    }
}

llvm::AllocaInst* Emitter::createAlloca(llvm::Function* fn, const std::string& name, llvm::Type* type) {
    llvm::outs() << "[Alloca]       Creating alloca for '" << name << "'\n";
    llvm::IRBuilder<> tmpBuilder(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tmpBuilder.CreateAlloca(type, nullptr, name);
}

llvm::Type* Emitter::emitType(const ast::TypeSpec& type) {
    llvm::outs() << "[Type]         Emitting type\n";
    llvm::outs() << "[Type]         Kind: " 
                 << (type.kind == ast::TypeSpec::Kind::Struct ? "struct" : "builtin") << "\n";
    
    if (!type.builtin.empty()) {
        llvm::outs() << "[Type]         Builtin: " << type.builtin << "\n";
    }

    if (type.structType.name.has_value()) {
        llvm::outs() << "[Type]         Struct name: " << *type.structType.name << "\n";
    }
    
    if (type.kind == ast::TypeSpec::Kind::Struct) {
        const auto& s = type.structType;
        std::string structName = s.name.has_value() ? s.name.value() : "";
        
        // Forward reference check
        if (s.fields.empty() && !structName.empty()) {
            llvm::outs() << "[Type]         Forward reference to '" << structName << "'\n";
            
            for (auto& [name, info] : structTable) {
                if (name == structName || name.find(structName + ".") == 0) {
                    llvm::outs() << "[Type]         Found existing struct '" << name << "'\n";
                    return info.type;
                }
            }
            
            llvm::outs() << "[Type]         WARNING: Undefined struct, creating opaque type\n";
            return llvm::StructType::create(ctx, structName);
        }
        
        // Full struct definition
        llvm::outs() << "[Type]         Full struct definition\n";
        std::vector<llvm::Type*> fieldTypes;
        StructInfo info;
        int idx = 0;
        
        for (const auto& field : s.fields) {
            llvm::Type* fieldTy = emitType(field.type);
            fieldTypes.push_back(fieldTy);
            
            if (field.declarator) {
                std::string fieldName = field.declarator->direct.identifier;
                info.fieldIndices[fieldName] = idx;
                info.fieldTypes[fieldName] = fieldTy;
                llvm::outs() << "[Type]         Field '" << fieldName << "' at index " << idx << "\n";
            }
            idx++;
        }

        llvm::StructType* structType = llvm::StructType::create(
            ctx, llvm::ArrayRef<llvm::Type*>(fieldTypes), structName
        );
        
        info.type = structType;
        std::string llvmName = structType->getName().str();
        
        llvm::outs() << "[Type]         Created struct\n";
        llvm::outs() << "[Type]           Original name: '" << structName << "'\n";
        llvm::outs() << "[Type]           LLVM name: '" << llvmName << "'\n";
        llvm::outs() << "[Type]           Fields: " << info.fieldIndices.size() << "\n";
        
        if (!llvmName.empty()) {
            structTable[llvmName] = info;
            llvm::outs() << "[Type]         Registered in struct table\n";
        }
        
        return structType;
    }

    llvm::outs() << "[Type]         Handling builtin type\n";
    if (type.builtin == "int")    return llvm::Type::getInt32Ty(ctx);
    if (type.builtin == "char")   return llvm::Type::getInt8Ty(ctx);
    if (type.builtin == "float")  return llvm::Type::getFloatTy(ctx);
    if (type.builtin == "double") return llvm::Type::getDoubleTy(ctx);
    if (type.builtin == "void")   return llvm::Type::getVoidTy(ctx);
    
    return llvm::Type::getInt32Ty(ctx);
}

// =============================================================
// ======================= STATEMENTS ==========================
// =============================================================

void Emitter::emitStatement(const ast::Statement& stmt) {
    llvm::outs() << "[Stmt]         Emitting statement\n";
    std::visit([this](auto&& s) {
        emitStatement(s);
    }, stmt.node);
}

void Emitter::emitDecl(const ast::Decl& decl) {
    llvm::outs() << "[Stmt]         Declaration statement\n";
    bindDecl(decl);
}

void Emitter::emitStatement(const ast::StmtGoto& gotoStmt) {
    llvm::outs() << "[Stmt]         Goto: " << gotoStmt.label << "\n";
    auto it = labelMap.find(gotoStmt.label);
    if (it != labelMap.end()) {
        builder.CreateBr(it->second);
    }
}

void Emitter::emitStatement(const ast::StmtLabel& label) {
    llvm::outs() << "[Stmt]         Label: " << label.label << "\n";
    llvm::Function* fn = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(ctx, label.label, fn);
    labelMap[label.label] = bb;
    builder.CreateBr(bb);
    assert(bb->getParent() && "BasicBlock has no parent!");
    builder.SetInsertPoint(bb);
    emitStatement(*label.stmt);
}

void Emitter::emitStatement(const ast::StmtDecl& decl) {
    llvm::outs() << "[Stmt]         Declaration\n";
    emitDecl(decl.decl);
}

void Emitter::emitStatement(const ast::StmtExpr& expr) {
    llvm::outs() << "[Stmt]         Expression statement\n";
    if (expr.expr.has_value()) {
        emitExpr(*expr.expr);
    }
}

int Emitter::getFieldIndex(llvm::StructType* structTy, const std::string& fieldName) {
    llvm::outs() << "[Struct]       Looking up field '" << fieldName << "'\n";
    
    for (auto& [name, info] : structTable) {
        if (info.type == structTy) {
            auto it = info.fieldIndices.find(fieldName);
            if (it != info.fieldIndices.end()) {
                llvm::outs() << "[Struct]       Found at index " << it->second << "\n";
                return it->second;
            }
        }
    }
    
    llvm::outs() << "[Struct]       ERROR: Field not found\n";
    return 0;
}

void Emitter::emitStatement(const ast::StmtIf& ifStmt) {
    llvm::outs() << "[Stmt]         If statement\n";
    llvm::Value* condValue = emitExpr(ifStmt.condition);
    condValue = builder.CreateICmpNE(
        condValue, llvm::ConstantInt::get(condValue->getType(), 0), "ifcond"
    );

    llvm::Function* function = builder.GetInsertBlock()->getParent();

    llvm::BasicBlock* thenBB  = llvm::BasicBlock::Create(ctx, "then",   function);
    llvm::BasicBlock* elseBB  = llvm::BasicBlock::Create(ctx, "else");
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx, "ifcont");

    if (ifStmt.elseStmt)
        builder.CreateCondBr(condValue, thenBB, elseBB);
    else
        builder.CreateCondBr(condValue, thenBB, mergeBB);

    // THEN
    assert(thenBB->getParent() && "BasicBlock has no parent!");
    builder.SetInsertPoint(thenBB);
    emitStatement(*ifStmt.thenStmt);
    if (!builder.GetInsertBlock()->getTerminator())
        builder.CreateBr(mergeBB);

    // ELSE
    if (ifStmt.elseStmt) {
        elseBB->insertInto(function);
        assert(elseBB->getParent() && "BasicBlock has no parent!");
        builder.SetInsertPoint(elseBB);
        emitStatement(*ifStmt.elseStmt);
        if (!builder.GetInsertBlock()->getTerminator())
            builder.CreateBr(mergeBB);
    }

    // MERGE
    mergeBB->insertInto(function);
    assert(mergeBB->getParent() && "BasicBlock has no parent!");
    builder.SetInsertPoint(mergeBB);
}

void Emitter::emitStatement(const ast::StmtWhile& whileStmt) {
    llvm::outs() << "[Stmt]         While loop\n";
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(ctx, "while.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(ctx, "while.body", function);
    llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(ctx, "while.exit", function);

    builder.CreateBr(condBB);

    assert(condBB->getParent() && "BasicBlock has no parent!");
    builder.SetInsertPoint(condBB);
    llvm::Value* condValue = emitExpr(whileStmt.condition);
    condValue = builder.CreateICmpNE(
        condValue, llvm::ConstantInt::get(condValue->getType(), 0), "whilecond"
    );
    builder.CreateCondBr(condValue, bodyBB, exitBB);

    breakStack.push_back(exitBB);
    continueStack.push_back(condBB);

    assert(bodyBB->getParent() && "BasicBlock has no parent!");
    builder.SetInsertPoint(bodyBB);
    emitStatement(*whileStmt.body);
    if (!builder.GetInsertBlock()->getTerminator())
        builder.CreateBr(condBB);

    breakStack.pop_back();
    continueStack.pop_back();

    assert(exitBB->getParent() && "BasicBlock has no parent!");
    builder.SetInsertPoint(exitBB);
}

void Emitter::emitStatement(const ast::StmtReturn& ret) {
    llvm::outs() << "[Stmt]         Return statement\n";
    if (ret.expr.has_value()) {
        builder.CreateRet(emitExpr(*ret.expr));
    } else {
        builder.CreateRetVoid();
    }
}

void Emitter::emitStatement(const ast::StmtContinue&) {
    llvm::outs() << "[Stmt]         Continue\n";
    emitContinue();
}

void Emitter::emitStatement(const ast::StmtBreak&) {
    llvm::outs() << "[Stmt]         Break\n";
    emitBreak();
}

void Emitter::emitStatement(const ast::StmtCompound& compound) {
    llvm::outs() << "[Stmt]         Compound statement (block)\n";
    scopes.push_back({});
    for (auto& stmt : compound.items) {
        emitStatement(*stmt);
        if (builder.GetInsertBlock()->getTerminator())
            break;
    }
    scopes.pop_back();
}

void Emitter::emitDirectDeclarator(const ast::DirectDeclarator& direct) {
    llvm::outs() << "[DirectDecl]   Emitting direct declarator\n";
    if (direct.kind == ast::DirectDeclarator::Kind::Identifier) {
        currentDeclaratorName = direct.identifier;
    }
}

void Emitter::emitParamList(const ast::ParamList& paramList) {
    llvm::outs() << "[ParamList]    Emitting parameter list\n";
    std::vector<llvm::Type*> paramTypes;
    for (const auto& param : paramList.params) {
        paramTypes.push_back(emitType(param.type));
    }
    currentFunctionParamTypes = std::move(paramTypes);
}

void Emitter::emitAbstractDeclarator(const ast::AbstractDeclarator& abstract) {
    llvm::outs() << "[AbstractDecl] Emitting abstract declarator\n";
    currentPointerDepth = abstract.pointerDepth;
    if (abstract.hasDirect) {
        emitDirectAbstractDeclarator(abstract.direct);
    }
}

void Emitter::emitDirectAbstractDeclarator(const ast::DirectAbstractDeclarator& directAbstract) {
    llvm::outs() << "[DirectAbsDecl] Emitting direct abstract declarator\n";
    if (directAbstract.kind == ast::DirectAbstractDeclarator::Kind::ParamList) {
        currentFunctionParamTypes.clear();
        for (const auto& param : directAbstract.firstParamList.params) {
            currentFunctionParamTypes.push_back(emitType(param.type));
        }
    }
    for (const auto& suffix : directAbstract.suffixes) {
        if (suffix.isArray) {
            if (suffix.arraySize.has_value()) {
                arraySuffixStack.push_back(emitExpr(*suffix.arraySize));
            } else {
                arraySuffixStack.push_back(nullptr);
            }
        } else {
            emitParamList(suffix);
        }
    }
}

void Emitter::emitParamListTail(const std::vector<ast::ParamDecl>& tail) {
    llvm::outs() << "[ParamListTail] Emitting parameter list tail\n";
    for (const auto& param : tail) {
        currentFunctionParamTypes.push_back(emitType(param.type));
    }
}