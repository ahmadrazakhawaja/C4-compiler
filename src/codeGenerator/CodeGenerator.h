
#include <string>
#include "../helper/structs/Node.h"
#include <sstream>
#include <memory>

class CodeGenerator {
public:
    // ========================================================================
    // Main Entry Point
    // ========================================================================
    
    static std::string generate(const Node::Ptr& node);
    static void printCode(const Node::Ptr& ast);
    
    // ========================================================================
    // Utility Functions
    // ========================================================================
    
    static std::string getIndent(int indentLevel);
    
    // ========================================================================
    // Node Dispatcher
    // ========================================================================
    
    static void generateNode(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    
    // ========================================================================
    // Top-Level Declarations
    // ========================================================================
    
    static void generateRoot(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    static void generateFunctionDecl(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    static void generateFunctionBody(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    static void generateParameter(const Node::Ptr& node, std::stringstream& code);
    static void generateStructDecl(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    static void generateMember(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    
    // ========================================================================
    // Statements
    // ========================================================================
    
    static void generateVarDecl(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    static void generateAssign(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    static void generateIf(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    static void generateElse(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    static void generateWhile(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    static void generateLabel(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    static void generateReturn(const Node::Ptr& node, std::stringstream& code, int& indentLevel);
    
    // ========================================================================
    // Type Handling & Expressions
    // ========================================================================
    
    static std::string extractTypeString(const Node::Ptr& typeNode, Node::Ptr& structDef);
    static std::string generateType(const Node::Ptr& typeNode);
    static std::string generateExpression(const Node::Ptr& node);
    static std::string generateAssignExpression(const Node::Ptr& node);
    static std::string generateMemberAccess(const Node::Ptr& node);
    static std::string generatePointerMemberAccess(const Node::Ptr& node);
    static std::string generateReference(const Node::Ptr& node);
    static std::string generateDereference(const Node::Ptr& node);
    static std::string generateBinaryOp(const Node::Ptr& node);
    static std::string generateFunctionCall(const Node::Ptr& node);
};