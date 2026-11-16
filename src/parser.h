#pragma once

#include <string>
#include <vector>

#include "ast.h"

struct FuncDecl {
    std::string name;
    std::vector<std::string> params;
    std::vector<int> paramSlots;
    StmtPtr body;
    int maxSlots = 0;
    int line = 0;
    int col = 0;
};

struct Program {
    std::vector<StmtPtr> globals;
    std::vector<FuncDecl> funcs;
    int globalMaxSlots = 0;
    std::string path;
};

Program parse(const std::vector<Token>& tokens, const std::string& path);
