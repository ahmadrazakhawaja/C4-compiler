#include "prettyPrint.h"

#include <ostream>
#include <sstream>
#include <iostream>

namespace prettyPrint {

    static std::string label(const Node::Ptr& node, const Options& opt) {
        std::ostringstream ss;
        ss << node->getType();
        if (opt.showTokenValue && node->getToken().has_value()) {
            ss << "-" << node->getToken()->getValue();
        }
        return ss.str();
    }

    static void rec(const Node::Ptr& node, std::ostream& os, const Options& opt,
                    const std::string& prefix, bool isLast) {
        const char* tee   = opt.unicodeBranches ? "├── " : "|-- ";
        const char* elbow = opt.unicodeBranches ? "└── " : "`-- ";

        os << prefix << (isLast ? elbow : tee) << label(node, opt) << "\n";

        const auto& children = node->getChildren();
        if (children.empty()) return;

        std::string childPrefix = prefix + (opt.unicodeBranches
            ? (isLast ? "    " : "│   ")
            : (isLast ? "    " : "|   ")
        );

        for (size_t i = 0; i < children.size(); ++i) {
            rec(children[i], os, opt, childPrefix, i + 1 == children.size());
        }
    }

    void printTree(const Node::Ptr& root, std::ostream& os, const Options& opt) {
        os << label(root, opt) << "\n";
        const auto& children = root->getChildren();
        for (size_t i = 0; i < children.size(); ++i) {
            rec(children[i], os, opt, "", i + 1 == children.size());
        }
    }

    Node::Ptr reduceTree(const Node::Ptr& node) {
        if (!node) return nullptr;
        std::vector<Node::Ptr> newChildren;

        for (auto& child : node->getChildren()) {
            auto reducedChild = reduceTree(child);
            if (!reducedChild) continue;

            // skip punctuators
            if (auto tok = reducedChild->getToken()) {
                if (tok->getTokenType() == "punctuator") continue;
            }

            Symbol type = reducedChild->getType();

            // flatten declarator / directdec chains
            if (type == Symbol::declarator || type == Symbol::directdec) {
                for (auto& c : reducedChild->getChildren()) {
                    newChildren.push_back(c);
                }
                continue;
            }

            // flatten structtype Nodes: ersetze Node durch ihre Kinder
            if (reducedChild && reducedChild->getType() == Symbol::structtype) {
                for (auto& c : reducedChild->getChildren()) {
                    if (c) newChildren.push_back(c);
                }
                continue; // remove structtype node
            }

            // flatten extdec_ / extdec__ Nodes
            if (type == Symbol::extdec_ || type == Symbol::extdec__) {
                for (auto& c : reducedChild->getChildren()) {
                    newChildren.push_back(c);
                }
                continue;
            }

            // flatten compoundstatement / blockitemlist Chains
            if (type == Symbol::compoundstatement || type == Symbol::blockitemlist) {
                for (auto& c : reducedChild->getChildren()) {
                    if (c->getType() == Symbol::compoundstatement || c->getType() == Symbol::blockitemlist) {
                        for (auto& gc : c->getChildren()) newChildren.push_back(gc);
                    } else {
                        newChildren.push_back(c);
                    }
                }
                continue;
            }

            // merge selectstatement_ into selectstatement as else branch
            if (type == Symbol::selectstatement_ && !reducedChild->getChildren().empty()) {
                Node::Ptr elseBranch = nullptr;
                Node::Ptr elseToken = nullptr;

                if (reducedChild->getChildren().size() == 2) {
                    elseToken = reducedChild->getChildren()[0]; // terminal-else
                    elseBranch = reducedChild->getChildren()[1];
                } else if (reducedChild->getChildren().size() == 1) {
                    elseBranch = reducedChild->getChildren()[0];
                }

                // parent selectstatement Node should add else branch
                if (!newChildren.empty()) {
                    auto& parentIf = newChildren.back();
                    if (parentIf->getType() == Symbol::selectstatement) {
                        parentIf->setChildren({parentIf->getChildren()[0], elseBranch}); // [then, else]
                        continue; // selectstatement_ can be removed
                    }
                }
            }

            // default: add child
            newChildren.push_back(reducedChild);
        }

        node->setChildren(std::move(newChildren));

        // flatten node if not a semantical node
        Symbol ntype = node->getType();
        if (!node->getToken() &&
            ntype != Symbol::pointer &&
            ntype != Symbol::id &&
            ntype != Symbol::type &&
            ntype != Symbol::paramlist &&
            ntype != Symbol::paramdec &&
            ntype != Symbol::blockitemlist &&
            ntype != Symbol::compoundstatement &&
            ntype != Symbol::selectstatement &&
            ntype != Symbol::declarator &&
            ntype != Symbol::directdec &&
            ntype != Symbol::extdec &&
            ntype != Symbol::reference &&
            ntype != Symbol::dereference) {

            if (node->getChildren().empty()) return nullptr;
            if (node->getChildren().size() == 1) return node->getChildren()[0];
        }
        return node;
    }
   

