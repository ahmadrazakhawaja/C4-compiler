#include "TokenizeHelper.h"

#include <iostream>
#include <ostream>
#include <vector>

#include "../helper/structs/TokenizeAttempt.h"

namespace {

static bool isOctalDigit(char c) {
    return c >= '0' && c <= '7';
}

static bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') ||
        (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F');
}

static bool isSimpleEscapeChar(char c) {
    return c == 'n' || c == 't' || c == 'r' || c == 'b' ||
        c == 'f' || c == 'v' || c == 'a' ||
        c == '\\' || c == '\'' || c == '\"' || c == '?';
}

static size_t escapeSequenceLength(const char* p) {
    if (!p || *p == '\0') return 0;
    if (isSimpleEscapeChar(*p)) return 1;

    if (*p == 'x' || *p == 'X') {
        size_t len = 1;
        size_t digits = 0;
        while (isHexDigit(p[len])) {
            ++len;
            ++digits;
        }
        return digits > 0 ? len : 0;
    }

    if (isOctalDigit(*p)) {
        size_t len = 1;
        while (len < 3 && isOctalDigit(p[len])) {
            ++len;
        }
        return len;
    }

    return 0;
}

} // namespace

TokenizeAttempt TokenizeHelper::tokenizeStringLiterals(const char* code) {
    if (!code || *code != '"') return TokenizeAttempt();

    const char* start = code;
    const char* ptr = code + 1;

    while (*ptr) {
        if (*ptr == '\\') {
            size_t escLen = escapeSequenceLength(ptr + 1);
            if (escLen == 0) {
                TokenizeAttempt attempt;
                attempt.setCharsLexed(ptr - start);
                return attempt;
            }
            ptr += escLen + 1;
            continue;
        }

        if (*ptr == '\n') {
            TokenizeAttempt attempt;
            attempt.setCharsLexed(ptr - start);
            return attempt;
        }

        if (*ptr == '"') {
            std::string value(start, ptr - start + 1);
            Token token;
            token.setTokenType("string-literal");
            token.setValue(value);

            TokenizeAttempt attempt;
            attempt.setToken(token);
            attempt.setCharsLexed(ptr - start + 1);
            return attempt;
        }
        ++ptr;
    }
    TokenizeAttempt attempt;
    attempt.setCharsLexed(ptr - start);
    return attempt;
}

bool isIdentifierNonDigit(char c);
bool isDigit_orIdentifierNonDigit(char c);

TokenizeAttempt TokenizeHelper::tokenizeKeywordPunctuators(const char* code) {
    if (code) {
        if (code[0] == '<' && code[1] == ':') {
            Token token;
            token.setTokenType("punctuator");
            token.setValue("[");
            TokenizeAttempt attempt;
            attempt.setToken(token);
            attempt.setCharsLexed(2);
            return attempt;
        }
        if (code[0] == ':' && code[1] == '>') {
            Token token;
            token.setTokenType("punctuator");
            token.setValue("]");
            TokenizeAttempt attempt;
            attempt.setToken(token);
            attempt.setCharsLexed(2);
            return attempt;
        }
        if (code[0] == '<' && code[1] == '%') {
            Token token;
            token.setTokenType("punctuator");
            token.setValue("{");
            TokenizeAttempt attempt;
            attempt.setToken(token);
            attempt.setCharsLexed(2);
            return attempt;
        }
        if (code[0] == '%' && code[1] == '>') {
            Token token;
            token.setTokenType("punctuator");
            token.setValue("}");
            TokenizeAttempt attempt;
            attempt.setToken(token);
            attempt.setCharsLexed(2);
            return attempt;
        }
        if (code[0] == '%' && code[1] == ':' && code[2] == '%' && code[3] == ':') {
            Token token;
            token.setTokenType("punctuator");
            token.setValue("##");
            TokenizeAttempt attempt;
            attempt.setToken(token);
            attempt.setCharsLexed(4);
            return attempt;
        }
        if (code[0] == '%' && code[1] == ':') {
            Token token;
            token.setTokenType("punctuator");
            token.setValue("#");
            TokenizeAttempt attempt;
            attempt.setToken(token);
            attempt.setCharsLexed(2);
            return attempt;
        }
    }
    int max_len;
    for(max_len=0;max_len<=10;max_len++) {
        if(code[max_len] == '\0')
            break;
    }

    std::string toCheck;
    for (int i = 0; i < max_len; i++) {
        toCheck += code[i];
    }

    std::vector<std::string> punctuators = {
        "[", "]", "(", ")", "{", "}", ".", "->",
        "++", "--", "&", "*", "+", "-", "~", "!",
        "/", "%", "<<", ">>",
        "<", ">", "<=", ">=", "==", "!=",
        "^", "|", "&&", "||",
        "?", ":", ";", "...",
        "=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|=",
        ",",
        "auto", "break", "case", "char", "const", "continue", "default", "do", "double",
        "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long",
        "register", "restrict", "return", "short", "signed", "sizeof", "static", "struct",
        "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
        "_Bool", "_Complex", "_Imaginary"
    };

    //int lexedChars = 0;

    for (int i = max_len; i > 0; i--) {
        //lexedChars++;
        int index = 0;
        for (const std::string& s : punctuators) {
            if (toCheck == s) {
                // initialize new token
                Token found;
                found.setTokenType("punctuator");
                if(index > 45) {
                    if (code[i] != '\0' && isDigit_orIdentifierNonDigit(code[i])) {
                        return TokenizeAttempt();
                    }
                    found.setTokenType("keyword");
                }
                found.setValue(toCheck);

                // initialize new TokenizeAttempt
                TokenizeAttempt validAttempt;
                validAttempt.setToken(found);
                validAttempt.setCharsLexed(i);

                return validAttempt;
            }
            index++;
        }
        toCheck.pop_back();
    }
    return TokenizeAttempt();   //There is no need to look at how many tokens it could lex if it can't lex a word since a prefix of a punctuator is a punctuator
                                //and a prefix of a keyword is an identifier.
}

