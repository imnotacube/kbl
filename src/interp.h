#pragma once

#include <vector>

#include "parser.h"
#include "value.h"

struct ReturnSignal {
    Value v;
};
struct BreakSignal {};
struct ContinueSignal {};

class Interp {
public:
    explicit Interp(Program& p) : prog_(p) {}

    void run();

private:
    struct Frame {
        const FuncDecl* fn = nullptr;
        std::vector<Value> slots;
    };

    Program& prog_;
    std::vector<Value> globals_;
    std::vector<Frame> frames_;

    Value eval(Expr* e);
    void exec(Stmt* s);

    Value evalBinary(Op op, const Value& a, const Value& b, int line, int col);
    Value evalUnary(Op op, const Value& v, int line, int col);
    Value callUser(int id, std::vector<Value>& args, int line, int col);

    Value* slotPtr(int slot, bool global, int line, int col);
    Value* lvalue(Expr* e, int line, int col);
};

bool valuesEqual(const Value& a, const Value& b);
