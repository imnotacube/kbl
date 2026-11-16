#include "interp.h"

#include <algorithm>
#include <cmath>

#include "builtins.h"

namespace {

bool isNumeric(const Value& v) {
    return v.t == VT::Int || v.t == VT::Float || v.t == VT::Bool;
}

bool isIntLike(const Value& v) {
    return v.t == VT::Int || v.t == VT::Bool;
}

[[noreturn]] void rtErr(int line, int col, std::string msg) {
    throw ScriptError(line, col, std::move(msg));
}

std::string typeNameOf(const Value& v) { return v.typeName(); }

}  // namespace

bool valuesEqual(const Value& a, const Value& b) {
    if (isNumeric(a) && isNumeric(b)) {
        if (isIntLike(a) && isIntLike(b)) return a.asInt() == b.asInt();
        return a.asDouble() == b.asDouble();
    }
    if (a.t == VT::Null || b.t == VT::Null) return a.t == b.t;
    if (a.t == VT::Str && b.t == VT::Str) return a.s == b.s;
    return false;
}

void Interp::run() {
    globals_.assign((size_t)std::max(prog_.globalMaxSlots, 1), Value());
    for (auto& s : prog_.globals) exec(s.get());
}

Value* Interp::slotPtr(int slot, bool global, int line, int col) {
    if (global) {
        if (slot < 0 || slot >= (int)globals_.size()) rtErr(line, col, "внутренняя ошибка: глобальный слот вне диапазона");
        return &globals_[(size_t)slot];
    }
    if (frames_.empty()) rtErr(line, col, "внутренняя ошибка: локальная переменная вне функции");
    Frame& f = frames_.back();
    if (slot < 0 || slot >= (int)f.slots.size()) rtErr(line, col, "внутренняя ошибка: локальный слот вне диапазона");
    return &f.slots[(size_t)slot];
}

Value* Interp::lvalue(Expr* e, int line, int col) {
    if (!e || e->k != EK::Var) rtErr(line, col, "ожидалась переменная");
    return slotPtr(e->slot, e->globalSlot, line, col);
}

Value Interp::evalBinary(Op op, const Value& a, const Value& b, int line, int col) {
    switch (op) {
        case Op::And:
            return Value::boolean(a.truthy() && b.truthy());
        case Op::Or:
            return Value::boolean(a.truthy() || b.truthy());
        case Op::Eq:
            return Value::boolean(valuesEqual(a, b));
        case Op::Ne:
            return Value::boolean(!valuesEqual(a, b));
        case Op::Add:
            if (a.t == VT::Str || b.t == VT::Str) return Value::str(a.toString() + b.toString());
            break;
        case Op::Sub:
        case Op::Mul:
        case Op::Div:
        case Op::Mod:
            break;
        case Op::Lt:
        case Op::Le:
        case Op::Gt:
        case Op::Ge: {
            if (a.t == VT::Str && b.t == VT::Str) {
                int c = a.s.compare(b.s);
                switch (op) {
                    case Op::Lt: return Value::boolean(c < 0);
                    case Op::Le: return Value::boolean(c <= 0);
                    case Op::Gt: return Value::boolean(c > 0);
                    default: return Value::boolean(c >= 0);
                }
            }
            if (!isNumeric(a) || !isNumeric(b)) {
                rtErr(line, col, std::string("нельзя сравнить ") + typeNameOf(a) + " и " + typeNameOf(b));
            }
            bool bothInt = isIntLike(a) && isIntLike(b);
            if (bothInt) {
                long long x = a.asInt();
                long long y = b.asInt();
                switch (op) {
                    case Op::Lt: return Value::boolean(x < y);
                    case Op::Le: return Value::boolean(x <= y);
                    case Op::Gt: return Value::boolean(x > y);
                    default: return Value::boolean(x >= y);
                }
            }
            double x = a.asDouble();
            double y = b.asDouble();
            switch (op) {
                case Op::Lt: return Value::boolean(x < y);
                case Op::Le: return Value::boolean(x <= y);
                case Op::Gt: return Value::boolean(x > y);
                default: return Value::boolean(x >= y);
            }
        }
        case Op::BAnd:
        case Op::BOr:
        case Op::BXor:
        case Op::Shl:
        case Op::Shr: {
            if (!isNumeric(a) || !isNumeric(b)) rtErr(line, col, "битовые операции требуют чисел");
            long long x = a.asInt();
            long long y = b.asInt();
            switch (op) {
                case Op::BAnd: return Value::integer(x & y);
                case Op::BOr: return Value::integer(x | y);
                case Op::BXor: return Value::integer(x ^ y);
                case Op::Shl: return Value::integer((y < 0 || y > 63) ? 0 : (long long)((unsigned long long)x << y));
                default: return Value::integer((y < 0 || y > 63) ? 0 : (x >> y));
            }
        }
        default:
            rtErr(line, col, "внутренняя ошибка: неизвестная бинарная операция");
    }

    // arithmetic
    if (!isNumeric(a) || !isNumeric(b)) {
        if (op == Op::Add) return Value::str(a.toString() + b.toString());
        rtErr(line, col, std::string("нельзя применить операцию к ") + typeNameOf(a) + " и " + typeNameOf(b));
    }
    bool bothInt = isIntLike(a) && isIntLike(b);
    if (bothInt) {
        long long x = a.asInt();
        long long y = b.asInt();
        switch (op) {
            case Op::Add: return Value::integer(x + y);
            case Op::Sub: return Value::integer(x - y);
            case Op::Mul: return Value::integer(x * y);
            case Op::Div:
                if (y == 0) rtErr(line, col, "деление на ноль");
                return Value::integer(x / y);
            case Op::Mod:
                if (y == 0) rtErr(line, col, "остаток от деления на ноль");
                return Value::integer(x % y);
            default: break;
        }
    }
    double x = a.asDouble();
    double y = b.asDouble();
    switch (op) {
        case Op::Add: return Value::number(x + y);
        case Op::Sub: return Value::number(x - y);
        case Op::Mul: return Value::number(x * y);
        case Op::Div:
            if (y == 0.0) rtErr(line, col, "деление на ноль");
            return Value::number(x / y);
        case Op::Mod:
            if (y == 0.0) rtErr(line, col, "остаток от деления на ноль");
            return Value::number(std::fmod(x, y));
        default: break;
    }
    rtErr(line, col, "внутренняя ошибка: неизвестная арифметическая операция");
}

