#include <iostream>
#include "CodeGenerator.h"

// ============================================================================
// Main Entry Point
// ============================================================================

std::string CodeGenerator::generate(const Node::Ptr& node) {
    std::stringstream code;
    int indentLevel = 0;
    CodeGenerator::generateNode(node, code, indentLevel);
    return code.str();
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string CodeGenerator::getIndent(int indentLevel) {
    return std::string(indentLevel * 4, ' ');
}

// ============================================================================
// Node Dispatcher
// ============================================================================

void CodeGenerator::generateNode(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    if (!node) return;
    
    // Dispatch to appropriate generator based on node type
    switch(node->getType()) {
        case Symbol::Root:
            CodeGenerator::generateRoot(node, code, indentLevel);
            break;
        case Symbol::FunctionDecl:
            CodeGenerator::generateFunctionDecl(node, code, indentLevel);
            break;
        case Symbol::FunctionBody:
            CodeGenerator::generateFunctionBody(node, code, indentLevel);
            break;
        case Symbol::StructDecl:
            CodeGenerator::generateStructDecl(node, code, indentLevel);
            break;
        case Symbol::VarDecl:
            CodeGenerator::generateVarDecl(node, code, indentLevel);
            break;
        case Symbol::Assign:
            CodeGenerator::generateAssign(node, code, indentLevel);
            break;
        case Symbol::If:
            CodeGenerator::generateIf(node, code, indentLevel);
            break;
        case Symbol::Else:
            CodeGenerator::generateElse(node, code, indentLevel);
            break;
        case Symbol::While:
            CodeGenerator::generateWhile(node, code, indentLevel);
            break;
        case Symbol::Return:
            CodeGenerator::generateReturn(node, code, indentLevel);
            break;
        case Symbol::Label:
            CodeGenerator::generateLabel(node, code, indentLevel);
            break;
        default:
            // Fallback: process children
            for (const auto& child : node->getChildren()) {
                CodeGenerator::generateNode(child, code, indentLevel);
            }
            break;
    }
}

// ============================================================================
// Top-Level Declarations
// ============================================================================

void CodeGenerator::generateRoot(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    // Generate all top-level declarations
    for (const auto& child : node->getChildren()) {
        generateNode(child, code, indentLevel);
        code << "\n";
    }
}

void CodeGenerator::generateFunctionDecl(const Node::Ptr& node,
                                         std::stringstream& code,
                                         int& indentLevel) {
    if (!node) return;

    // Extract function components from AST
    std::string funcName;
    Node::Ptr params = nullptr;
    Node::Ptr body = nullptr;
    Node::Ptr typeNode = nullptr;
    Node::Ptr pointerNode = nullptr;

    for (auto& child : node->getChildren()) {
        switch (child->getType()) {
            case Symbol::Identifier: funcName = child->getToken()->getValue(); break;
            case Symbol::Parameters: params = child; break;
            case Symbol::FunctionBody: body = child; break;
            case Symbol::Type: typeNode = child; break;
            case Symbol::Pointer: pointerNode = child; break;
        }
    }

    // Generate return type (with pointer if applicable)
    std::string typeStr;
    if (pointerNode) typeStr = generateType(pointerNode);
    else typeStr = generateType(typeNode);

    code << getIndent(indentLevel) << typeStr << " (" << funcName << ")(";

    // Generate parameter list
    if (params) {
        auto paramChildren = params->getChildren();
        for (size_t i = 0; i < paramChildren.size(); i++) {
            if (i > 0) code << ", ";
            generateParameter(paramChildren[i], code);
        }
    }

    code << ")";

    // Generate function body or semicolon (for declarations)
    if (body) {
        code << " {\n";
        indentLevel++;
        generateNode(body, code, indentLevel);
        indentLevel--;
        code << getIndent(indentLevel) << "}\n";
    } else {
        code << ";\n";
    }
}

std::string CodeGenerator::generateType(const Node::Ptr& node) {
    if (!node) return "int";

    std::string result;
    Node::Ptr current = node;
    bool needParens = false;

    // Process pointer chain from innermost to outermost
    while (current) {
        if (current->getType() == Symbol::Pointer) {
            if (needParens) result = "(" + result + ")";
            result = "*" + result;
            needParens = true;
            current = !current->getChildren().empty() ? current->getChildren()[0] : nullptr;
        } else if (current->getType() == Symbol::Type) {
            if (!current->getChildren().empty())
                result += current->getChildren()[0]->getToken()->getValue();
            break;
        } else {
            break;
        }
    }

    return result;
}

void CodeGenerator::generateParameter(const Node::Ptr& node, std::stringstream& code) {
    std::string paramType = "int"; // Default type
    std::string paramName = "";
    
    // Extract parameter name
    for (const auto& child : node->getChildren()) {
        if (child->getType() == Symbol::Identifier) {
            paramName = child->getToken()->getValue();
        }
    }
    
    // Handle void parameter (no name)
    if (paramName.empty()) {
        code << "void";
    } else {
        code << paramType << " " << paramName;
    }
}

void CodeGenerator::generateFunctionBody(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    // Generate all statements in function body
    for (const auto& child : node->getChildren()) {
        generateNode(child, code, indentLevel);
    }
}

void CodeGenerator::generateStructDecl(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    auto children = node->getChildren();
    std::string structName = "";
    std::vector<Node::Ptr> members;
    Node::Ptr instanceVar = nullptr;
    
    // Extract struct components
    for (const auto& child : children) {
        if (child->getType() == Symbol::Identifier) {
            structName = child->getToken()->getValue();
        } else if (child->getType() == Symbol::Member) {
            members.push_back(child);
        } else if (child->getType() == Symbol::InstanceVar) {
            instanceVar = child;
        }
    }
    
    // Generate struct header
    code << getIndent(indentLevel) << "struct " << structName << " {\n";
    indentLevel++;
    
    // Generate member variables
    for (const auto& member : members) {
        generateMember(member, code, indentLevel);
    }
    
    indentLevel--;
    code << getIndent(indentLevel) << "}";
    
    // Generate optional instance variable
    if (instanceVar) {
        for (const auto& child : instanceVar->getChildren()) {
            if (child->getType() == Symbol::Identifier) {
                code << " " << child->getToken()->getValue();
            }
        }
    }
    
    code << ";\n";
}

void CodeGenerator::generateMember(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    code << getIndent(indentLevel) << "int "; // Default type
    
    // Extract member name
    for (const auto& child : node->getChildren()) {
        if (child->getType() == Symbol::Identifier) {
            code << child->getToken()->getValue();
        }
    }
    
    code << ";\n";
}

// ============================================================================
// Statements
// ============================================================================

void CodeGenerator::generateVarDecl(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    code << getIndent(indentLevel);
    
    std::string typeStr = "int"; // Default type
    std::string varName = "";
    int pointerCount = 0;
    
    auto children = node->getChildren();
    std::vector<std::string> identifiers;
    
    // Collect all identifiers and pointers
    for (const auto& child : children) {
        if (child->getType() == Symbol::Identifier) {
            identifiers.push_back(child->getToken()->getValue());
        } else if (child->getType() == Symbol::Pointer) {
            pointerCount++;
        }
    }
    
    // Parse type and variable name
    if (identifiers.size() >= 2) {
        // Multiple identifiers: first = type, last = variable name
        typeStr = "struct " + identifiers[0];
        varName = identifiers[identifiers.size() - 1];
    } else if (identifiers.size() == 1) {
        // Single identifier = variable name only
        varName = identifiers[0];
    }
    
    // Generate declaration with pointers
    code << typeStr;
    for (int i = 0; i < pointerCount; i++) {
        code << "*";
    }
    code << " " << varName << ";\n";
}

void CodeGenerator::generateAssign(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    code << getIndent(indentLevel);
    
    auto children = node->getChildren();
    if (children.size() >= 2) {
        code << CodeGenerator::generateExpression(children[0]);
        code << " = ";
        code << CodeGenerator::generateExpression(children[1]);
        code << ";\n";
    }
}

void CodeGenerator::generateIf(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    auto children = node->getChildren();
    
    // Generate if condition
    code << getIndent(indentLevel) << "if (";
    if (!children.empty()) {
        code << CodeGenerator::generateExpression(children[0]);
    }
    code << ") {\n";
    indentLevel++;
    
    // Generate if body and handle else clause
    for (size_t i = 1; i < children.size(); i++) {
        if (children[i]->getType() == Symbol::Else) {
            indentLevel--;
            code << getIndent(indentLevel) << "} ";
            generateNode(children[i], code, indentLevel);
            return;
        } else {
            generateNode(children[i], code, indentLevel);
        }
    }
    
    indentLevel--;
    code << getIndent(indentLevel) << "}\n";
}

void CodeGenerator::generateElse(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    auto children = node->getChildren();
    
    if (!children.empty() && children[0]->getType() == Symbol::If) {
        // Handle else if
        code << "else ";
        generateNode(children[0], code, indentLevel);
    } else {
        // Handle else
        code << "else {\n";
        indentLevel++;
        for (const auto& child : children) {
            generateNode(child, code, indentLevel);
        }
        indentLevel--;
        code << getIndent(indentLevel) << "}\n";
    }
}

void CodeGenerator::generateWhile(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    auto children = node->getChildren();
    
    // Generate while condition
    code << getIndent(indentLevel) << "while (";
    if (!children.empty()) {
        code << CodeGenerator::generateExpression(children[0]);
    }
    code << ") {\n";
    indentLevel++;
    
    // Generate while body
    for (size_t i = 1; i < children.size(); i++) {
        generateNode(children[i], code, indentLevel);
    }
    
    indentLevel--;
    code << getIndent(indentLevel) << "}\n";
}

void CodeGenerator::generateLabel(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    auto children = node->getChildren();
    
    if (!children.empty() && children[0]->getType() == Symbol::Identifier) {
        // Generate label
        if (indentLevel > 0) indentLevel--;
        code << getIndent(indentLevel) << children[0]->getToken()->getValue() << ":\n";
        indentLevel++;
        
        // Generate statements after label (e.g., break/continue)
        for (size_t i = 1; i < children.size(); i++) {
            if (children[i]->getType() == Symbol::Terminal) {
                code << getIndent(indentLevel) << children[i]->getToken()->getValue() << ";\n";
            } else {
                generateNode(children[i], code, indentLevel);
            }
        }
    }
}

void CodeGenerator::generateReturn(const Node::Ptr& node, std::stringstream& code, int& indentLevel) {
    code << getIndent(indentLevel) << "return";

    // Generate return expression if present
    for (const auto& child : node->getChildren()) {
        if (child->getType() != Symbol::Terminal) {
            code << " " << CodeGenerator::generateExpression(child);
        }
    }

    code << ";\n";
}

// ============================================================================
// Expressions
// ============================================================================

std::string CodeGenerator::generateExpression(const Node::Ptr& node) {
    if (!node) return "";
    
    // Dispatch to appropriate expression generator
    switch(node->getType()) {
        case Symbol::Identifier:
            return node->getToken()->getValue();
        case Symbol::Literal:
            return node->getToken()->getValue();
        case Symbol::MemberAccess:
            return CodeGenerator::generateMemberAccess(node);
        case Symbol::PointerMemberAccess:
            return CodeGenerator::generatePointerMemberAccess(node);
        case Symbol::Reference:
            return CodeGenerator::generateReference(node);
        case Symbol::Dereference:
            return CodeGenerator::generateDereference(node);
        case Symbol::BinaryOp:
            return CodeGenerator::generateBinaryOp(node);
        case Symbol::FunctionCall:
            return CodeGenerator::generateFunctionCall(node);
        case Symbol::Assign:
            return generateAssignExpression(node);
        default:
            return "";
    }
}

std::string CodeGenerator::generateAssignExpression(const Node::Ptr& node) {
    auto children = node->getChildren();
    if (children.size() >= 2) {
        return "(" + generateExpression(children[0]) + " = " + generateExpression(children[1]) + ")";
    }
    return "";
}

std::string CodeGenerator::generateMemberAccess(const Node::Ptr& node) {
    auto children = node->getChildren();
    if (children.size() >= 2) {
        return generateExpression(children[0]) + "." + generateExpression(children[1]);
    }
    return "";
}

std::string CodeGenerator::generatePointerMemberAccess(const Node::Ptr& node) {
    auto children = node->getChildren();
    if (children.size() >= 2) {
        return generateExpression(children[0]) + "->" + generateExpression(children[1]);
    }
    return "";
}

std::string CodeGenerator::generateReference(const Node::Ptr& node) {
    auto children = node->getChildren();
    if (!children.empty()) {
        return "&" + generateExpression(children[0]);
    }
    return "";
}

std::string CodeGenerator::generateDereference(const Node::Ptr& node) {
    auto children = node->getChildren();
    if (!children.empty()) {
        return "*" + generateExpression(children[0]);
    }
    return "";
}

std::string CodeGenerator::generateBinaryOp(const Node::Ptr& node) {
    auto children = node->getChildren();
    if (children.size() >= 2) {
        std::string op = "*"; // Default fallback operator

        // Extract operator from token if available
        if (node->getToken()) {
            op = node->getToken()->getValue();
        }

        return "(" + generateExpression(children[0]) + " " + op + " " + generateExpression(children[1]) + ")";
    }
    return "";
}

std::string CodeGenerator::generateFunctionCall(const Node::Ptr& node) {
    auto children = node->getChildren();
    std::string result = "";
    
    if (!children.empty()) {
        // Generate function name
        result = generateExpression(children[0]) + "(";
        
        // Generate arguments
        for (size_t i = 1; i < children.size(); i++) {
            if (i > 1) result += ", ";
            result += generateExpression(children[i]);
        }
        
        result += ")";
    }
    
    return result;
}

// ============================================================================
// Output
// ============================================================================

void CodeGenerator::printCode(const Node::Ptr& ast) {
    std::string code = CodeGenerator::generate(ast);
    std::cout << code << std::endl;
}