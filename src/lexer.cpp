#include "lexer.h"

#include <cstdlib>
#include <cstring>

namespace {

void appendUtf8(std::string& out, unsigned cp) {
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentChar(char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

class Lexer {
public:
    explicit Lexer(const std::string& src) : src_(src) {}

    std::vector<Token> run() {
        std::vector<Token> out;
        for (;;) {
            skipSpace();
            Token t;
            t.line = line_;
            t.col = col_;
            if (i_ >= src_.size()) {
                t.kind = Tok::End;
                t.text = "<конец файла>";
                out.push_back(t);
                return out;
            }
            char c = src_[i_];
            if (isIdentStart(c)) {
                size_t start = i_;
                while (i_ < src_.size() && isIdentChar(src_[i_])) bump();
                t.kind = Tok::Ident;
                t.text = src_.substr(start, i_ - start);
                out.push_back(t);
                continue;
            }
            if (c >= '0' && c <= '9') {
                out.push_back(number());
                continue;
            }
            if (c == '"') {
                out.push_back(stringLit());
                continue;
            }
            out.push_back(punct());
        }
    }

private:
    const std::string& src_;
    size_t i_ = 0;
    int line_ = 1;
    int col_ = 1;

    char peek(size_t off = 0) const {
        return (i_ + off < src_.size()) ? src_[i_ + off] : '\0';
    }

    void bump() {
        if (i_ < src_.size()) {
            if (src_[i_] == '\n') {
                ++line_;
                col_ = 1;
            } else {
                ++col_;
            }
            ++i_;
        }
    }

    [[noreturn]] void err(int line, int col, const std::string& msg) const {
        throw ScriptError(line, col, msg);
    }

    void skipSpace() {
        for (;;) {
            if (i_ >= src_.size()) return;
            char c = src_[i_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
                bump();
                continue;
            }
            if (c == '/' && peek(1) == '/') {
                while (i_ < src_.size() && src_[i_] != '\n') bump();
                continue;
            }
            if (c == '/' && peek(1) == '*') {
                int sl = line_, sc = col_;
                bump();
                bump();
                for (;;) {
                    if (i_ >= src_.size()) err(sl, sc, "не закрыт комментарий /*");
                    if (src_[i_] == '*' && peek(1) == '/') {
                        bump();
                        bump();
                        break;
                    }
                    bump();
                }
                continue;
            }
            return;
        }
    }

    Token number() {
        Token t;
        t.line = line_;
        t.col = col_;
        size_t start = i_;
        if (src_[i_] == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            bump();
            bump();
            size_t hstart = i_;
            while (i_ < src_.size() && hexVal(src_[i_]) >= 0) bump();
            if (i_ == hstart) err(t.line, t.col, "пустое шестнадцатеричное число");
            std::string digits = src_.substr(hstart, i_ - hstart);
            t.kind = Tok::Number;
            t.ival = (long long)std::strtoull(digits.c_str(), nullptr, 16);
            t.dval = (double)t.ival;
            t.text = src_.substr(start, i_ - start);
            return t;
        }
        bool isFloat = false;
        while (i_ < src_.size() && src_[i_] >= '0' && src_[i_] <= '9') bump();
        if (peek() == '.' && peek(1) >= '0' && peek(1) <= '9') {
            isFloat = true;
            bump();
            while (i_ < src_.size() && src_[i_] >= '0' && src_[i_] <= '9') bump();
        }
        if (peek() == 'e' || peek() == 'E') {
            size_t j = i_ + 1;
            if (j < src_.size() && (src_[j] == '+' || src_[j] == '-')) ++j;
            if (j < src_.size() && src_[j] >= '0' && src_[j] <= '9') {
                isFloat = true;
                bump();
                if (peek() == '+' || peek() == '-') bump();
                while (i_ < src_.size() && src_[i_] >= '0' && src_[i_] <= '9') bump();
            }
        }
        std::string text = src_.substr(start, i_ - start);
        t.text = text;
        t.kind = Tok::Number;
        t.isFloat = isFloat;
        if (isFloat) {
            t.dval = std::strtod(text.c_str(), nullptr);
            t.ival = (long long)t.dval;
        } else {
            t.ival = std::strtoll(text.c_str(), nullptr, 10);
            t.dval = (double)t.ival;
        }
        return t;
    }

    Token stringLit() {
        Token t;
        t.line = line_;
        t.col = col_;
        t.kind = Tok::String;
        bump();  // opening quote
        std::string out;
        for (;;) {
            if (i_ >= src_.size()) err(t.line, t.col, "не закрыта строка");
            char c = src_[i_];
            if (c == '"') {
                bump();
                break;
            }
            if (c == '\n') err(t.line, t.col, "перевод строки внутри строки");
            if (c != '\\') {
                out.push_back(c);
                bump();
                continue;
            }
            int el = line_, ec = col_;
            bump();
            if (i_ >= src_.size()) err(el, ec, "незавершённая escape-последовательность");
            char e = src_[i_];
            bump();
            switch (e) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                case '\'': out.push_back('\''); break;
                case '0': out.push_back('\0'); break;
                case 'x': {
                    int v = 0, n = 0;
                    while (n < 2 && hexVal(peek()) >= 0) {
                        v = v * 16 + hexVal(peek());
                        bump();
                        ++n;
                    }
                    if (n == 0) err(el, ec, "после \\x нужны шестнадцатеричные цифры");
                    out.push_back((char)v);
                    break;
                }
                case 'u': {
                    unsigned cp = 0;
                    for (int n = 0; n < 4; ++n) {
                        int h = hexVal(peek());
                        if (h < 0) err(el, ec, "после \\u нужно 4 шестнадцатеричные цифры");
                        cp = cp * 16 + (unsigned)h;
                        bump();
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    err(el, ec, std::string("неизвестная escape-последовательность \\") + e);
            }
        }
        t.text = std::move(out);
        return t;
    }

    Token punct() {
        Token t;
        t.line = line_;
        t.col = col_;
        char c = src_[i_];
        char n = peek(1);
        auto two = [&](Tok k, const char* text) {
            bump();
            bump();
            t.kind = k;
            t.text = text;
            return t;
        };
        auto one = [&](Tok k, const char* text) {
            bump();
            t.kind = k;
            t.text = text;
            return t;
        };
        switch (c) {
            case '(': return one(Tok::LParen, "(");
            case ')': return one(Tok::RParen, ")");
            case '{': return one(Tok::LBrace, "{");
            case '}': return one(Tok::RBrace, "}");
            case '[': return one(Tok::LBracket, "[");
            case ']': return one(Tok::RBracket, "]");
            case ',': return one(Tok::Comma, ",");
            case ';': return one(Tok::Semicolon, ";");
            case ':': return one(Tok::Colon, ":");
            case '?': return one(Tok::Question, "?");
            case '~': return one(Tok::Tilde, "~");
            case '.':
                if (n >= '0' && n <= '9') err(t.line, t.col, "число должно начинаться с цифры");
                return one(Tok::Dot, ".");
            case '+':
                if (n == '+') return two(Tok::PlusPlus, "++");
                if (n == '=') return two(Tok::PlusAssign, "+=");
                return one(Tok::Plus, "+");
            case '-':
                if (n == '-') return two(Tok::MinusMinus, "--");
                if (n == '=') return two(Tok::MinusAssign, "-=");
                return one(Tok::Minus, "-");
            case '*':
                if (n == '=') return two(Tok::StarAssign, "*=");
                return one(Tok::Star, "*");
            case '/':
                if (n == '=') return two(Tok::SlashAssign, "/=");
                return one(Tok::Slash, "/");
            case '%':
                if (n == '=') return two(Tok::PercentAssign, "%=");
                return one(Tok::Percent, "%");
            case '=':
                if (n == '=') return two(Tok::Eq, "==");
                return one(Tok::Assign, "=");
            case '!':
                if (n == '=') return two(Tok::Ne, "!=");
                return one(Tok::Not, "!");
            case '<':
                if (n == '=') return two(Tok::Le, "<=");
                if (n == '<') return two(Tok::Shl, "<<");
                return one(Tok::Lt, "<");
            case '>':
                if (n == '=') return two(Tok::Ge, ">=");
                if (n == '>') return two(Tok::Shr, ">>");
                return one(Tok::Gt, ">");
            case '&':
                if (n == '&') return two(Tok::AndAnd, "&&");
                return one(Tok::Amp, "&");
            case '|':
                if (n == '|') return two(Tok::OrOr, "||");
                return one(Tok::Pipe, "|");
            case '^': return one(Tok::Caret, "^");
            default:
                err(t.line, t.col, std::string("неожиданный символ '") + c + "'");
        }
    }
};

}  // namespace

std::vector<Token> lex(const std::string& src) {
    Lexer lx(src);
    return lx.run();
}