Value Interp::evalUnary(Op op, const Value& v, int line, int col) {
    switch (op) {
        case Op::Not:
            return Value::boolean(!v.truthy());
        case Op::Neg:
            if (!isNumeric(v)) rtErr(line, col, "унарный минус требует числа");
            if (isIntLike(v)) return Value::integer(-v.asInt());
            return Value::number(-v.asDouble());
        case Op::Plus:
            if (!isNumeric(v)) rtErr(line, col, "унарный плюс требует числа");
            return v;
        case Op::BNot:
            if (!isNumeric(v)) rtErr(line, col, "'~' требует числа");
            return Value::integer(~v.asInt());
        default:
            rtErr(line, col, "внутренняя ошибка: неизвестная унарная операция");
    }
}

Value Interp::eval(Expr* e) {
    switch (e->k) {
        case EK::IntLit:
            return Value::integer(e->iv);
        case EK::FloatLit:
            return Value::number(e->dv);
        case EK::StrLit:
            return Value::str(e->sv);
        case EK::BoolLit:
            return Value::boolean(e->iv != 0);
        case EK::NullLit:
            return Value::null();

        case EK::Var: {
            Value* p = slotPtr(e->slot, e->globalSlot, e->line, e->col);
            return *p;
        }

        case EK::Assign: {
            Value* p = lvalue(e->a.get(), e->line, e->col);
            Value rhs = eval(e->b.get());
            Value result;
            if (e->op == Op::None) {
                result = std::move(rhs);
            } else {
                result = evalBinary(e->op, *p, rhs, e->line, e->col);
            }
            *p = result;
            return result;
        }

        case EK::Binary: {
            if (e->op == Op::And) {
                Value a = eval(e->a.get());
                if (!a.truthy()) return Value::boolean(false);
                return Value::boolean(eval(e->b.get()).truthy());
            }
            if (e->op == Op::Or) {
                Value a = eval(e->a.get());
                if (a.truthy()) return Value::boolean(true);
                return Value::boolean(eval(e->b.get()).truthy());
            }
            Value a = eval(e->a.get());
            Value b = eval(e->b.get());
            return evalBinary(e->op, a, b, e->line, e->col);
        }

        case EK::Unary: {
            switch (e->op) {
                case Op::PreInc:
                case Op::PreDec: {
                    Value* p = lvalue(e->a.get(), e->line, e->col);
                    if (!isNumeric(*p)) rtErr(e->line, e->col, "++/-- требует числа");
                    if (isIntLike(*p)) {
                        *p = Value::integer(p->asInt() + (e->op == Op::PreInc ? 1 : -1));
                    } else {
                        *p = Value::number(p->asDouble() + (e->op == Op::PreInc ? 1.0 : -1.0));
                    }
                    return *p;
                }
                case Op::PostInc:
                case Op::PostDec: {
                    Value* p = lvalue(e->a.get(), e->line, e->col);
                    if (!isNumeric(*p)) rtErr(e->line, e->col, "++/-- требует числа");
                    Value old = *p;
                    if (isIntLike(*p)) {
                        *p = Value::integer(p->asInt() + (e->op == Op::PostInc ? 1 : -1));
                    } else {
                        *p = Value::number(p->asDouble() + (e->op == Op::PostInc ? 1.0 : -1.0));
                    }
                    return old;
                }
                default: {
                    Value v = eval(e->a.get());
                    return evalUnary(e->op, v, e->line, e->col);
                }
            }
        }

        case EK::Call: {
            std::vector<Value> args;
            args.reserve(e->args.size());
            for (auto& a : e->args) args.push_back(eval(a.get()));
            if (e->builtinId >= 0) return callBuiltin(e->builtinId, args, e->line, e->col);
            return callUser(e->funcId, args, e->line, e->col);
        }
    }
    rtErr(e->line, e->col, "внутренняя ошибка: неизвестный узел выражения");
}

