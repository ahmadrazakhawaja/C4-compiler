#include "Parser.h"
#include <cmath>
#include <iostream>
#include <ostream>
#include <vector>
#include "../helper/structs/Node.h"
#include "../helper/structs/Token.h"
#include "../helper/Symbol.h"
#include <assert.h>
#include "../helper/Utils.h"
#include "../lexer/Tokenizer.h"


void print(std::vector<Token> tokens);
void print_shallow(Node node) {
    //std::cout << "Node$";
    std::cout << node.getType();
    if(node.getToken().has_value()) {
        std::cout << "-" << node.getToken()->getValue();
    }
}
void print(Node node) {
    print_shallow(node);
    if(node.getChildren().empty()) {
        return;
    }
    std::cout << " $";
    bool first = true;
    for(Node child:node.getChildren()) {
        if(!first)
            std::cout << " ";
        first = false;
        print_shallow(child);
    }
    std::cout << "$";

}

// Constructors
Parser::Parser(std::vector<Token> tokens, bool verbose)
    :parseTreeRoot(start)
{
    while (!tokens.empty()) { //This switches the order s.t. the token accessible by back is the first token that needs parsing
        remTokens.push_back(tokens.back());
        tokens.pop_back();
    }
    remSymbols.push_back(parseTreeRoot);
    isVerbose=verbose;
}



// Lookahead k
Node Parser::peekSymbol(int k) { //This isn't a standard thing of ll(k) parsers, i hope i don't need to use it.
    return remSymbols.at(remSymbols.size()-k-1);
}
Token Parser::peek(int k) {
    if(isVerbose)
        std::cout << "\t\tPeeked at " << remTokens.at(remTokens.size()-k-1) << "using k=" << k << '\n'; //TODO remove
    return remTokens.at(remTokens.size()-k-1);
}

void Parser::run(const std::string& fileName, const std::string& path, bool isVerbose) { //This is a static method, just to be clear
        std::string fullPath = "test/lexer/" + fileName;
        std::string sourceCode = Utils::readSourceCode(fullPath);
        sourceCode += '\0';
        auto sequence = Tokenizer::tokenizeSeq(sourceCode, false);
        
        if (sequence.second.has_value()) {
            int a = sequence.second->first;
            int b = sequence.second->second;
            std::cerr << "Lexer Error at line:" << a+1 << ":" << b+1 << std::endl;
            return;
        }

        std::vector<Token> tokens = sequence.first;
        Parser parser(tokens, isVerbose);
        if(!parser.parse()) {
            std::cout << "Successfully parsed " << fileName << '\n';
        }
        return;
}

void print(std::vector<Token> tokens) {
    std::cout<<"Debug print of tokens\n";
    for(Token token:tokens)
        std::cout << token << '\n';
    std::cout <<"Debug print over\n";
}

void Parser::dump_state() {
    std::cout << "remTokens" << '\n';
    for(int i = remTokens.size()-1; i>=0; i--) {
        std::cout << remTokens.at(i) << '\n';
    }
    std::cout << "remSymbols" << '\n';
    for(int i = remSymbols.size()-1; i>=0; i--) {
        Node node = remSymbols.at(i);
        print_shallow(node);
        std::cout << '\n';
    }
}

