# ifndef COMPILER_LAB_TOKENIZER_H
# define COMPILER_LAB_TOKENIZER_H
# include "../helper/structs/TokenizeAttempt.h"
# include <vector>
# include <optional>
# include <utility>
# include <string>

struct LexError {
    int line = -1;
    int column = -1;
    std::string message;
};

class Tokenizer {
public:
    static TokenizeAttempt tokenize(const char* source, bool isVerbose);
    static std::pair<std::vector<Token>, std::optional<LexError>> tokenizeSeq(std::string source, bool isVerbose);
};

# endif //COMPILER_LAB_TOKENIZER_H