TokenizeAttempt TokenizeHelper::tokenizeCharacterConstants(const char* code) {
    if (code == nullptr) {
        return TokenizeAttempt();
    }

    if (code[0] != '\'') {
        return TokenizeAttempt();
    }

    size_t i = 1;
    if (code[i] == '\0' || code[i] == '\n') {
        TokenizeAttempt attempt;
        attempt.setCharsLexed(static_cast<int>(i));
        return attempt;
    }

    if (code[i] == '\\') {
        size_t escLen = escapeSequenceLength(code + i + 1);
        if (escLen == 0) {
            TokenizeAttempt attempt;
            attempt.setCharsLexed(static_cast<int>(i));
            return attempt;
        }
        i += escLen + 1;
    } else {
        if (code[i] == '\'') {
            TokenizeAttempt attempt;
            attempt.setCharsLexed(static_cast<int>(i));
            return attempt;
        }
        ++i;
    }

    if (code[i] != '\'') {
        TokenizeAttempt attempt;
        attempt.setCharsLexed(static_cast<int>(i));
        return attempt;
    }

    std::string current(code, i + 1);
    Token token;
    token.setTokenType("character-constant");
    token.setValue(current);

    TokenizeAttempt attempt;
    attempt.setToken(token);
    attempt.setCharsLexed(static_cast<int>(i + 1));
    return attempt;
}


TokenizeAttempt TokenizeHelper::tokenizeDecimalConstants(const char* code) {

    if (code == nullptr) {
        return TokenizeAttempt();
    }

    std::string current;
    int charsLexed = 0;

    size_t i = 0;
    while (code[i] != '\0') {
        char c = code[i];
        if(i == 1){
            if (code[0] == '0' && ('0' <= c && c <= '9')) {
                TokenizeAttempt attempt;
                attempt.setCharsLexed(charsLexed);

                return attempt;
            }
        }
        if ('0' <= c && c <= '9') {
            current += c;
            charsLexed++;
        } else {
            break;
        }
        ++i;
    }

    if (charsLexed == 0) {
        return TokenizeAttempt();
    }

    Token token;
    token.setTokenType("decimal-constant");
    token.setValue(current);

    TokenizeAttempt attempt;
    attempt.setToken(token);
    attempt.setCharsLexed(charsLexed);

    return attempt;
}

bool isIdentifierNonDigit(char c) {
    return (
        c == '_'
        || ('a' <= c && c <= 'z')
        || ('A' <= c && c <= 'Z')
    );
}
bool isDigit_orIdentifierNonDigit(char c) {
    return ('0'<= c && c <= '9') || isIdentifierNonDigit(c);
}

TokenizeAttempt TokenizeHelper::tokenizeIdentifier(const char* str) {
    if(!isIdentifierNonDigit(*str)) {
        return TokenizeAttempt(); //failure
    }
    int n = 1;
    while(isDigit_orIdentifierNonDigit(str[n])) {
        n++;
    }
    std::string value(str, n);
    Token token = Token("identifier", value, 0, 0);
    return TokenizeAttempt(token, n); //success
}
