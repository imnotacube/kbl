#include "value.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool Value::truthy() const {
    switch (t) {
        case VT::Null: return false;
        case VT::Int: return i != 0;
        case VT::Float: return d != 0.0;
        case VT::Bool: return i != 0;
        case VT::Str: return !s.empty();
    }
    return false;
}

double Value::asDouble() const {
    switch (t) {
        case VT::Int: return (double)i;
        case VT::Float: return d;
        case VT::Bool: return i ? 1.0 : 0.0;
        case VT::Str: return std::strtod(s.c_str(), nullptr);
        case VT::Null: return 0.0;
    }
    return 0.0;
}

long long Value::asInt() const {
    switch (t) {
        case VT::Int: return i;
        case VT::Float: return (long long)d;
        case VT::Bool: return i;
        case VT::Str: return std::strtoll(s.c_str(), nullptr, 10);
        case VT::Null: return 0;
    }
    return 0;
}

std::string formatDouble(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.15g", d);
    return std::string(buf);
}

std::string Value::toString() const {
    switch (t) {
        case VT::Null: return "null";
        case VT::Int: return std::to_string(i);
        case VT::Float: return formatDouble(d);
        case VT::Bool: return i ? "true" : "false";
        case VT::Str: return s;
    }
    return "";
}

const char* Value::typeName() const {
    switch (t) {
        case VT::Null: return "null";
        case VT::Int: return "int";
        case VT::Float: return "float";
        case VT::Bool: return "bool";
        case VT::Str: return "string";
    }
    return "?";
}
