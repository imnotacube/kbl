#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "errors.h"

enum class Tok : uint8_t {
    End,
    Ident,
    Number,
    String,

    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Semicolon,
    Colon,
    Question,
    Dot,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,

    Assign,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,

    AndAnd,
    OrOr,
    Not,

    Amp,
    Pipe,
    Caret,
    Tilde,
    Shl,
    Shr,

    PlusAssign,
    MinusAssign,
    StarAssign,
    SlashAssign,
    PercentAssign,

    PlusPlus,
    MinusMinus
};

struct Token {
    Tok kind = Tok::End;
    std::string text;
    int line = 1;
    int col = 1;
    long long ival = 0;
    double dval = 0.0;
    bool isFloat = false;
};

std::vector<Token> lex(const std::string& src);
