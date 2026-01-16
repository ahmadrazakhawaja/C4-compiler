#include "Parser.h"
#include <iostream>
#include <vector>
#include <exception>

#include "../helper/structs/Node.h"
#include "../helper/structs/Token.h"
#include "../helper/Symbol.h"
#include <assert.h>
#include "../helper/Utils.h"
#include "../lexer/Tokenizer.h"
#include "../prettyPrint/prettyPrint.h"
#include "../ast/Ast.h"
#include "../semantic/Semantic.h"

// -------------------------
// Debug helpers (shallow)
// -------------------------
static void print_shallow(const Node::Ptr& node) {
    std::cout << node->getType();
    if (node->getToken().has_value()) {
        std::cout << "-" << node->getToken()->getValue();
    }
}

// (kept from you: production print)
static void print_production(const Node::Ptr& node) {
    print_shallow(node);
    const auto& kids = node->getChildren();
    if (kids.empty()) return;

    std::cout << " $";
    bool first = true;
    for (const auto& child : kids) {
        if (!first) std::cout << " ";
        first = false;
        print_shallow(child);
    }
    std::cout << "$";
}

// -------------------------
// Constructor
// -------------------------
Parser::Parser(std::vector<Token> tokens, bool verbose, std::string fileName)
    : parseTreeRoot(Node::make(start)), isVerbose(verbose) {
    parseFileName = fileName.empty() ? "unknown" : std::move(fileName);
    while (!tokens.empty()) {
        remTokens.push_back(tokens.back());
        tokens.pop_back();
    }
    remSymbols.push_back(parseTreeRoot);
}

// -------------------------
// Lookahead
// -------------------------
Node::Ptr Parser::peekSymbol(int k) {
    return remSymbols.at(remSymbols.size() - k - 1);
}

Token Parser::peek(int k) {
    if (isVerbose) {
        std::cout << "\t\tPeeked at " << remTokens.at(remTokens.size()-k-1) << " using k=" << k << "\n";
    }
    return remTokens.at(remTokens.size() - k - 1);
}

// -------------------------
// Static run
// -------------------------
bool Parser::run(const std::string& fileName, const std::string& path, bool isVerbose) {
    std::string sourceCode;
    try {
        sourceCode = Utils::readSourceCode(path);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return false;
    }
    sourceCode += '\0';
    auto sequence = Tokenizer::tokenizeSeq(sourceCode, false);

    if (sequence.second.has_value()) {
        const auto& err = *sequence.second;
        std::cerr << fileName << ":" << err.line + 1 << ":" << err.column + 1
                  << ": error: " << err.message << std::endl;
        return false;
    }

    std::vector<Token> tokens = sequence.first;
    Parser parser(tokens, isVerbose, fileName);

    if (!parser.parse()) {
        auto astTree = ast::buildFromParseTree(parser.getParseTreeRoot());
        if (!semantic::analyze(astTree, std::cerr, fileName)) return false;

        std::cout << "Successfully parsed " << fileName << "\n";
        prettyPrint::Options opt;
        opt.unicodeBranches = false;
        opt.showTokenValue = true;

        std::cout << "\n=== Parse Tree ===\n";
        prettyPrint::printTree(parser.getParseTreeRoot(), std::cout, opt);
        return true;
    }
    return false;
}

// -------------------------
// dump_state
// -------------------------
void Parser::dump_state() {
    std::cout << "remTokens\n";
    for (int i = (int)remTokens.size() - 1; i >= 0; --i) {
        std::cout << remTokens.at(i) << "\n";
    }

    std::cout << "remSymbols\n";
    for (int i = (int)remSymbols.size() - 1; i >= 0; --i) {
        print_shallow(remSymbols.at(i));
        std::cout << "\n";
    }
}

// -------------------------
// Expression parsing helpers
// -------------------------
static int opPrec(Symbol symbol) {
    switch(symbol) {
        case arrayaccess:
        case functioncall:
        case memberaccess:
        case pointermemberaccess: return 9;
        case postincrement:
        case postdecrement: return 9;

        case reference:
        case dereference:
        case negationarithmetic:
        case negationlogical:
        case sizeoperator:
        case preincrement:
        case predecrement: return 8;

        case product: return 7;

        case sum:
        case difference: return 6;

        case comparison: return 5;

        case equality:
        case inequality: return 4;

        case conjunction: return 3;
        case disjunction: return 2;

        case ternary: return 1;
        case assignment: return 0;

        default:
            assert(!"nothing else has operator precedence so this shouldn't be reachable");
            return -1;
    }
}

