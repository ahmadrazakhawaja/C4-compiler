#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <ostream>

#include "../Symbol.h"
#include "Token.h"

class Node {
public:
    using Ptr = std::shared_ptr<Node>;

private:
    Symbol type;
    std::vector<Ptr> children;
    std::optional<Token> token;

public:
    // Constructors
    explicit Node(Symbol type);
    Node(Symbol type, const Token& tok);
    Node(Symbol type, std::optional<Token> tok);
    explicit Node(const std::string& terminalValue); // makes a terminal node with that value

    // Factory helpers (recommended)
    static Ptr make(Symbol type);
    static Ptr make(Symbol type, const Token& tok);
    static Ptr make(Symbol type, std::optional<Token> tok);
    static Ptr makeTerminal(const std::string& terminalValue);

    // Getters / setters
    Symbol getType() const { return type; }
    void setType(Symbol t) { type = t; }

    const std::vector<Ptr>& getChildren() const { return children; }
    void setChildren(const std::vector<Ptr>& c) { children = c; }

    const std::optional<Token>& getToken() const { return token; }
    void setToken(const Token& t) { token = t; }
    void setToken(std::optional<Token> t) { token = std::move(t); }

    // Child helpers
    void addChild(const Ptr& child);
    void addChild(Symbol sym);                  // convenience: adds Node(sym)
    void addChild(const std::string& terminal); // convenience: adds terminal node

    // Debug printing (small, not recursive)
    friend std::ostream& operator<<(std::ostream& os, const Node& node);
};
