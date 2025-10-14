// symbol_table.cpp
#include "symbol_table.h"
#include <sstream>

SymbolTable::SymbolTable(ErrorCb err_cb) : error_cb(err_cb) {
    stack.clear();
    // start with global scope level 0
    stack.emplace_back();
    stack.back().level = 0;
    stack.back().is_function_level = false;
}

SymbolTable::~SymbolTable() {
    stack.clear();
}

void SymbolTable::enter_scope(bool is_function_level) {
    Scope s;
    s.level = (int)stack.size();
    s.is_function_level = is_function_level;
    stack.push_back(std::move(s));
}

void SymbolTable::exit_scope() {
    if (stack.size() <= 1) {
        report_error("[symbol_table] attempt to pop global scope ignored");
        return;
    }
    stack.pop_back();
}

InsertResult SymbolTable::insert_ident(const SymbolPtr &sym) {
    if (!sym) return InsertResult::ConflictWithDifferentKind;
    if (stack.empty()) enter_scope();

    auto &cur = stack.back().idents;
    auto it = cur.find(sym->name);
    if (it != cur.end()) {
        // redeclared in same scope
        std::ostringstream ss;
        ss << "Redeclaration of identifier '" << sym->name << "' at line " << sym->line_declared;
        report_error(ss.str());
        return InsertResult::RedeclaredInSameScope;
    }

    // set scope level metadata
    sym->scope_level = stack.back().level;
    cur[sym->name] = sym;

    // Note: Shadowing detection disabled - it produces too many false warnings
    // for normal C patterns like parameter names being reused as local variables

    return InsertResult::OK;
}

InsertResult SymbolTable::insert_tag(const SymbolPtr &sym) {
    if (!sym) return InsertResult::ConflictWithDifferentKind;
    auto &cur = stack.back().tag_names;
    auto it = cur.find(sym->name);
    if (it != cur.end()) {
        std::ostringstream ss;
        ss << "Redeclaration of tag '" << sym->name << "' at line " << sym->line_declared;
        report_error(ss.str());
        return InsertResult::RedeclaredInSameScope;
    }
    sym->scope_level = stack.back().level;
    cur[sym->name] = sym;
    return InsertResult::OK;
}

InsertResult SymbolTable::insert_typedef(const SymbolPtr &sym) {
    if (!sym) return InsertResult::ConflictWithDifferentKind;
    auto &cur = stack.back().typedefs;
    auto it = cur.find(sym->name);
    if (it != cur.end()) {
        std::ostringstream ss;
        ss << "Redeclaration of typedef '" << sym->name << "' at line " << sym->line_declared;
        report_error(ss.str());
        return InsertResult::RedeclaredInSameScope;
    }
    sym->scope_level = stack.back().level;
    cur[sym->name] = sym;
    return InsertResult::OK;
}

SymbolPtr SymbolTable::lookup_ident(const std::string &name) const {
    for (int i = (int)stack.size() - 1; i >= 0; --i) {
        auto it = stack[i].idents.find(name);
        if (it != stack[i].idents.end()) return it->second;
    }
    return nullptr;
}

SymbolPtr SymbolTable::lookup_tag(const std::string &name) const {
    for (int i = (int)stack.size() - 1; i >= 0; --i) {
        auto it = stack[i].tag_names.find(name);
        if (it != stack[i].tag_names.end()) return it->second;
    }
    return nullptr;
}

SymbolPtr SymbolTable::lookup_typedef(const std::string &name) const {
    for (int i = (int)stack.size() - 1; i >= 0; --i) {
        auto it = stack[i].typedefs.find(name);
        if (it != stack[i].typedefs.end()) return it->second;
    }
    return nullptr;
}

SymbolPtr SymbolTable::lookup_ident_current(const std::string &name) const {
    if (stack.empty()) return nullptr;
    auto it = stack.back().idents.find(name);
    if (it != stack.back().idents.end()) return it->second;
    return nullptr;
}

