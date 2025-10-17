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

    // Add array dimensions
    if (is_array)
    {
        for (int size : array_sizes)
        {
            result += "[";
            if (size > 0)
                result += std::to_string(size);
            result += "]";
        }
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

/**
 * Get the total size including all array elements
 * For int arr[5][3], this returns 5 * 3 * 4 = 60 bytes
 */
int Type::get_total_size() const
{
    int base_size = get_size(); // Size of base type or pointer

    if (!is_array)
        return base_size;

    // Multiply by all dimensions
    int total = base_size;
    for (int dim_size : array_sizes)
    {
        total *= dim_size;
    }

    return total;
}

/**
 * Get the size of an element for pointer/array arithmetic
 * For int *p, this returns sizeof(int) = 4
 * For int arr[5][3], this returns sizeof(int[3]) = 12
 * For int **p, this returns sizeof(int*) = 8
 */
int Type::get_element_size() const
{
    // For pointers: size of what they point to
    if (pointer_level > 0)
    {
        // Create a type with one less pointer level
        Type pointed_type = *this;
        pointed_type.pointer_level--;
        return pointed_type.get_size();
    }

    // For arrays: size of sub-array (all dimensions except first)
    if (is_array && array_dim > 1)
    {
        int size = get_size(); // Base type size
        for (int i = 1; i < array_dim; i++)
        {
            size *= array_sizes[i];
        }
        return size;
    }

    // For 1D arrays or non-arrays: just the base type size
    return get_size();
}

/**
 * Check if this is a pointer type
 */
bool Type::is_pointer() const
{
    return pointer_level > 0;
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

SymbolTable::SymbolTable() : currentScope(0), scopeCounter(0), currentOffset(0) {}

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

    // Update offset for next variable (use total size for arrays)
    currentOffset += type.get_total_size();

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

    // During parsing (currentScope > 0): Return most recent symbol in current/outer scopes
    // During TAC generation (currentScope == 0): Return symbol from innermost accessible scope
    // The list is ordered with most recent (highest scope) first

    if (currentScope > 0)
    {
        // Normal parsing: return first symbol with scope <= currentScope
        for (Symbol *sym : it->second)
        {
            if (sym->scope <= currentScope)
            {
                return sym;
            }
        }
    }
    else
    {
        // TAC generation phase: all scopes exited
        // Return the first symbol (most recent in list = highest scope)
        // This gives proper shadowing semantics
        if (!it->second.empty())
        {
            return it->second.front();
        }
    }

    return nullptr;
}

void SymbolTable::enterScope()
{
    scopeCounter++;              // Increment unique scope ID
    currentScope = scopeCounter; // Set as current scope
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

void SymbolTable::exitScopeKeepSymbols()
{
    if (currentScope == 0)
        return;

    cout << "[Symbol Table] Exiting scope " << currentScope << " (keeping symbols for TAC generation)" << endl;

    // Decrement scope level but DON'T remove symbols
    // This allows TAC generation to access variables after parsing completes
    // The lookup() function is modified to still find these symbols when currentScope < sym->scope

    // Note: In a full compiler with proper IR, you'd remove symbols here
    // and keep variable info in the AST nodes themselves
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
