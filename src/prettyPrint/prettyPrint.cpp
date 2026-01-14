#include "prettyPrint.h"

#include <ostream>
#include <sstream>

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