static bool isRightAssociative(Symbol symbol) {
    switch(symbol) {
        case reference:
        case dereference:
        case negationarithmetic:
        case negationlogical:
        case sizeoperator:
        case preincrement:
        case predecrement:
        case ternary:
        case assignment:
            return true;
        default:
            return false;
    }
}

static std::optional<Symbol> toSymbol(const std::string& str, bool isExpectArg) {
    if (str == "[") return isExpectArg ? std::nullopt : std::make_optional(arrayaccess);
    if (str == "(") return isExpectArg ? std::make_optional(parenthesizedexpr) : std::make_optional(functioncall);
    if (str == ".") return isExpectArg ? std::nullopt : std::make_optional(memberaccess);
    if (str == "->") return isExpectArg ? std::nullopt : std::make_optional(pointermemberaccess);
    if (str == "&") return isExpectArg ? std::make_optional(reference) : std::nullopt;
    if (str == "*") return isExpectArg ? std::make_optional(dereference) : std::make_optional(product);
    if (str == "++") return isExpectArg ? std::make_optional(preincrement) : std::make_optional(postincrement);
    if (str == "--") return isExpectArg ? std::make_optional(predecrement) : std::make_optional(postdecrement);
    if (str == "-") return isExpectArg ? std::make_optional(negationarithmetic) : std::make_optional(difference);
    if (str == "!") return isExpectArg ? std::make_optional(negationlogical) : std::nullopt;
    if (str == "sizeof") return isExpectArg ? std::make_optional(sizeoperator) : std::nullopt;
    if (str == "+") return isExpectArg ? std::nullopt : std::make_optional(sum);
    if (str == "<") return isExpectArg ? std::nullopt : std::make_optional(comparison);
    if (str == "==") return isExpectArg ? std::nullopt : std::make_optional(equality);
    if (str == "!=") return isExpectArg ? std::nullopt : std::make_optional(inequality);
    if (str == "&&") return isExpectArg ? std::nullopt : std::make_optional(conjunction);
    if (str == "||") return isExpectArg ? std::nullopt : std::make_optional(disjunction);
    if (str == "?") return isExpectArg ? std::nullopt : std::make_optional(ternary);
    if (str == "=") return isExpectArg ? std::nullopt : std::make_optional(assignment);

    return std::nullopt;
}

struct OpEntry {
    Symbol op;
    Token tok;
};

// Reduce now uses Node::Ptr stacks
static int reduce(std::vector<OpEntry>& opStack, std::vector<Node::Ptr>& argStack) {
    if (opStack.empty()) return 1;

    OpEntry entry = opStack.back();
    opStack.pop_back();

    auto node = Node::make(entry.op, entry.tok);
    int argStackSize = (int)argStack.size();

    switch(entry.op) {
        case ternary: {
            if (argStackSize < 3) return 1;
            node->addChild(argStack.at(argStackSize - 3));
            node->addChild(argStack.at(argStackSize - 2));
            node->addChild(argStack.at(argStackSize - 1));
            argStack.pop_back(); argStack.pop_back(); argStack.pop_back();
            argStack.push_back(node);
            return 0;
        }
        case reference:
        case dereference:
        case negationarithmetic:
        case negationlogical:
        case sizeoperator:
        case preincrement:
        case predecrement:
        case postincrement:
        case postdecrement: {
            if (argStackSize < 1) return 1;
            node->addChild(argStack.at(argStackSize - 1));
            argStack.pop_back();
            argStack.push_back(node);
            return 0;
        }
        default: {
            if (argStackSize < 2) return 1;
            node->addChild(argStack.at(argStackSize - 2));
            node->addChild(argStack.at(argStackSize - 1));
            argStack.pop_back(); argStack.pop_back();
            argStack.push_back(node);
            return 0;
        }
    }
}