    void analyzeSemantics(const Node::Ptr& root) {
        std::unordered_map<std::string, std::string> ids;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> structTable;

        // 1. collect struct defs
        std::cout << "Collecting struct definitions\n";
        collectStructDefinitions(root, structTable);

        // 2. collect declarations
        std::cout << "Collecting declarations\n";
        collectDeclarations(root, ids);

        // 3. run type analysis
        std::cout << "Starting type analysis\n";
        analyzeTypes(root, ids, structTable);

        std::cout << "Semantical Analysis successful\n";
    }


    void collectStructDefinitions(const Node::Ptr& node, 
                                std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& structTable) {
        if (!node) return;

        auto children = node->getChildren();

        // search for struct definitions in type node
        if (node->getType() == Symbol::type && children.size() >= 3) {
            auto firstChild = children[0];
            
            // check for terminal-struct
            if (firstChild && firstChild->getToken() && 
                firstChild->getToken()->getValue() == "struct") {
                
                // children[1] is the struct name (id-S)
                auto structNameNode = children[1];
                if (structNameNode && structNameNode->getToken()) {
                    std::string structName = structNameNode->getToken()->getValue();
                    
                    // children[2] is the dec with member
                    auto memberDecl = children[2];
                    if (memberDecl && memberDecl->getType() == Symbol::dec) {
                        auto memberChildren = memberDecl->getChildren();
                        if (memberChildren.size() >= 2) {
                            // memberChildren[0] is type with terminal-int
                            // memberChildren[1] is id-x
                            auto typeNode = memberChildren[0];
                            auto idNode = memberChildren[1];
                            
                            if (typeNode && typeNode->getChildren().size() > 0 && 
                                typeNode->getChildren()[0]->getToken() &&
                                idNode && idNode->getToken()) {
                                
                                std::string memberType = typeNode->getChildren()[0]->getToken()->getValue();
                                std::string memberName = idNode->getToken()->getValue();
                                
                                structTable[structName][memberName] = memberType;
                            }
                        }
                    }
                }
            }
        }

        // check children
        for (auto& child : children) {
            collectStructDefinitions(child, structTable);
        }
    }


    void collectDeclarations(const Node::Ptr& node,
                            std::unordered_map<std::string, std::string>& ids) {
        if (!node) return;

        auto children = node->getChildren();

        // dec or extdec
        if ((node->getType() == Symbol::dec || node->getType() == Symbol::extdec) &&
            children.size() >= 2) {

            Node::Ptr typeNode = children[0]; // type
            Node::Ptr declNode = children[1]; // id or dec_

            std::string baseType;
            std::string name;
            int pointerDepth = 0;

            // get base type
            if (typeNode && typeNode->getChildren().size() > 0) {
                auto terminalNode = typeNode->getChildren()[0];
                if (terminalNode && terminalNode->getToken()) {
                    if (terminalNode->getToken()->getValue() == "struct") {
                        // struct
                        if (typeNode->getChildren().size() > 1 && 
                            typeNode->getChildren()[1]->getToken()) {
                            baseType = "struct " + typeNode->getChildren()[1]->getToken()->getValue();
                        }
                    } else {
                        // int, char, void
                        baseType = terminalNode->getToken()->getValue();
                    }
                }
            }

            // normal: int a;
            if (declNode->getType() == Symbol::id && declNode->getToken()) {
                name = declNode->getToken()->getValue();
            }
            // pointer: int *p; or struct S *p;
            else if (declNode->getType() == Symbol::dec_) {
                // count pointer nodes
                Node::Ptr current = declNode->getChildren()[0];
                while (current && current->getType() == Symbol::pointer) {
                    pointerDepth++;
                    if (!current->getChildren().empty())
                        current = current->getChildren()[0];
                    else
                        break;
                }
                
                // last child is id
                auto idNode = declNode->getChildren().back();
                if (idNode && idNode->getToken())
                    name = idNode->getToken()->getValue();
            }

            if (!name.empty() && !baseType.empty()) {
                std::string fullType = baseType;
                for (int i = 0; i < pointerDepth; ++i)
                    fullType += "*";

                ids[name] = fullType;
            }
        }

        for (auto& c : children)
            collectDeclarations(c, ids);
    }