Value Interp::callUser(int id, std::vector<Value>& args, int line, int col) {
    if (id < 0 || id >= (int)prog_.funcs.size()) rtErr(line, col, "внутренняя ошибка: неизвестная функция");
    const FuncDecl& fd = prog_.funcs[(size_t)id];
    if (args.size() != fd.params.size()) {
        rtErr(line, col, "функция '" + fd.name + "' ожидает " + std::to_string(fd.params.size()) +
                             " аргумент(ов), получено " + std::to_string(args.size()));
    }
    if (frames_.size() >= 500) rtErr(line, col, "слишком глубокая рекурсия");

    Frame f;
    f.fn = &fd;
    f.slots.assign((size_t)std::max(fd.maxSlots, 1), Value());
    for (size_t i = 0; i < args.size(); ++i) {
        f.slots[(size_t)fd.paramSlots[i]] = std::move(args[i]);
    }
    frames_.push_back(std::move(f));

    try {
        exec(fd.body.get());
    } catch (ReturnSignal& r) {
        frames_.pop_back();
        return std::move(r.v);
    } catch (...) {
        frames_.pop_back();
        throw;
    }
    frames_.pop_back();
    return Value::null();
}

void Interp::exec(Stmt* s) {
    switch (s->k) {
        case SK::Empty:
        case SK::FuncDecl:
            return;

        case SK::Expr:
            eval(s->e.get());
            return;

        case SK::Decl: {
            Value* p = slotPtr(s->slot, s->globalSlot, s->line, s->col);
            if (s->e) {
                *p = eval(s->e.get());
            } else {
                *p = Value::null();
            }
            return;
        }

        case SK::Block:
            for (auto& st : s->list) exec(st.get());
            return;

        case SK::If:
            if (eval(s->e.get()).truthy()) {
                if (s->s1) exec(s->s1.get());
            } else if (s->s2) {
                exec(s->s2.get());
            }
            return;

        case SK::While: {
            while (eval(s->e.get()).truthy()) {
                try {
                    if (s->s1) exec(s->s1.get());
                } catch (BreakSignal&) {
                    break;
                } catch (ContinueSignal&) {
                    continue;
                }
            }
            return;
        }

        case SK::For: {
            if (s->s1) exec(s->s1.get());
            for (;;) {
                if (s->e && !eval(s->e.get()).truthy()) break;
                try {
                    if (s->s2) exec(s->s2.get());
                } catch (BreakSignal&) {
                    break;
                } catch (ContinueSignal&) {
                    // fall through to the step
                }
                if (s->e2) eval(s->e2.get());
            }
            return;
        }

        case SK::Break:
            throw BreakSignal{};
        case SK::Continue:
            throw ContinueSignal{};
        case SK::Return:
            throw ReturnSignal{s->e ? eval(s->e.get()) : Value::null()};
    }
}