Token Parser::peekExpr(int k) {
    if (isVerbose) {
        std::cout << "\t\t\tExprPeeked at "
                  << remTokens.at(remTokens.size() - k - 1 - remTokensExpressionIndex)
                  << " using k=" << k
                  << " & remTokensExpressionIndex=" << remTokensExpressionIndex << "\n";
    }
    return remTokens.at(remTokens.size() - k - 1 - remTokensExpressionIndex);
}

// Overloads
std::optional<Node::Ptr> Parser::evilShuntingYard(std::string limit, bool isOutermost) {
    return evilShuntingYard(limit, limit, isOutermost);
}

std::optional<Node::Ptr> Parser::evilShuntingYard(std::string limit, std::string limit2, bool isOutermost) {
    std::vector<OpEntry> opStack;
    std::vector<Node::Ptr> argStack;
    bool isExpectArg = true;
    auto noteError = [&](const Token& tok) {
        if (!errorToken.has_value()) {
            errorToken = tok;
        }
    };

    while (true) {
        Token tok = peekExpr(0);

        if (tok.getValue() == limit || tok.getValue() == limit2) {
            if (!isOutermost) {
                remTokensExpressionIndex++;
                remRevExprSymbols.push_back(Node::makeTerminal(tok.getValue()));
            }
            break;
        }
        if (tok.getValue() == "EOF") {
            noteError(tok);
            return std::nullopt;
        }

        // literals / identifiers
        if (tok.getTokenType() == "string-literal") {
            if (!isExpectArg) {
                noteError(tok);
                return std::nullopt;
            }
            remTokensExpressionIndex++;
            remRevExprSymbols.push_back(Node::make(stringliteral));
            argStack.push_back(Node::make(stringliteral, tok));
            isExpectArg = false;
            continue;
        } else if (tok.getTokenType() == "character-constant") {
            if (!isExpectArg) {
                noteError(tok);
                return std::nullopt;
            }
            remTokensExpressionIndex++;
            remRevExprSymbols.push_back(Node::make(charconst));
            argStack.push_back(Node::make(charconst, tok));
            isExpectArg = false;
            continue;
        } else if (tok.getTokenType() == "decimal-constant") {
            if (!isExpectArg) {
                noteError(tok);
                return std::nullopt;
            }
            remTokensExpressionIndex++;
            remRevExprSymbols.push_back(Node::make(decimalconst));
            argStack.push_back(Node::make(decimalconst, tok));
            isExpectArg = false;
            continue;
        } else if (tok.getTokenType() == "identifier") {
            if (!isExpectArg) {
                noteError(tok);
                return std::nullopt;
            }
            remTokensExpressionIndex++;
            remRevExprSymbols.push_back(Node::make(id));
            argStack.push_back(Node::make(id, tok));
            isExpectArg = false;
            continue;
        }

        // sizeof(type) special case (kept structurally similar)
        if (tok.getValue() == "sizeof") {
            if (peekExpr(1).getValue() == "(") {
                std::string t2 = peekExpr(2).getValue();
                if (t2 == "void" || t2 == "char" || t2 == "int" || t2 == "struct") {
                    // consume sizeof
                    remTokensExpressionIndex++;
                    remRevExprSymbols.push_back(Node::makeTerminal("sizeof"));
                    // consume "("
                    remTokensExpressionIndex++;
                    remRevExprSymbols.push_back(Node::makeTerminal("("));

                    auto typeNode = Node::make(type);
                    remRevExprSymbols.push_back(typeNode);

                    auto sizeNode = Node::make(sizeoperator);
                    sizeNode->addChild(typeNode);
                    argStack.push_back(sizeNode);

                    isExpectArg = true;

                    int open = 1;
                    while (open > 0) {
                        remTokensExpressionIndex++;
                        Token t = peekExpr(0);
                        if (t.getValue() == "(") open++;
                        else if (t.getValue() == ")") open--;
                        else if (t.getValue() == "EOF") {
                            noteError(t);
                            return std::nullopt;
                        }
                    }

                    // consume ")"
                    remTokensExpressionIndex++;
                    remRevExprSymbols.push_back(Node::makeTerminal(")"));
                    continue;
                }
            }
        }

        // operators
        auto maybeOp = toSymbol(tok.getValue(), isExpectArg);
        if (!maybeOp.has_value()) {
            noteError(tok);
            return std::nullopt;
        }

        remTokensExpressionIndex++;
        remRevExprSymbols.push_back(Node::makeTerminal(tok.getValue()));

        Symbol op = *maybeOp;
        Token opTok = tok;
        if (op == functioncall || op == arrayaccess || op == parenthesizedexpr ||
            op == postincrement || op == postdecrement) {
            isExpectArg = false;
        } else {
            isExpectArg = true;
        }

        while (op != parenthesizedexpr && !opStack.empty() &&
               (opPrec(op) < opPrec(opStack.back().op) ||
                (!isRightAssociative(op) && opPrec(op) == opPrec(opStack.back().op)))) {
            if (reduce(opStack, argStack)) {
                noteError(tok);
                return std::nullopt;
            }
        }

        if (op == parenthesizedexpr) {
            auto res = evilShuntingYard(")", false);
            if (!res.has_value()) return std::nullopt;
            auto n = Node::make(op, opTok);
            n->addChild(res.value());
            argStack.push_back(n);
            continue;
        }

        if (op == arrayaccess) {
            auto res = evilShuntingYard("]", false);
            if (!res.has_value()) return std::nullopt;
            argStack.push_back(res.value());
            opStack.push_back(OpEntry{op, opTok});
            continue;
        }

        if (op == functioncall) {
            if (peekExpr(0).getValue() == ")") {
                remTokensExpressionIndex++;
                remRevExprSymbols.push_back(Node::makeTerminal(")"));
                auto n = Node::make(op, opTok);
                n->addChild(argStack.back());
                argStack.push_back(n);
                continue;
            }

            int args = 0;
            while (true) {
                args++;
                auto res = evilShuntingYard(")", ",", true);
                if (!res.has_value()) {
                    noteError(peekExpr(0));
                    return std::nullopt;
                }

                if (peekExpr(0).getValue() == ")") {
                    remTokensExpressionIndex++;
                    remRevExprSymbols.push_back(Node::makeTerminal(")"));
                    argStack.push_back(res.value());
                    break;
                } else if (peekExpr(0).getValue() == ",") {
                    remTokensExpressionIndex++;
                    remRevExprSymbols.push_back(Node::makeTerminal(","));
                    argStack.push_back(res.value());
                    continue;
                } else {
                    std::abort();
                }
            }

            auto n = Node::make(op, opTok);
            int total = (int)argStack.size();
            for (int i = 0; i <= args; i++) { // includes function name
                n->addChild(argStack.at(total - 1 - args + i));
            }
            for (int i = 0; i <= args; i++) argStack.pop_back();
            argStack.push_back(n);
            continue;
        }

        if (op == ternary) {
            auto res = evilShuntingYard(":", false);
            if (!res.has_value()) {
                noteError(peekExpr(0));
                return std::nullopt;
            }
            argStack.push_back(res.value());
            opStack.push_back(OpEntry{op, opTok});
            continue;
        }

        opStack.push_back(OpEntry{op, opTok});
    }

    while (!opStack.empty()) {
        if (reduce(opStack, argStack)) {
            noteError(peekExpr(0));
            return std::nullopt;
        }
    }

    if (opStack.empty() && argStack.size() == 1) return argStack.back();
    return std::nullopt;
}

