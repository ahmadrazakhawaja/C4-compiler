#pragma once

#include <iosfwd>
#include <vector>
#include <string>

#include "../helper/structs/Node.h"
#include <unordered_set>
#include <unordered_map>

namespace prettyPrint {

struct Options {
    bool unicodeBranches = true; // ├── └── │
    bool showTokenValue = true;
};

void printTree(const Node::Ptr& root, std::ostream& os, const Options& opt = Options{});
void printForest(const std::vector<Node::Ptr>& roots, std::ostream& os, const Options& opt = Options{});
Node::Ptr reduceTree(const Node::Ptr& root);
void collectDeclarations(const Node::Ptr& node, 
                        std::unordered_map<std::string, std::string>& ids);

void collectStructDefinitions(const Node::Ptr& node, 
                              std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& structTable);

std::string getNodeDataType(const Node::Ptr& node, 
                            const std::unordered_map<std::string, std::string>& ids,
                            const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& structTable);

void analyzeAssignments(const Node::Ptr& node, 
                       const std::unordered_map<std::string, std::string>& ids,
                       const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& structTable);

void analyzeSemantics(const Node::Ptr& root);

void analyzeTypes(const Node::Ptr& node, 
                  const std::unordered_map<std::string, std::string>& ids,
                  const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& structTable);

void reportError(const Node::Ptr& node, const std::string& message);
Node::Ptr findFirstTerminal(const Node::Ptr& node);
Node::Ptr buildAST(const Node::Ptr& root);
bool shouldIncludeInAST(const Node::Ptr& node);
}
