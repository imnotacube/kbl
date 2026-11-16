#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lexer.h"
#include "value.h"

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

enum class EK : uint8_t {
    IntLit,
    FloatLit,
    StrLit,
    BoolLit,
    NullLit,
    Var,
    Assign,
    Binary,
    Unary,
    Call
};

enum class Op : uint8_t {
    None,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    And,
    Or,
    Not,
    Neg,
    Plus,
    BAnd,
    BOr,
    BXor,
    BNot,
    Shl,
    Shr,
    PreInc,
    PreDec,
    PostInc,
    PostDec
};

struct Expr {
    EK k = EK::NullLit;
    Op op = Op::None;
    int line = 0;
    int col = 0;

    long long iv = 0;
    double dv = 0.0;
    std::string sv;   // string literal / callee name
    std::string name; // variable name (for diagnostics)

    int slot = -1;
    bool globalSlot = false;
    int builtinId = -1;
    int funcId = -1;

    std::vector<ExprPtr> args;
    ExprPtr a;
    ExprPtr b;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

enum class SK : uint8_t {
    Empty,
    Expr,
    Decl,
    Block,
    If,
    While,
    For,
    Break,
    Continue,
    Return,
    FuncDecl
};

struct Stmt {
    SK k = SK::Empty;
    int line = 0;
    int col = 0;

    int slot = -1;
    bool globalSlot = false;
    int funcId = -1;

    ExprPtr e;   // condition / value / init expression
    ExprPtr e2;  // for-step

    StmtPtr s1;  // then / body / for-init
    StmtPtr s2;  // else / body

    std::vector<StmtPtr> list;  // block
};