int Parser::parse() {
    while(!remSymbols.empty() && !remTokens.empty()) {
        //expression handling
        if(remSymbols.back().getType() == expr) {
            assert(peekSymbol(1).getToken().has_value()); //every expression (that isn't inside an expression) is follwed by ')' or ;
            remSymbols.pop_back(); //get ride of the expr token now!
            remTokensExpressionIndex = 0;
            remRevExprSymbols = std::vector<Node>();
            std::optional<Node> res = evilShuntingYard(peekSymbol(0).getToken()->getValue(), true);
            while(!remRevExprSymbols.empty()) {
                remSymbols.push_back(remRevExprSymbols.back());
                remRevExprSymbols.pop_back();
            }

            if(!res.has_value()) {
                std::cout << "Expression Parsing Error at Token " << remTokens.back() << '\n';
                dump_state();
                return 1;
            }
            if(isVerbose)
                std::cout << "Expression parsed successfully" << '\n';
            continue;
        }
        std::optional<Node> changedNode = parseSymbol();
        if(!changedNode.has_value()) {
            std::cout << "Parsing Error at Token " << remTokens.back() << '\n';
            dump_state();
            return 1;
        }
        if(isVerbose) {
            std::cout << '\t';
            print(changedNode.value());
            std::cout << '\n'; //this should always show the production used.
        }
        remSymbols.pop_back();
        for(int i = changedNode->getChildren().size()-1; i>=0; i--) {
            remSymbols.push_back(changedNode->getChildren().at(i));
        }
    }
    if(!remTokens.empty() && remTokens.back().getTokenType()=="EOF") {
        remTokens.pop_back();
    }
    if(remSymbols.empty() && remTokens.empty()) {
        //Success!
        return 0;
    } else {
        std::cout << "Failure. Leftover Tokens or Symbols. The otherone is empty" << '\n';
        dump_state();
        return 1;
    }
}
std::optional<Node> Parser::parseSymbol() {
    Node symbol = remSymbols.back();
    Token next = peek(0); //Slightly easier access to next token for LL(1) purposes.
    switch(symbol.getType()) {
        case expr:
            //Shouldn't occur.
            assert(!"This code should be inaccessible tell me if it is please");
            abort();
        case terminal:
            assert(symbol.getToken()); //A terminal symbol (node object) needs to contain a token.
            if(symbol.getToken()->getValue()==next.getValue()) {
                symbol.setToken(next);
                remTokens.pop_back();
                return symbol;
            } else {
                return std::nullopt;
            }
        case stringliteral: //I guess this code is a bit whacky now that I distinguished literal into the different options, but we this should never run anyways I think
            //assert(!"I don't think this code is accesible, tell me if it is please"); //let's make sure this assumption is correct
            //We do run it now.
            if(next.getTokenType()=="string-literal") {
                symbol.setToken(next);
                remTokens.pop_back();
                return symbol;
            }
            return std::nullopt;
        case charconst:
            if(next.getTokenType()=="character-constant") {
                symbol.setToken(next);
                remTokens.pop_back();
                return symbol;
            }
            return std::nullopt;
        case decimalconst:
            if(next.getTokenType()=="decimal-constant") {
                symbol.setToken(next);
                remTokens.pop_back();
                return symbol;
            }
            return std::nullopt;
        case id:
            if(next.getTokenType()=="identifier") {
                symbol.setToken(next);
                remTokens.pop_back();
                return symbol;
            }
            return std::nullopt;
        case start:
            symbol.addChild(transunit);
            return symbol;
        case transunit:
            symbol.addChild(extdec);
            symbol.addChild(transunit_);
            return symbol;
        case transunit_:
            if(next.getValue() == "EOF") {
                return symbol;
            } else {
                symbol.addChild(extdec);
                symbol.addChild(transunit_);
                return symbol;
            }
        case extdec:
            symbol.addChild(type);
            symbol.addChild(extdec_);
            return symbol;
        case extdec_:
            if(next.getValue()==";") {
                symbol.addChild(decEnd);
                return symbol;
            } else {
                symbol.addChild(declarator);
                symbol.addChild(extdec__);
                return symbol;
            }
        case extdec__:
            if(next.getValue()==";") {
                symbol.addChild(decEnd);
                return symbol;
            } else {
                symbol.addChild(funcdef_);
                return symbol;
            }
        case decEnd:
            symbol.addChild(std::string(";"));
            return symbol;
        case declarator:
            if(next.getValue()=="*") {
                symbol.addChild(pointer);
                symbol.addChild(directdec);
                return symbol;
            } else {
                symbol.addChild(directdec);
                return symbol;
            }
        case pointer:
            symbol.addChild(std::string("*"));
            symbol.addChild(pointer_);
            return symbol;
        case pointer_:
            if(next.getValue()=="*") {
                symbol.addChild(std::string("*"));
                symbol.addChild(pointer_);
                return symbol;
            } else {
                return symbol;
            }
        case type:
            if(next.getValue()=="void") {
                symbol.addChild(std::string("void"));
                return symbol;
            } else if(next.getValue()=="char") {
                symbol.addChild(std::string("char"));
                return symbol;
            } else if(next.getValue()=="int") {
                symbol.addChild(std::string("int"));
                return symbol;
            } else if(next.getValue()=="struct") {
                symbol.addChild(structtype);
                return symbol;
            } else {
                return std::nullopt;
            }
        case structtype:
            if(peek(1).getValue()=="{") {
                symbol.addChild(std::string("struct"));
                symbol.addChild(std::string("{"));
                symbol.addChild(structdeclist);
                symbol.addChild(std::string("}"));
                return symbol;
            } else if(peek(1).getTokenType()=="identifier") {
                if(peek(2).getValue()=="{") {
                    symbol.addChild(std::string("struct"));
                    symbol.addChild(id);
                    symbol.addChild(std::string("{"));
                    symbol.addChild(structdeclist);
                    symbol.addChild(std::string("}"));
                    return symbol;
                } else {
                    symbol.addChild(std::string("struct"));
                    symbol.addChild(id);
                    return symbol;
                }
            } else {
                return std::nullopt;
            }
        case structdeclist:
            symbol.addChild(dec);
            symbol.addChild(structdeclist_);
            return symbol;
        case structdeclist_:
            if(next.getValue()=="}") {
                return symbol;
            } else {
                symbol.addChild(dec);
                symbol.addChild(structdeclist_);
                return symbol;
            }
        case dec:
            symbol.addChild(type);
            symbol.addChild(dec_);
            return symbol;
        case dec_:
            if(next.getValue()==";") {
                symbol.addChild(decEnd);
                return symbol;
            } else {
                symbol.addChild(declarator);
                symbol.addChild(decEnd);
                return symbol;
            }
        case directdec:
            if(next.getValue()=="(") {
                symbol.addChild(std::string("("));
                symbol.addChild(declarator);
                symbol.addChild(std::string(")"));
                symbol.addChild(directdec_);
                return symbol;
            } else {
                symbol.addChild(id);
                symbol.addChild(directdec_);
                return symbol;
            }
        case directdec_:
            if(next.getValue()=="(") {
                symbol.addChild(std::string("("));
                symbol.addChild(paramlist);
                symbol.addChild(std::string(")"));
                symbol.addChild(directdec_);
                return symbol;
            } else {
                return symbol;
            }
        case paramlist:
            symbol.addChild(paramdec);
            symbol.addChild(paramlist_);
            return symbol;
        case paramlist_:
            if(next.getValue()==")") {
                return symbol;
            } else {
                symbol.addChild(std::string(","));
                symbol.addChild(paramdec);
                symbol.addChild(paramlist_);
                return symbol;
            }
        case paramdec:
            symbol.addChild(type);
            symbol.addChild(paramdec_);
            return symbol;
        case paramdec_: // I can't do this in LL(k) easily, declarator will always yield id before ')' and vice versa for abstract-dec
            if(peek(0).getValue() == "," || peek(0).getValue() == ")") {
                return symbol;
            }
            { //namespace for k
            int k = 0;
            while(peek(k).getValue() != "EOF") {
                if(peek(k).getTokenType() == "identifier") {
                    symbol.addChild(declarator);
                    return symbol;
                }
                if(peek(k).getValue() == ")") {
                    symbol.addChild(abstractdeclarator);
                    return symbol;
                }
                k++;
            }
            }
            return std::nullopt;
        case abstractdeclarator:
            if(next.getValue() == "*") {
                symbol.addChild(pointer);
                symbol.addChild(abstractdeclarator_);
                return symbol;
            } else {
                symbol.addChild(directabstractdeclarator);
                return symbol;
            }
        case abstractdeclarator_:
            if(next.getValue() == "(") {
                symbol.addChild(directabstractdeclarator);
                return symbol;
            } else {
                return symbol;
            }
        case directabstractdeclarator:
            if(next.getValue() == "(" && (peek(1).getValue()=="void" || peek(1).getValue()=="char" || peek(1).getValue()=="int" || peek(1).getValue()=="struct")) {
                symbol.addChild(std::string("("));
                symbol.addChild(paramlist);
                symbol.addChild(std::string(")"));
                symbol.addChild(directabstractdeclarator_);
                return symbol;
            } else {
                symbol.addChild(std::string("("));
                symbol.addChild(abstractdeclarator);
                symbol.addChild(std::string(")"));
                symbol.addChild(directabstractdeclarator_);
                return symbol;
            }
        case directabstractdeclarator_: //follow is ')' and ',' first is '(' which makes for easier seperation
            if(next.getValue()=="(") {
                symbol.addChild(std::string("("));
                symbol.addChild(paramlist);
                symbol.addChild(std::string(")"));
                symbol.addChild(directabstractdeclarator_);
                return symbol;
            } else {
                return symbol;
            }
        case funcdef_:
            symbol.addChild(compoundstatement);
            return symbol;
        case compoundstatement:
            symbol.addChild(std::string("{"));
            symbol.addChild(blockitemlist);
            symbol.addChild(std::string("}"));
            return symbol;
        case blockitemlist:
            if(next.getValue() == "}") {
                return symbol;
            } else {
                symbol.addChild(blockitem);
                symbol.addChild(blockitemlist);
                return symbol;
            }
        case blockitem:
            if((next.getValue()=="void" || next.getValue()=="char" || next.getValue()=="int" || next.getValue()=="struct")) {
                symbol.addChild(dec);
                return symbol;
            } else {
                symbol.addChild(statement);
                return symbol;
            }
        case statement:
            if(next.getTokenType() == "identifier" && peek(1).getValue()==":") {
                symbol.addChild(labelstatement);
                return symbol;
            } else if(next.getValue() == "if") {
                symbol.addChild(selectstatement);
                return symbol;
            } else if(next.getValue() == "while") {
                symbol.addChild(iterstatement);
                return symbol;
            } else if(next.getValue() == "goto" || next.getValue() == "continue" || next.getValue() == "break" || next.getValue() == "return") {
                symbol.addChild(jumpstatement);
                return symbol;
            } else if(next.getValue() == "{") {
                symbol.addChild(compoundstatement);
                return symbol;
            } else {
                symbol.addChild(exprstatement); // :(
                return symbol;
            }
        case labelstatement:
            symbol.addChild(id);
            symbol.addChild(std::string(":"));
            symbol.addChild(statement);
            return symbol;
        case selectstatement:
            symbol.addChild(std::string("if"));
            symbol.addChild(std::string("("));
            symbol.addChild(expr);
            symbol.addChild(std::string(")"));
            symbol.addChild(statement);
            symbol.addChild(selectstatement_);
            return symbol;
        case selectstatement_:
            if(next.getValue() == "else") {
                symbol.addChild(std::string("else"));
                symbol.addChild(statement);
                return symbol;
            } else {
                return symbol;
            }
        case iterstatement:
            symbol.addChild(std::string("while"));
            symbol.addChild(std::string("("));
            symbol.addChild(expr);
            symbol.addChild(std::string(")"));
            symbol.addChild(statement);
            return symbol;
        case jumpstatement:
            if(next.getValue() == "goto") {
                symbol.addChild(std::string("goto"));
                symbol.addChild(id);
                symbol.addChild(std::string(";"));
                return symbol;
            } else if(next.getValue() == "continue") {
                symbol.addChild(std::string("continue"));
                symbol.addChild(std::string(";"));
                return symbol;
            } else if(next.getValue() == "break") {
                symbol.addChild(std::string("break"));
                symbol.addChild(std::string(";"));
                return symbol;
            } else if(next.getValue() == "return" && peek(1).getValue() == ";") {
                symbol.addChild(std::string("return"));
                symbol.addChild(std::string(";"));
                return symbol;
            } else if(next.getValue() == "return") {
                symbol.addChild(std::string("return"));
                symbol.addChild(expr);
                symbol.addChild(std::string(";"));
                return symbol;
            } else { //no need to try anything here if it doesn't work, keeps unsuccessful parsing shorter ig.
                return std::nullopt;
            }
        case exprstatement:
            if(next.getValue()==";") {
                symbol.addChild(std::string(";"));
                return symbol;
            } else {
                symbol.addChild(expr);
                symbol.addChild(std::string(";"));
                return symbol;
            }





    }
    std::cout << "Unhandled case found: " << symbol.getType();
    return std::nullopt;
}