SymbolPtr SymbolTable::lookup_ident_max_scope(const std::string &name, int max_scope_level) const {
    // Search from innermost to outermost, but only return symbols from max_scope_level or lower
    for (int i = (int)stack.size() - 1; i >= 0; --i) {
        auto it = stack[i].idents.find(name);
        if (it != stack[i].idents.end()) {
            if (it->second->scope_level <= max_scope_level) {
                return it->second;
            }
        }
    }
    return nullptr;
}

SymbolPtr SymbolTable::lookup_tag_current(const std::string &name) const {
    if (stack.empty()) return nullptr;
    auto it = stack.back().tag_names.find(name);
    if (it != stack.back().tag_names.end()) return it->second;
    return nullptr;
}

SymbolPtr SymbolTable::lookup_typedef_current(const std::string &name) const {
    if (stack.empty()) return nullptr;
    auto it = stack.back().typedefs.find(name);
    if (it != stack.back().typedefs.end()) return it->second;
    return nullptr;
}

bool SymbolTable::mark_defined(const SymbolPtr &sym) {
    if (!sym) return false;
    if (sym->is_defined) {
        std::ostringstream ss;
        ss << "Symbol '" << sym->name << "' already defined at line " << sym->line_declared;
        report_error(ss.str());
        return false;
    }
    sym->is_defined = true;
    return true;
}

bool SymbolTable::mark_used(const SymbolPtr &sym) {
    if (!sym) return false;
    sym->is_used = true;
    return true;
}

bool SymbolTable::mark_initialized(const SymbolPtr &sym) {
    if (!sym) return false;
    sym->is_initialized = true;
    return true;
}

void SymbolTable::dump_current_scope(std::ostream &os) const {
    if (stack.empty()) { os << "<no scopes>\n"; return; }
    const auto &s = stack.back();
    os << "Scope level " << s.level << " (function-level=" << s.is_function_level << ")\n";
    if (!s.idents.empty()) {
        os << "  idents:\n";
        for (auto &p : s.idents) {
            os << "    " << p.first << " (kind=" << (int)p.second->kind
               << " line=" << p.second->line_declared << " defined=" << p.second->is_defined << ")\n";
        }
    }
    if (!s.tag_names.empty()) {
        os << "  tags:\n";
        for (auto &p : s.tag_names) {
            os << "    " << p.first << " (line=" << p.second->line_declared << ")\n";
        }
    }
    if (!s.typedefs.empty()) {
        os << "  typedefs:\n";
        for (auto &p : s.typedefs) {
            os << "    " << p.first << " (line=" << p.second->line_declared << ")\n";
        }
    }
}

void SymbolTable::dump_all_scopes(std::ostream &os) const {
    for (const auto &s : stack) {
        os << "=== scope level " << s.level << " (function=" << s.is_function_level << ") ===\n";
        if (!s.idents.empty()) {
            os << "  idents:\n";
            for (auto &p : s.idents) {
                os << "    " << p.first << " (kind=" << (int)p.second->kind
                   << " line=" << p.second->line_declared << " defined=" << p.second->is_defined << ")\n";
            }
        }
        if (!s.tag_names.empty()) {
            os << "  tags:\n";
            for (auto &p : s.tag_names) {
                os << "    " << p.first << " (line=" << p.second->line_declared << ")\n";
            }
        }
        if (!s.typedefs.empty()) {
            os << "  typedefs:\n";
            for (auto &p : s.typedefs) {
                os << "    " << p.first << " (line=" << p.second->line_declared << ")\n";
            }
        }
    }
}

void SymbolTable::report_error(std::string msg) const {
    if (error_cb) {
        error_cb(msg);
    } else {
        // non-const cast to push errors into local vector (we keep errors for caller)
        auto nonconst = const_cast<SymbolTable*>(this);
        nonconst->error_list.push_back(msg);
    }
}
