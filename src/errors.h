#pragma once

#include <stdexcept>
#include <string>

struct ScriptError : std::runtime_error {
    int line;
    int col;
    std::string msg;

    ScriptError(int line_, int col_, std::string m)
        : std::runtime_error(m), line(line_), col(col_), msg(std::move(m)) {}
};