//and it's time to deal with expressions

int opPrec(Symbol symbol) {
    switch(symbol) {
        //case parenthesizedexpr:
        //return 10;

        case arrayaccess:
        case functioncall:
        case memberaccess:
        case pointermemberaccess:
        return 9;

        case reference:
        case dereference:
        case negationarithmetic:
        case negationlogical:
        case sizeoperator:
        return 8;

        case product:
        return 7;

        case sum:
        case difference:
        return 6;

        case comparison:
        return 5;

        case equality:
        case inequality:
        return 4;

        case conjunction:
        return 3;

        case disjunction:
        return 2;

        case ternary:
        return 1;

        case assignment:
        return 0;

        default:
        assert(!"nothing else has operator precedence so this shouldn't be reachable");
        return -1;
    }
}

bool isRightAssociative(Symbol symbol) {
    switch(symbol) {
        case reference: case dereference: case negationarithmetic: case negationlogical: case sizeoperator: case ternary: case assignment: return true;
        default: return false;
    }
}
int reduce(std::vector<Symbol>& opStack, std::vector<Node>& argStack);
std::optional<Symbol> toSymbol(std::string str, bool isExpectArg) {
    std::cout << "Called toSymbol with str=" << str << " and isExpectArg=" << isExpectArg << "\n";
    if (str == "[") {
        return isExpectArg ? std::nullopt : std::make_optional(arrayaccess);
    }
    else if (str == "(") {
        return isExpectArg ? std::make_optional(parenthesizedexpr) : std::make_optional(functioncall);
    }
    else if (str == ".") {
        return isExpectArg ? std::nullopt : std::make_optional(memberaccess);
    }
    else if (str == "->") {
        return isExpectArg ? std::nullopt : std::make_optional(pointermemberaccess);
    }
    else if (str == "&") {
        return isExpectArg ? std::make_optional(reference): std::nullopt;
    }
    else if (str == "*") {
        return isExpectArg ? std::make_optional(dereference) : std::make_optional(product);
    }
    else if (str == "-") {
        return isExpectArg ? std::make_optional(negationarithmetic) : std::make_optional(difference);
    }
    else if (str == "!") {
        return isExpectArg ? std::make_optional(negationlogical) : std::nullopt;
    }
    else if (str == "sizeof") {
        return isExpectArg ? std::make_optional(sizeoperator) : std::nullopt;
    }
    else if (str == "+") {
        return isExpectArg ? std::nullopt : std::make_optional(sum);
    }
    else if (str == "<") {
        return isExpectArg ? std::nullopt : std::make_optional(comparison);
    }
    else if (str == "==") {
        return isExpectArg ? std::nullopt : std::make_optional(equality);
    }
    else if (str == "!=") {
        return isExpectArg ? std::nullopt : std::make_optional(inequality);
    }
    else if (str == "&&") {
        return isExpectArg ? std::nullopt : std::make_optional(conjunction);
    }
    else if (str == "||") {
        return isExpectArg ? std::nullopt : std::make_optional(disjunction);
    }
    else if (str == "?") {
        return isExpectArg ? std::nullopt : std::make_optional(ternary);
    }
    else if (str == "=") {
        return isExpectArg ? std::nullopt : std::make_optional(assignment);
    }

    return std::nullopt;
}