    void analyzeTypes(const Node::Ptr& node, 
                    const std::unordered_map<std::string, std::string>& ids,
                    const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& structTable) {
        if (!node) return;
        auto children = node->getChildren();

        switch (node->getType()) {
            case Symbol::assignment: {
                if (children.size() == 2) {
                    std::string leftType  = getNodeDataType(children[0], ids, structTable);
                    std::string rightType = getNodeDataType(children[1], ids, structTable);
                    
                    if (leftType == "unknown") {
                        reportError(node, "assignment to undeclared identifier");
                    } else if (leftType != rightType) {
                        reportError(node, "type mismatch in assignment");
                    }
                }
                break;
            }

            case Symbol::selectstatement: {  // if
                if (children.size() >= 2) {
                    std::string condType = getNodeDataType(children[1], ids, structTable);
                    if (condType.starts_with("struct ")) {
                        reportError(children[0], "condition must have scalar type");
                    }
                }
                break;
            }

            case Symbol::iterstatement: {  // while
                if (children.size() >= 2) {
                    std::string condType = getNodeDataType(children[1], ids, structTable);
                    if (condType.starts_with("struct ")) {
                        reportError(children[0], "condition must have scalar type");
                    }
                }
                break;
            }

            case Symbol::sum:
            case Symbol::difference: {
                if (children.size() == 2) {
                    std::string leftType  = getNodeDataType(children[0], ids, structTable);
                    std::string rightType = getNodeDataType(children[1], ids, structTable);
                    
                    bool leftIsPointer = leftType.ends_with("*");
                    bool rightIsPointer = rightType.ends_with("*");
                    
                    if (node->getType() == Symbol::sum && leftIsPointer && rightIsPointer) {
                        reportError(node, "invalid operands to binary +");
                    }
                }
                break;
            }

            case Symbol::product: {
                if (children.size() == 2) {
                    std::string leftType  = getNodeDataType(children[0], ids, structTable);
                    std::string rightType = getNodeDataType(children[1], ids, structTable);
                    
                    if (leftType.ends_with("*") || rightType.ends_with("*")) {
                        reportError(node, "invalid operands to binary *");
                    }
                }
                break;
            }

            case Symbol::disjunction:
            case Symbol::conjunction: {
                if (children.size() >= 2) {
                    for (size_t i = 0; i < children.size(); ++i) {
                        std::string opType = getNodeDataType(children[i], ids, structTable);
                        if (opType.starts_with("struct ")) {
                            reportError(node, "operand must have scalar type");
                            break;
                        }
                    }
                }
                break;
            }
        }

        for (auto& child : children) {
            analyzeTypes(child, ids, structTable);
        }
    }


    void reportError(const Node::Ptr& node, const std::string& message) {
        // find first terminal
        auto terminal = findFirstTerminal(node);
        if (terminal && terminal->getToken()) {
            std::cerr << terminal->getToken()->getSourceLine() << ":" 
                    << terminal->getToken()->getSourceIndex() 
                    << ": error: " << message << "\n";
        } else {
            std::cerr << "error: " << message << "\n";
        }
    }
    

