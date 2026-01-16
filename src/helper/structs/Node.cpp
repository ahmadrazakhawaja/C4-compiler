#include "Node.h"

Node::Node(Symbol type)
    : type(type), children(), token(std::nullopt) {}

Node::Node(Symbol type, const Token& tok)
    : type(type), children(), token(tok) {}

Node::Node(Symbol type, std::optional<Token> tok)
    : type(type), children(), token(std::move(tok)) {}

Node::Node(const std::string& terminalValue)
    : type(terminal), children(), token(Token("", terminalValue, -1, -1)) {}

Node::Ptr Node::make(Symbol type) {
    return std::make_shared<Node>(type);
}
Node::Ptr Node::make(Symbol type, const Token& tok) {
    return std::make_shared<Node>(type, tok);
}
Node::Ptr Node::make(Symbol type, std::optional<Token> tok) {
    return std::make_shared<Node>(type, std::move(tok));
}
Node::Ptr Node::makeTerminal(const std::string& terminalValue) {
    return std::make_shared<Node>(terminalValue);
}

Node::Ptr Node::popLastChild() {
        Node::Ptr last = children.back();
        children.pop_back();
        return last;
}

void Node::addChild(const Ptr& child) {
    children.push_back(child);
}
void Node::addChild(Symbol sym) {
    children.push_back(Node::make(sym));
}
void Node::addChild(const std::string& terminalVal) {
    children.push_back(Node::makeTerminal(terminalVal));
}

std::ostream& operator<<(std::ostream& os, const Node& node) {
    os << "Node(" << node.type;
    if (node.token.has_value()) os << ":" << node.token->getValue();
    os << ", children=" << node.children.size() << ")";
    return os;
}
