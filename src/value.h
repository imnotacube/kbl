#pragma once

#include <cstdint>
#include <string>

enum class VT : uint8_t { Null, Int, Float, Bool, Str };

struct Value {
    VT t = VT::Null;
    long long i = 0;
    double d = 0.0;
    std::string s;

    Value() = default;

    static Value null() { return Value(); }
    static Value integer(long long v) {
        Value r;
        r.t = VT::Int;
        r.i = v;
        return r;
    }
    static Value number(double v) {
        Value r;
        r.t = VT::Float;
        r.d = v;
        return r;
    }
    static Value boolean(bool v) {
        Value r;
        r.t = VT::Bool;
        r.i = v ? 1 : 0;
        return r;
    }
    static Value str(std::string v) {
        Value r;
        r.t = VT::Str;
        r.s = std::move(v);
        return r;
    }

    bool truthy() const;
    double asDouble() const;
    long long asInt() const;
    std::string toString() const;
    const char* typeName() const;
};

std::string formatDouble(double d);