    Node::Ptr buildAST(const Node::Ptr& node) {
        if (!node) return nullptr;

        Node::Ptr astNode = std::make_shared<Node>();
        astNode->setType(node->getType());

        switch (node->getType()) {

        // ---------------- TYPES & DECLARATORS ----------------

        case Symbol::type: {
            astNode->setType(Symbol::Type);
            break;
        }

        case Symbol::dec_: {
            // Wir bauen den Deklarator MANUELL
            Node::Ptr current = nullptr;

            for (const auto& child : node->getChildren()) {
                Node::Ptr c = buildAST(child);
                if (!c) continue;

                if (c->getType() == Symbol::pointer) {
                    if (!current) {
                        current = c;
                    } else {
                        c->addChild(current);
                        current = c;
                    }
                } else {
                    // Identifier oder FunctionDeclarator
                    if (!current) {
                        current = c;
                    } else {
                        // hänge Identifier ganz unten an
                        Node::Ptr bottom = current;
                        while (!bottom->getChildren().empty())
                            bottom = bottom->getChildren()[0];
                        bottom->addChild(c);
                    }
                }
            }
            return current;
        }

        case Symbol::pointer: {
            astNode->setType(Symbol::Pointer);
            break;
        }

        case Symbol::id: {
            astNode->setType(Symbol::Identifier);
            astNode->setToken(node->getToken());
            return astNode;
        }

        // ---------------- LITERALS ----------------

        case Symbol::decimalconst:
        case Symbol::stringliteral: {
            astNode->setType(Symbol::Literal);
            astNode->setToken(node->getToken());
            return astNode;
        }

        // ---------------- TERMINALS ----------------

        case Symbol::terminal: {
            const auto& v = node->getToken()->getValue();

            // fix terminal words 
            if (v == "if" || v == "else" || v == "while" || v == "{"
                || v == "}" || v == ";" || v == "goto" || v == "return")
                return nullptr;

            astNode->setType(Symbol::Terminal);
            astNode->setToken(node->getToken());
            return astNode;
        }

        // ---------------- EXPRESSIONS ----------------

        case Symbol::product:
            astNode->setType(Symbol::BinaryOp);
            astNode->setToken(Token("*"));
            break;

        case Symbol::difference:
            astNode->setType(Symbol::BinaryOp);
            astNode->setToken(Token("-"));
            break;

        case Symbol::sum:
            astNode->setType(Symbol::BinaryOp);
            astNode->setToken(Token("+"));
            break;

        case Symbol::comparison:
            astNode->setType(Symbol::BinaryOp);
            astNode->setToken(Token("<"));
            break;

        case Symbol::equality:
            astNode->setType(Symbol::BinaryOp);
            astNode->setToken(Token("=="));
            break;

        case Symbol::disjunction:
            astNode->setType(Symbol::BinaryOp);
            astNode->setToken(Token("||"));
            break;

        case Symbol::conjunction:                 
            astNode->setType(Symbol::BinaryOp);
            astNode->setToken(Token("&&"));
            break;

        case Symbol::assignment:
            astNode->setType(Symbol::Assign);
            break;

        case Symbol::dereference:
            astNode->setType(Symbol::Dereference);
            break;

        case Symbol::reference:
            astNode->setType(Symbol::Reference);
            break;

        case Symbol::memberaccess:
            astNode->setType(Symbol::MemberAccess);
            break;

        case Symbol::pointermemberaccess:
            astNode->setType(Symbol::PointerMemberAccess);
            break;

        case Symbol::functioncall:
            astNode->setType(Symbol::FunctionCall);
            break;

        // ---------------- STATEMENTS ----------------
        case Symbol::statement:
            astNode->setType(Symbol::FunctionBody);
            break;

        case Symbol::selectstatement:
            astNode->setType(Symbol::If);
            break;

        case Symbol::selectstatement_:
            astNode->setType(Symbol::Else);
            break;

        case Symbol::iterstatement:
            astNode->setType(Symbol::While);
            break;

        case Symbol::labelstatement:
            astNode->setType(Symbol::Label);
            break;

        case Symbol::jumpstatement: {
            auto kw = node->getChildren()[0]->getToken()->getValue();
            if (kw == "return") astNode->setType(Symbol::Return);
            else if (kw == "goto") astNode->setType(Symbol::Goto);
            else if (kw == "break") astNode->setType(Symbol::Break);
            else if (kw == "continue") astNode->setType(Symbol::Continue);
            break;
        }

        // ---------------- FUNCTIONS ----------------

        case Symbol::paramlist:
            astNode->setType(Symbol::Parameters);
            break;

        case Symbol::paramdec:
            astNode->setType(Symbol::Parameter);
            break;

        case Symbol::funcdef_:
            astNode->setType(Symbol::FunctionBody);
            break;

        case Symbol::extdec:
            astNode->setType(Symbol::FunctionDecl);
            break;

        case Symbol::transunit:
            astNode->setType(Symbol::Root);
            break;

        default:
            break;
        }

        // ---------------- CHILD RECURSION ----------------

        for (const auto& child : node->getChildren()) {
            Node::Ptr astChild = buildAST(child);
            if (!astChild)
                continue;

            if (astChild->getType() == Symbol::Pointer) {
                if (!astNode->getChildren().empty()) {
                    Node::Ptr last = astNode->popLastChild();
                    astChild->addChild(last);
                }
                astNode->addChild(astChild);
            } else {
                astNode->addChild(astChild);
            }
        }
        return astNode;
    }