// -------------------------
// parse()
// -------------------------
int Parser::parse() {
    errorToken.reset();
    while (!remSymbols.empty() && !remTokens.empty()) {

        // expression handling
        if (remSymbols.back()->getType() == expr) {
            Node::Ptr exprNode = remSymbols.back();

            assert(peekSymbol(1)->getToken().has_value()); // follow token
            remSymbols.pop_back();

            remTokensExpressionIndex = 0;
            remRevExprSymbols.clear();

            auto res = evilShuntingYard(peekSymbol(0)->getToken()->getValue(), true);

            // push back reverse expr symbols onto symbol stack
            while (!remRevExprSymbols.empty()) {
                remSymbols.push_back(remRevExprSymbols.back());
                remRevExprSymbols.pop_back();
            }

            if (!res.has_value()) {
                const Token& next = errorToken.has_value() ? *errorToken : remTokens.back();
                std::cerr << parseFileName << ":" << next.getSourceLine() + 1
                          << ":" << next.getSourceIndex() + 1
                          << ": error: parse error\n";
                dump_state();
                return 1;
            }

            exprNode->addChild(res.value());

            if (isVerbose) std::cout << "Expression parsed successfully\n";
            continue;
        }

        auto changedNode = parseSymbol();
        if (!changedNode.has_value()) {
            const Token& next = remTokens.back();
            if (!errorToken.has_value()) {
                errorToken = next;
            }
            std::cerr << parseFileName << ":" << next.getSourceLine() + 1
                      << ":" << next.getSourceIndex() + 1
                      << ": error: parse error\n";
            dump_state();
            return 1;
        }

        if (isVerbose) {
            std::cout << "\t";
            print_production(changedNode.value());
            std::cout << "\n";
        }

        remSymbols.pop_back();

        const auto& kids = changedNode.value()->getChildren();
        for (int i = (int)kids.size() - 1; i >= 0; --i) {
            remSymbols.push_back(kids.at(i));
        }
    }

    std::optional<Token> eofToken;
    if (!remTokens.empty() && remTokens.back().getTokenType() == "EOF") {
        eofToken = remTokens.back();
        remTokens.pop_back();
    }

    if (remSymbols.empty() && remTokens.empty()) return 0;

    if (errorToken.has_value()) {
        const Token& next = *errorToken;
        std::cerr << parseFileName << ":" << next.getSourceLine() + 1
                  << ":" << next.getSourceIndex() + 1
                  << ": error: parse error\n";
    } else if (eofToken.has_value()) {
        const Token& next = *eofToken;
        std::cerr << parseFileName << ":" << next.getSourceLine() + 1
                  << ":" << next.getSourceIndex() + 1
                  << ": error: parse error\n";
    } else {
        std::cerr << parseFileName << ": error: parse error\n";
    }
    dump_state();
    return 1;
}