Token Parser::peekExpr(int k) {
    if(isVerbose)
        std::cout << "\t\t\tExprPeeked at " << remTokens.at(remTokens.size()-k-1-remTokensExpressionIndex) << "using k=" << k << " & remTokensExpressionIndex="
            << remTokensExpressionIndex << '\n'; //TODO remove
    return remTokens.at(remTokens.size()-k-1-remTokensExpressionIndex);
}

std::optional<Node> Parser::evilShuntingYard(std::string limit, bool isOutermost) { //The double/single limit construction exists for function calls
    return evilShuntingYard(limit, limit, isOutermost);
}
std::optional<Node> Parser::evilShuntingYard(std::string limit, std::string limit2, bool isOutermost) { //Shunting Yard but with more than just binary operators of varying precedences, hence evil Shunting Yard
    std::cout << "evilShuntingYard called" << "\n"; //TODO COMMENT OUT
    std::vector<Symbol> opStack = std::vector<Symbol>();
    std::vector<Node> argStack = std::vector<Node>();
    bool isExpectArg = true;

    while(true) {
        std::cout << "\topStack: " << '\n';
        for(Symbol symbol : opStack) {
            std::cout << "\t\t" << symbol << '\n'; //TODO COMMENT OUT
        }
        std::cout << "\targStack: \n";
        for(Node node : argStack) {
            std::cout << "\t\t";
            print(node); //TODO COMMENT OUT
            std::cout << '\n';
        }
        //std::cout << "loop token eating" << "\n"; //TODO COMMENT OUT
        Token tok = peekExpr(0);
        std::cout << "\texpr-token: " << tok << "\n";
        std::cout << "limit=" << limit << " limit2=" << limit2 << " exprToken=" << tok << "\n";
        if(tok.getValue() == limit || tok.getValue() == limit2) {
            std::cout << "limit reached" << "\n";
            if(!isOutermost) { //The general parsing function wants the ; of "return expr;" to remain. Shunting Yard does not want that. Basically an off-by-one-error in spirit
                remTokensExpressionIndex++;
                remRevExprSymbols.push_back(tok.getValue());
            }
            break;
        }
        if(tok.getValue() == "EOF") {
            return std::nullopt;
        }
        

        if (tok.getTokenType() == "string-literal") {
            if (!isExpectArg) return std::nullopt;
            remTokensExpressionIndex++;
            remRevExprSymbols.push_back(stringliteral);
            argStack.push_back(Node(stringliteral, tok));
            isExpectArg = false;
            continue;
        } else if (tok.getTokenType() == "character-constant") {
            if (!isExpectArg) return std::nullopt;
            remTokensExpressionIndex++;
            remRevExprSymbols.push_back(charconst);
            argStack.push_back(Node(charconst, tok));
            isExpectArg = false;
            continue;
        } else if (tok.getTokenType() == "decimal-constant") {
            if (!isExpectArg) return std::nullopt;
            remTokensExpressionIndex++;
            remRevExprSymbols.push_back(decimalconst);
            argStack.push_back(Node(decimalconst, tok));
            isExpectArg = false;
            continue;
        } else if (tok.getTokenType() == "identifier") {
            if (!isExpectArg) return std::nullopt;
            remTokensExpressionIndex++;
            remRevExprSymbols.push_back(id);
            argStack.push_back(Node(id, tok));
            isExpectArg = false;
            continue;
        } else if (tok.getValue() == "sizeof") {
            if(peekExpr(1).getValue() == "(") {
                if(peekExpr(2).getValue()=="void" || peekExpr(2).getValue()=="char" || peekExpr(2).getValue()=="int" || peekExpr(2).getValue()=="struct") {
                    //consume sizeof(
                    remTokensExpressionIndex++;
                    remRevExprSymbols.push_back(std::string("sizeof"));
                    remTokensExpressionIndex++;
                    remRevExprSymbols.push_back(std::string("("));
                    //dealing with the type
                    Node typeSymbolNode = Node(type);
                    remRevExprSymbols.push_back(typeSymbolNode);
                    Node sizeOperatorNode = Node(sizeoperator);
                    sizeOperatorNode.addChild(typeSymbolNode);
                    argStack.push_back(sizeOperatorNode);
                    isExpectArg = true; //!!!
                    //find matching parenthesis for sizeof(...) <- this one
                    int openParenthesis = 1;
                    while(openParenthesis > 0) {
                        remTokensExpressionIndex++;
                        Token tok = peekExpr(0);
                        if(tok.getValue() == "(") {
                            openParenthesis++;
                        } else if(tok.getValue() == ")") {
                            openParenthesis--;
                        } else if(tok.getValue() == "EOF") {
                            return std::nullopt;
                        }
                    }
                    //closing parenthesis
                    remTokensExpressionIndex++;
                    remRevExprSymbols.push_back(std::string(")"));
                    continue;
                }
            }
        }

        std::optional<Symbol> maybeOperator = toSymbol(tok.getValue(), isExpectArg);
        if(!maybeOperator.has_value()) {
            std::cout << "no operator" << "\n";
            return std::nullopt;
        }
        remTokensExpressionIndex++; //We delay it so that it is confirmed this token is good and useful before we delete it (replaced by incrasing the index now). Leads to better error messages ont he problematic token not after
        remRevExprSymbols.push_back(tok.getValue());
        Symbol op = maybeOperator.value();
        if(op==functioncall || op==arrayaccess || op==parenthesizedexpr) {
            isExpectArg = false;
        } else {
            isExpectArg = true;
        }
        std::cout << op << " and isExpectArg=" << isExpectArg << "\n";

        //special handling for sizeof(type wait fuck you can put any type here including structs. I'm fucked)

        while(op!= parenthesizedexpr && !opStack.empty() && (opPrec(op) < opPrec(opStack.back()) || !isRightAssociative(op) && opPrec(op) == opPrec(opStack.back()))) { //I am deeply familiar with op precedence of && & || now, so I will not place parenthesis
            std::cout << "loop opPrec reducing" << "\n"; //TODO COMMENT OUT
            if(reduce(opStack, argStack)) {
                return std::nullopt;
            }
        }

        if(op == parenthesizedexpr) {
            std::optional<Node> res = evilShuntingYard(")", false);
            if(!res.has_value()) {
                return std::nullopt;
            }
            Node node = Node(op);
            node.addChild(res.value());
            argStack.push_back(node);
            continue;
        }
        if(op == arrayaccess) {
            std::optional<Node> res = evilShuntingYard("]", false);
            if(!res.has_value()) {
                return std::nullopt;
            }
            argStack.push_back(res.value());
            opStack.push_back(op);
            continue;
        }
        if(op == functioncall) {
            std::cout << "functioncall handling entered\n";
            if(peekExpr(0).getValue() == ")") {
                remTokensExpressionIndex++;
                remRevExprSymbols.push_back(std::string(")"));
                Node node = Node(op);
                node.addChild(argStack.back()); //This is the function name
                argStack.push_back(node);
                continue;
            }
            int args = 0; //The argument for the function itself is accounted for by letting the loop go to <= args.
            while(true) {
                args++;
                std::optional<Node> res = evilShuntingYard(")", ",", true); //is Outermost true so we can distinguish between , and )
                if(!res.has_value()) {
                    return std::nullopt;
                }
                if(peekExpr(0).getValue() == ")") {
                    remTokensExpressionIndex++;
                    remRevExprSymbols.push_back(std::string(")"));
                    argStack.push_back(res.value());
                    break;
                } else if(peekExpr(0).getValue() == ",") {
                    remTokensExpressionIndex++;
                    remRevExprSymbols.push_back(std::string(","));
                    argStack.push_back(res.value());
                    continue; //continue innerloop, i.e. look for another functioncall argument
                } else {
                    std::cout << "Shouldn't be accessible" << "\n";
                    std::abort();
                }
            }
            Node node = Node(op);
            int totalArgStackSize = argStack.size();
            std::cout << "args=" << args << " argStackSize=" << totalArgStackSize << "\n";
            for(int i = 0; i <= args; i++) { //0 AND <=args is for called function name
                node.addChild(argStack.at(totalArgStackSize-1-args+i));
            }
            for(int i = 0; i <= args; i++) {
                argStack.pop_back();
            }
            argStack.push_back(node);
            std::cout << "functioncall handling left\n";
            continue; //continue outer loop, i.e. shunting yard next token
        }
        if(op == ternary) { //I think this works, but I'm not entirely sure.
            std::optional<Node> res = evilShuntingYard(":", false);
            if(!res.has_value()) {
                return std::nullopt;
            }
            argStack.push_back(res.value());
            opStack.push_back(op);
            continue;
        }

        opStack.push_back(op); //only reason I have to add this so many times is bc parenthesized expression doesn't add an op. Could have just made it one with the highest precedence that would have worked too.
        //oh, and functioncalls now that I remembered that they can have multiple arguments
    }
    while(!opStack.empty()) {
        std::cout << "loop final reducing" << "\n"; //TODO COMMENT OUT
        if(reduce(opStack, argStack)) return std::nullopt;
    }
    if(opStack.empty() && argStack.size() == 1) {
        return argStack.back();
    } else {
        return std::nullopt;
    }
}

