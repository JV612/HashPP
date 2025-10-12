// scope.h
#pragma once
#include "symbol.h"
#include <unordered_map>
#include <string>

struct Scope {
    // three separate name maps (C-like namespaces)
    std::unordered_map<std::string, SymbolPtr> idents;     // variables, functions, labels, enum constants
    std::unordered_map<std::string, SymbolPtr> tag_names; // struct/union/enum tags
    std::unordered_map<std::string, SymbolPtr> typedefs;  // typedef names

    // optional: labels (function-local) can be kept in idents or separate map
    int level = 0;           // scope level
    bool is_function_level = false; // useful for label resolution
};
