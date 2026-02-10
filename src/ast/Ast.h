#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <iosfwd>

#include "../helper/structs/Node.h"

namespace ast {

struct SourceLocation {
    int line = -1;
    int column = -1;
};

struct TypeSpec;
struct Declarator;
struct AbstractDeclarator;
struct ParamList;
struct Decl;
struct Statement;

struct Expr {
    Node::Ptr root;
};

struct StructType {
    std::optional<std::string> name;
    SourceLocation nameLoc;
    std::vector<Decl> fields;
};

struct TypeSpec {
    enum class Kind { Builtin, Struct };
    Kind kind = Kind::Builtin;
    std::string builtin;
    StructType structType;
    SourceLocation loc;
};

struct ParamDecl {
    TypeSpec type;
    std::shared_ptr<Declarator> declarator;
    std::shared_ptr<AbstractDeclarator> abstractDeclarator;
};

struct ParamList {
    std::vector<ParamDecl> params;
    bool isArray = false;
    std::optional<Expr> arraySize;
};

struct DirectDeclarator {
    enum class Kind { Identifier, Nested };
    Kind kind = Kind::Identifier;
    std::string identifier;
    SourceLocation identifierLoc;
    std::shared_ptr<Declarator> nested;
    std::vector<ParamList> params;
};

struct Declarator {
    int pointerDepth = 0;
    DirectDeclarator direct;
};

struct DirectAbstractDeclarator {
    enum class Kind { ParamList, Nested };
    Kind kind = Kind::ParamList;
    ParamList firstParamList;
    std::shared_ptr<AbstractDeclarator> nested;
    std::vector<ParamList> suffixes;
};

struct AbstractDeclarator {
    int pointerDepth = 0;
    bool hasDirect = false;
    DirectAbstractDeclarator direct;
};

struct Decl {
    TypeSpec type;
    std::optional<Declarator> declarator;
    bool isExtern = false;
};

struct StmtCompound {
    std::vector<std::shared_ptr<Statement>> items;
};

struct StmtDecl {
    Decl decl;
};

struct StmtExpr {
    std::optional<Expr> expr;
};

struct StmtIf {
    SourceLocation loc;
    Expr condition;
    std::shared_ptr<Statement> thenStmt;
    std::shared_ptr<Statement> elseStmt;
};

struct StmtWhile {
    SourceLocation loc;
    Expr condition;
    std::shared_ptr<Statement> body;
};

struct StmtLabel {
    std::string label;
    SourceLocation loc;
    std::shared_ptr<Statement> stmt;
};

struct StmtGoto {
    std::string label;
    SourceLocation loc;
};

struct StmtContinue {
    SourceLocation loc;
};
struct StmtBreak {
    SourceLocation loc;
};

struct StmtReturn {
    std::optional<Expr> expr;
    SourceLocation loc;
};

struct Statement {
    using Variant = std::variant<
        StmtCompound,
        StmtDecl,
        StmtExpr,
        StmtIf,
        StmtWhile,
        StmtLabel,
        StmtGoto,
        StmtContinue,
        StmtBreak,
        StmtReturn>;
    Variant node;
};

struct FuncDef {
    TypeSpec type;
    Declarator declarator;
    StmtCompound body;
};

struct ExternalDecl {
    std::variant<Decl, FuncDef> node;
};

struct TranslationUnit {
    std::vector<ExternalDecl> decls;
};

TranslationUnit buildFromParseTree(const Node::Ptr& root);
void printAst(const TranslationUnit& tu, std::ostream& os);

} // namespace ast
