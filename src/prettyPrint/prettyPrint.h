#pragma once

#include <iosfwd>
#include <vector>
#include <string>

#include "../helper/structs/Node.h"

namespace prettyPrint {

struct Options {
    bool unicodeBranches = true; // ├── └── │
    bool showTokenValue = true;
};

void printTree(const Node::Ptr& root, std::ostream& os, const Options& opt = Options{});
void printForest(const std::vector<Node::Ptr>& roots, std::ostream& os, const Options& opt = Options{});

}
