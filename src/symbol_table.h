// symbol_table.h
#pragma once
#include "scope.h"
#include <vector>
#include <functional>
#include <iostream>

enum class InsertResult {
    OK,
    RedeclaredInSameScope,
    ConflictWithDifferentKind,
    ShadowedOuterScope // inserted but shadows an outer symbol
};

class SymbolTable {
public:
    // error callback: (message)
    using ErrorCb = std::function<void(const std::string&)>;

    explicit SymbolTable(ErrorCb err_cb = nullptr);
    ~SymbolTable();

    // scope control
    void enter_scope(bool is_function_level = false);
    void exit_scope();

    int current_scope_level() const { return (int)stack.size() - 1; }
    int scope_depth() const { return (int)stack.size(); }

    // insert in specific namespace:
    InsertResult insert_ident(const SymbolPtr &sym);    // identifiers namespace
    InsertResult insert_tag(const SymbolPtr &sym);      // tag namespace
    InsertResult insert_typedef(const SymbolPtr &sym);  // typedef namespace

    // lookup from innermost to outermost:
    SymbolPtr lookup_ident(const std::string &name) const;
    SymbolPtr lookup_tag(const std::string &name) const;
    SymbolPtr lookup_typedef(const std::string &name) const;
    
    // lookup with scope level restriction (only symbols from current scope or outer scopes)
    SymbolPtr lookup_ident_max_scope(const std::string &name, int max_scope_level) const;

    // lookup in current (innermost) scope only
    SymbolPtr lookup_ident_current(const std::string &name) const;
    SymbolPtr lookup_tag_current(const std::string &name) const;
    SymbolPtr lookup_typedef_current(const std::string &name) const;

    // Update helpers
    bool mark_defined(const SymbolPtr &sym);
    bool mark_used(const SymbolPtr &sym);
    bool mark_initialized(const SymbolPtr &sym);

    // utilities
    void dump_current_scope(std::ostream &os = std::cout) const;
    void dump_all_scopes(std::ostream &os = std::cout) const;

    // error accumulation
    const std::vector<std::string>& errors() const { return error_list; }

private:
    std::vector<Scope> stack; // stack[0] = global scope
    ErrorCb error_cb;
    std::vector<std::string> error_list;

    void report_error(std::string msg) const;
};