int reduce(std::vector<Symbol>& opStack, std::vector<Node>& argStack) {
    std::cout << "reduce called" << "\n"; //TODO COMMENT OUT
    if(opStack.empty()) {
        return 1;
    }
    int argStackSize = argStack.size();
    Symbol op = opStack.back();
    Node node = Node(op);
    opStack.pop_back();
    switch(op) {
        case ternary:
            if(argStackSize < 3) return 1;
            node.addChild(argStack.at(argStackSize-3));
            node.addChild(argStack.at(argStackSize-2));
            node.addChild(argStack.at(argStackSize-1));
            argStack.pop_back(); argStack.pop_back(); argStack.pop_back();
            argStack.push_back(node);
            return 0;
        case reference: case dereference: case negationarithmetic: case negationlogical: case sizeoperator:
            if(argStackSize < 1) return 1;
            node.addChild(argStack.at(argStackSize-1));
            argStack.pop_back();
            argStack.push_back(node);
            return 0;
        default: //we've thrown an error by now due to checking opPrec if this isn't a valid operator no need to check, just use default
            if(argStackSize < 2) return 1;
            node.addChild(argStack.at(argStackSize-2));
            node.addChild(argStack.at(argStackSize-1));
            argStack.pop_back(); argStack.pop_back();
            argStack.push_back(node);
            return 0;
    }
}