// -------------------------
// parseSymbol() (shared_ptr)
// -------------------------
std::optional<Node::Ptr> Parser::parseSymbol() {
    Node::Ptr symbol = remSymbols.back();
    Token next = peek(0);

    switch (symbol->getType()) {
        case expr:
            assert(!"Should not occur here");
            std::abort();

        case terminal: {
            // terminal nodes carry a token/value to match
            assert(symbol->getToken().has_value());
            if (symbol->getToken()->getValue() == next.getValue()) {
                symbol->setToken(next);
                remTokens.pop_back();
                return symbol;
            }
            return std::nullopt;
        }

        case stringliteral:
            if (next.getTokenType() == "string-literal") {
                symbol->setToken(next);
                remTokens.pop_back();
                return symbol;
            }
            return std::nullopt;

        case charconst:
            if (next.getTokenType() == "character-constant") {
                symbol->setToken(next);
                remTokens.pop_back();
                return symbol;
            }
            return std::nullopt;

        case decimalconst:
            if (next.getTokenType() == "decimal-constant") {
                symbol->setToken(next);
                remTokens.pop_back();
                return symbol;
            }
            return std::nullopt;

        case id:
            if (next.getTokenType() == "identifier") {
                symbol->setToken(next);
                remTokens.pop_back();
                return symbol;
            }
            return std::nullopt;

        case start:
            symbol->addChild(transunit);
            return symbol;

        case transunit:
            symbol->addChild(extdec);
            symbol->addChild(transunit_);
            return symbol;

        case transunit_:
            if (next.getValue() == "EOF") return symbol;
            symbol->addChild(extdec);
            symbol->addChild(transunit_);
            return symbol;

        case extdec:
            symbol->addChild(type);
            symbol->addChild(extdec_);
            return symbol;

        case extdec_:
            if (next.getValue() == ";") {
                symbol->addChild(decEnd);
                return symbol;
            }
            symbol->addChild(declarator);
            symbol->addChild(extdec__);
            return symbol;

        case extdec__:
            if (next.getValue() == ";") {
                symbol->addChild(decEnd);
                return symbol;
            }
            symbol->addChild(funcdef_);
            return symbol;

        case decEnd:
            symbol->addChild(";");
            return symbol;

        case declarator:
            if (next.getValue() == "*") {
                symbol->addChild(pointer);
                symbol->addChild(directdec);
                return symbol;
            }
            symbol->addChild(directdec);
            return symbol;

        case pointer:
            symbol->addChild("*");
            symbol->addChild(pointer_);
            return symbol;

        case pointer_:
            if (next.getValue() == "*") {
                symbol->addChild("*");
                symbol->addChild(pointer_);
            }
            return symbol;

        case type:
            if (next.getValue() == "void") { symbol->addChild("void"); return symbol; }
            if (next.getValue() == "char") { symbol->addChild("char"); return symbol; }
            if (next.getValue() == "int")  { symbol->addChild("int");  return symbol; }
            if (next.getValue() == "struct") { symbol->addChild(structtype); return symbol; }
            return std::nullopt;

        case structtype:
            if (peek(1).getValue() == "{") {
                symbol->addChild("struct");
                symbol->addChild("{");
                symbol->addChild(structdeclist);
                symbol->addChild("}");
                return symbol;
            } else if (peek(1).getTokenType() == "identifier") {
                if (peek(2).getValue() == "{") {
                    symbol->addChild("struct");
                    symbol->addChild(id);
                    symbol->addChild("{");
                    symbol->addChild(structdeclist);
                    symbol->addChild("}");
                    return symbol;
                } else {
                    symbol->addChild("struct");
                    symbol->addChild(id);
                    return symbol;
                }
            }
            return std::nullopt;

        case structdeclist:
            symbol->addChild(dec);
            symbol->addChild(structdeclist_);
            return symbol;

        case structdeclist_:
            if (next.getValue() == "}") return symbol;
            symbol->addChild(dec);
            symbol->addChild(structdeclist_);
            return symbol;

        case dec:
            symbol->addChild(type);
            symbol->addChild(dec_);
            return symbol;

        case dec_:
            if (next.getValue() == ";") {
                symbol->addChild(decEnd);
                return symbol;
            }
            symbol->addChild(declarator);
            symbol->addChild(decEnd);
            return symbol;

        case directdec:
            if (next.getValue() == "(") {
                symbol->addChild("(");
                symbol->addChild(declarator);
                symbol->addChild(")");
                symbol->addChild(directdec_);
                return symbol;
            }
            symbol->addChild(id);
            symbol->addChild(directdec_);
            return symbol;

        case directdec_:
            if (next.getValue() == "(") {
                symbol->addChild("(");
                symbol->addChild(paramlist);
                symbol->addChild(")");
                symbol->addChild(directdec_);
            }
            return symbol;

        case paramlist:
            symbol->addChild(paramdec);
            symbol->addChild(paramlist_);
            return symbol;

        case paramlist_:
            if (next.getValue() == ")") return symbol;
            symbol->addChild(",");
            symbol->addChild(paramdec);
            symbol->addChild(paramlist_);
            return symbol;

        case paramdec:
            symbol->addChild(type);
            symbol->addChild(paramdec_);
            return symbol;

        case paramdec_:
            if (peek(0).getValue() == "," || peek(0).getValue() == ")") return symbol;
            {
                int depth = 0;
                for (int k = 0; peek(k).getValue() != "EOF"; ++k) {
                    const Token t = peek(k);
                    if (t.getValue() == "(") {
                        depth++;
                        continue;
                    }
                    if (t.getValue() == ")") {
                        if (depth == 0) {
                            symbol->addChild(abstractdeclarator);
                            return symbol;
                        }
                        depth--;
                        continue;
                    }
                    if (depth == 0 && t.getValue() == ",") {
                        symbol->addChild(abstractdeclarator);
                        return symbol;
                    }
                    if (depth == 0 && t.getTokenType() == "identifier") {
                        symbol->addChild(declarator);
                        return symbol;
                    }
                }
            }
            return std::nullopt;

        case abstractdeclarator:
            if (next.getValue() == "*") {
                symbol->addChild(pointer);
                symbol->addChild(abstractdeclarator_);
                return symbol;
            }
            symbol->addChild(directabstractdeclarator);
            return symbol;

        case abstractdeclarator_:
            if (next.getValue() == "(") {
                symbol->addChild(directabstractdeclarator);
            }
            return symbol;

        case directabstractdeclarator:
            if (next.getValue() == "(" &&
                (peek(1).getValue() == "void" || peek(1).getValue() == "char" ||
                 peek(1).getValue() == "int"  || peek(1).getValue() == "struct")) {
                symbol->addChild("(");
                symbol->addChild(paramlist);
                symbol->addChild(")");
                symbol->addChild(directabstractdeclarator_);
                return symbol;
            }
            symbol->addChild("(");
            symbol->addChild(abstractdeclarator);
            symbol->addChild(")");
            symbol->addChild(directabstractdeclarator_);
            return symbol;

        case directabstractdeclarator_:
            if (next.getValue() == "(") {
                symbol->addChild("(");
                symbol->addChild(paramlist);
                symbol->addChild(")");
                symbol->addChild(directabstractdeclarator_);
            }
            return symbol;

        case funcdef_:
            symbol->addChild(compoundstatement);
            return symbol;

        case compoundstatement:
            symbol->addChild("{");
            symbol->addChild(blockitemlist);
            symbol->addChild("}");
            return symbol;

        case blockitemlist:
            if (next.getValue() == "}") return symbol;
            symbol->addChild(blockitem);
            symbol->addChild(blockitemlist);
            return symbol;

        case blockitem:
            if (next.getValue() == "void" || next.getValue() == "char" ||
                next.getValue() == "int"  || next.getValue() == "struct") {
                symbol->addChild(dec);
                return symbol;
            }
            symbol->addChild(statement);
            return symbol;

        case statement:
            if (next.getTokenType() == "identifier" && peek(1).getValue() == ":") {
                symbol->addChild(labelstatement);
                return symbol;
            } else if (next.getValue() == "if") {
                symbol->addChild(selectstatement);
                return symbol;
            } else if (next.getValue() == "while") {
                symbol->addChild(iterstatement);
                return symbol;
            } else if (next.getValue() == "goto" || next.getValue() == "continue" ||
                       next.getValue() == "break" || next.getValue() == "return") {
                symbol->addChild(jumpstatement);
                return symbol;
            } else if (next.getValue() == "{") {
                symbol->addChild(compoundstatement);
                return symbol;
            } else {
                symbol->addChild(exprstatement);
                return symbol;
            }

        case labelstatement:
            symbol->addChild(id);
            symbol->addChild(":");
            symbol->addChild(statement);
            return symbol;

        case selectstatement:
            symbol->addChild("if");
            symbol->addChild("(");
            symbol->addChild(expr);
            symbol->addChild(")");
            symbol->addChild(statement);
            symbol->addChild(selectstatement_);
            return symbol;

        case selectstatement_:
            if (next.getValue() == "else") {
                symbol->addChild("else");
                symbol->addChild(statement);
            }
            return symbol;

        case iterstatement:
            symbol->addChild("while");
            symbol->addChild("(");
            symbol->addChild(expr);
            symbol->addChild(")");
            symbol->addChild(statement);
            return symbol;

        case jumpstatement:
            if (next.getValue() == "goto") {
                symbol->addChild("goto");
                symbol->addChild(id);
                symbol->addChild(";");
                return symbol;
            } else if (next.getValue() == "continue") {
                symbol->addChild("continue");
                symbol->addChild(";");
                return symbol;
            } else if (next.getValue() == "break") {
                symbol->addChild("break");
                symbol->addChild(";");
                return symbol;
            } else if (next.getValue() == "return" && peek(1).getValue() == ";") {
                symbol->addChild("return");
                symbol->addChild(";");
                return symbol;
            } else if (next.getValue() == "return") {
                symbol->addChild("return");
                symbol->addChild(expr);
                symbol->addChild(";");
                return symbol;
            }
            return std::nullopt;

        case exprstatement:
            if (next.getValue() == ";") {
                symbol->addChild(";");
                return symbol;
            }
            symbol->addChild(expr);
            symbol->addChild(";");
            return symbol;

        default:
            std::cout << "Unhandled case found: " << symbol->getType() << "\n";
            return std::nullopt;
    }
}
