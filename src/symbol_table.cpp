#include "symbol_table.h"
#include <iostream>
#include <iomanip>

using namespace std;

// Global symbol table instance
SymbolTable symbolTable;

// ============================================================================
// Type Implementation
// ============================================================================

string Type::to_string() const
{
    string result;

    if (is_const)
        result = "const ";

    switch (base_type)
    {
    case TYPE_INT:
        result += "int";
        break;
    case TYPE_FLOAT:
        result += "float";
        break;
    case TYPE_CHAR:
        result += "char";
        break;
    case TYPE_VOID:
        result += "void";
        break;
    case TYPE_ERROR:
        result += "error";
        break;
    }

    for (int i = 0; i < pointer_level; i++)
    {
        result += "*";
    }

    return result;
}

int Type::get_size() const
{
    // Pointers are 8 bytes (64-bit)
    if (pointer_level > 0)
        return 8;

    switch (base_type)
    {
    case TYPE_INT:
        return 4;
    case TYPE_FLOAT:
        return 4;
    case TYPE_CHAR:
        return 1;
    case TYPE_VOID:
        return 0;
    case TYPE_ERROR:
        return 0;
    }
    return 0;
}

// ============================================================================
// Phase 1: Type Checking Helper Methods
// ============================================================================

/**
 * Check if this type can be used in arithmetic operations
 * Returns true for int, float, and char (char promotes to int)
 */
bool Type::is_numeric() const
{
    return base_type == TYPE_INT ||
           base_type == TYPE_FLOAT ||
           base_type == TYPE_CHAR;
}

/**
 * Check if this type is an integer type (for modulo, bitwise ops)
 * Returns true for int and char only (no floating point)
 */
bool Type::is_integer() const
{
    return base_type == TYPE_INT ||
           base_type == TYPE_CHAR;
}

/**
 * Check if this is an error type (for error propagation)
 */
bool Type::is_error() const
{
    return base_type == TYPE_ERROR;
}

/**
 * Type promotion for binary operations
 * Phase 1 rules: FLOAT > INT > CHAR
 * This follows C's implicit conversion hierarchy
 */
Type Type::promote_with(const Type &other) const
{
    // Error propagation: if either type is error, result is error
    if (is_error() || other.is_error())
    {
        return Type(TYPE_ERROR);
    }

    // Float takes precedence over everything
    if (base_type == TYPE_FLOAT || other.base_type == TYPE_FLOAT)
    {
        return Type(TYPE_FLOAT);
    }

    // Int takes precedence over char
    if (base_type == TYPE_INT || other.base_type == TYPE_INT)
    {
        return Type(TYPE_INT);
    }

    // Both are char
    return Type(TYPE_CHAR);
}

// ============================================================================
// SymbolTable Implementation
// ============================================================================

SymbolTable::SymbolTable() : currentScope(0), currentOffset(0) {}

SymbolTable::~SymbolTable()
{
    // Clean up all symbols
    for (auto &pair : table)
    {
        for (Symbol *sym : pair.second)
        {
            delete sym;
        }
    }
}

void SymbolTable::insert(const string &name, Type type)
{
    // Check for redeclaration in current scope
    auto it = table.find(name);
    if (it != table.end())
    {
        for (Symbol *sym : it->second)
        {
            if (sym->scope == currentScope)
            {
                cerr << "Error: Redeclaration of '" << name
                     << "' in scope " << currentScope << endl;
                return;
            }
        }
    }

    // Create new symbol and add to front of list (most recent first)
    Symbol *sym = new Symbol(name, type, currentScope, currentOffset);
    table[name].push_front(sym);

    // Update offset for next variable
    currentOffset += type.get_size();

    cout << "[Symbol Table] Inserted: " << name
         << " (type: " << type.to_string()
         << ", scope: " << currentScope
         << ", offset: " << sym->offset << ")" << endl;
}

Symbol *SymbolTable::lookup(const string &name)
{
    auto it = table.find(name);
    if (it == table.end())
    {
        return nullptr;
    }

    // Return the most recent symbol (first in list) that's in valid scope
    for (Symbol *sym : it->second)
    {
        if (sym->scope <= currentScope)
        {
            return sym;
        }
    }

    return nullptr;
}

void SymbolTable::enterScope()
{
    currentScope++;
    cout << "[Symbol Table] Entered scope " << currentScope << endl;
}

void SymbolTable::exitScope()
{
    if (currentScope == 0)
        return;

    cout << "[Symbol Table] Exiting scope " << currentScope << endl;

    // Remove all symbols from current scope
    for (auto it = table.begin(); it != table.end();)
    {
        auto &symbolList = it->second;

        for (auto symIt = symbolList.begin(); symIt != symbolList.end();)
        {
            if ((*symIt)->scope == currentScope)
            {
                delete *symIt;
                symIt = symbolList.erase(symIt);
            }
            else
            {
                ++symIt;
            }
        }

        // Remove entry if list is empty
        if (symbolList.empty())
        {
            it = table.erase(it);
        }
        else
        {
            ++it;
        }
    }

    currentScope--;
}

void SymbolTable::print() const
{
    if (table.empty())
    {
        cout << "\n[Symbol Table is empty]\n"
             << endl;
        return;
    }

    cout << "\n========== SYMBOL TABLE ==========\n";
    cout << left << setw(15) << "Name"
         << setw(15) << "Type"
         << setw(8) << "Scope"
         << setw(10) << "Offset" << endl;
    cout << "---------------------------------------------------\n";

    for (const auto &pair : table)
    {
        for (const Symbol *sym : pair.second)
        {
            cout << left << setw(15) << sym->name
                 << setw(15) << sym->type.to_string()
                 << setw(8) << sym->scope
                 << setw(10) << sym->offset << endl;
        }
    }

    cout << "==================================\n"
         << endl;
}