    std::string getNodeDataType(const Node::Ptr& node,
                                const std::unordered_map<std::string, std::string>& ids,
                                const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& structTable)
    {
        if (!node) return "unknown";

        Symbol type = node->getType();

        switch (type) {
            case Symbol::id: {
                std::string name = node->getToken() ? node->getToken()->getValue() : "id";
                auto it = ids.find(name);
                if (it != ids.end()) return it->second;
                return "unknown";
            }

            case Symbol::reference: {
                // &x → if x is of type T, then &x if type T*
                if (!node->getChildren().empty()) {
                    std::string childType = getNodeDataType(node->getChildren()[0], ids, structTable);
                    if (childType != "unknown") {
                        return childType + "*";
                    }
                }
                return "unknown";
            }

            case Symbol::dereference: {
                // *p → if p of type T*, then *p is type T
                if (!node->getChildren().empty()) {
                    std::string childType = getNodeDataType(node->getChildren()[0], ids, structTable);
                    if (childType != "unknown" && childType.ends_with("*")) {
                        return childType.substr(0, childType.length() - 1);
                    }
                }
                return "unknown";
            }

            case Symbol::memberaccess: {
                if (node->getChildren().size() > 1) {
                    std::string objType = getNodeDataType(node->getChildren()[0], ids, structTable);
                    auto memberNode = node->getChildren()[1];
                    if (memberNode && memberNode->getToken()) {
                        std::string memberName = memberNode->getToken()->getValue();
                        
                        // "struct S" → lookup
                        if (objType.starts_with("struct ")) {
                            std::string structName = objType.substr(7);
                            auto it = structTable.find(structName);
                            if (it != structTable.end()) {
                                auto memberIt = it->second.find(memberName);
                                if (memberIt != it->second.end()) {
                                    return memberIt->second;
                                }
                            }
                        }
                    }
                }
                return "unknown";
            }

            case Symbol::pointermemberaccess: {
                if (node->getChildren().size() > 1) {
                    auto memberNode = node->getChildren()[1];
                    if (memberNode && memberNode->getToken()) {
                        std::string memberName = memberNode->getToken()->getValue();
                        auto it = ids.find(memberName);
                        if (it != ids.end()) return it->second;
                    }
                }
                return "unknown";
            }

            case Symbol::decimalconst:
                return "int";

            case Symbol::stringliteral:
                return "char*";

            case Symbol::product:
            case Symbol::difference:
            case Symbol::sum: {
                if (!node->getChildren().empty()) {
                    return getNodeDataType(node->getChildren()[0], ids, structTable);
                }
                return "int";
            }

            case Symbol::assignment: {
                if (!node->getChildren().empty()) {
                    return getNodeDataType(node->getChildren()[0], ids, structTable);
                }
                return "unknown";
            }

            case Symbol::type: {
                if (!node->getChildren().empty() && node->getChildren()[0]->getToken())
                    return node->getChildren()[0]->getToken()->getValue();
                return "unknown";
            }

            default:
                return "unknown";
        }
    }

    Node::Ptr findFirstTerminal(const Node::Ptr& node) {
        if (!node) return nullptr;
        
        // if node has terminal return that
        if (node->getToken()) {
            return node;
        }
        
        // check children
        for (auto& child : node->getChildren()) {
            auto terminal = findFirstTerminal(child);
            if (terminal) return terminal;
        }
        
        return nullptr;
    }

    void printForest(const std::vector<Node::Ptr>& roots, std::ostream& os, const Options& opt) {
        if (roots.empty()) {
            os << "(forest empty)\n";
            return;
        }
        for (size_t i = 0; i < roots.size(); ++i) {
            os << "Root[" << i << "]\n";
            printTree(roots[i], os, opt);
            if (i + 1 < roots.size()) os << "\n";
        }
    }
}
