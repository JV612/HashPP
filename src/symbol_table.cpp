#include "symbol_table.h"
#include <iostream>
#include <iomanip>

using namespace std;

// Global symbol table stack

vector<SymbolTable *> symbolTableStack;
int next_scope_id = 0;

int semantic_error_count = 0;
bool current_function_has_return = false;
bool debug = false;
std::vector<FunctionSignature> function_signatures;

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

SymbolTable::SymbolTable() : Scopelevel(0), scopeCounter(0), currentOffset(0), Parent(nullptr) 
{
}
    

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

Symbol *SymbolTable::insert(const string &name, Type type)
{
    // Check for redeclaration in current scope
    auto it = table.find(name);
    if (it != table.end())
    {
        for (Symbol *sym : it->second)
        {
            if (sym->scope == Scopelevel)
            {
             cerr << "[Semantic Error] Redeclaration of '" << name
                 << "' in scope " << Scopelevel << "\n";
             semantic_error_count++;
                return nullptr;
            }
        }
    }

    // Create new symbol and add to front of list (most recent first)
    Symbol *sym = new Symbol(name, type, Scopelevel, currentOffset);
    table[name].push_front(sym);

    // Update offset for next variable (use total size for arrays)
    currentOffset += type.get_total_size();

    if(debug) {
        cout << "[Symbol Table] Inserted: " << name
             << " (type: " << type.to_string()
             << ", scope: " << Scopelevel
             << ", offset: " << sym->offset << ")" << endl;
    }

    return sym;
}

Symbol *SymbolTable::lookup(const string &name)
{
    auto it = table.find(name);
    if (it == table.end())
    {
        return nullptr;
    }

    // Return the most recent symbol in this table for the name
    if (!it->second.empty())
        return it->second.front();
    return nullptr;
}

// ============================================================================
// Externalized scope management and recursive lookup
// ============================================================================

SymbolTable *current_scope()
{
    if (symbolTableStack.empty())
        return nullptr;
    return symbolTableStack.back();
}

SymbolTable *push_scope(const std::string &functionName)
{
    SymbolTable *parent = current_scope();
    SymbolTable *child = new SymbolTable();
    child->Parent = parent;
    child->Scopelevel = ++next_scope_id; // unique scope id
    child->FunctionName = functionName;
    symbolTableStack.push_back(child);
    if(debug) {
        cout << "[Scope] Entered new scope " << child->Scopelevel
             << (parent ? string(" (parent ") + to_string(parent->Scopelevel) + ")" : " (global)")
             << endl;
    }
    return child;
}

void pop_scope()
{
    if (symbolTableStack.empty())
        return;
    SymbolTable *top = symbolTableStack.back();
    if(debug) cout << "[Scope] Exiting scope " << top->Scopelevel << " (keeping symbols)" << endl;
    // Print the symbol table for the scope being popped
    top->print();
    symbolTableStack.pop_back();
    // Do not delete 'top' so that symbols remain available for TAC
}

Symbol *lookup_symbol(const std::string &name)
{
    for (SymbolTable *st = current_scope(); st != nullptr; st = st->Parent)
    {
        Symbol *s = st->lookup(name);
        if (s)
            return s;
    }
    return nullptr;
}

SymbolTable *global_scope()
{
    if (symbolTableStack.empty())
        return nullptr;
    return symbolTableStack.front();
}

std::string mangle_for_tac(const std::string &name, const Symbol *sym)
{
    if (!sym)
        return name; // fallback if symbol missing
    return name + "_" + std::to_string(sym->scope);
}

std::string mangle_function_for_tac(const std::string &name, const FunctionSignature &fs)
{
    if(&fs == nullptr)
        return name;
    else return name + "_" + std::to_string(fs.FunctionID);
}

// ===================== Function Registry =====================
FunctionSignature *register_function(const std::string &name, const std::vector<Type> &params, const Type &retType)
{

    // first check if function with same name exists

    int index = find_function_match(name, params);

    if(index != -1) {
        cerr << "[Semantic Error] Redeclaration of function '" << name << "' with same parameter types\n";
        semantic_error_count++;
        return nullptr;
    }

    // Find next unique FunctionID for overloading
    int max_id = 0;
    for (const auto &fs : function_signatures)
    {
        if (fs.name == name && fs.FunctionID >= max_id)
        {
            max_id = fs.FunctionID + 1;
        }
    }

    // Append new function signature
    function_signatures.push_back(FunctionSignature{name, params, retType, max_id});

    return &function_signatures.back();
}

static bool type_compatible(const Type &expected, const Type &actual)
{
    if (expected.is_error() || actual.is_error()) return false;
    if (expected.is_pointer() || expected.is_array || actual.is_pointer() || actual.is_array)
    {
        // For now require exact pointer level and base type match
        return expected.pointer_level == actual.pointer_level && expected.base_type == actual.base_type && expected.is_array == actual.is_array && expected.array_dim == actual.array_dim;
    }
    if (expected.base_type == actual.base_type) return true;
    // Allow numeric promotions (char < int < float)
    if (expected.is_numeric() && actual.is_numeric()) return true;
    return false;
}

int find_function_match(const std::string &name, const std::vector<Type> &argTypes)
{
    for (size_t i = 0; i < function_signatures.size(); ++i)
    {
        const auto &fs = function_signatures[i];
        if (fs.name != name) continue;
        
        if (fs.params.size() != argTypes.size()) continue;
        bool ok = true;
        for (size_t j = 0; j < argTypes.size(); ++j)
        {
            if (!type_compatible(fs.params[j], argTypes[j])) { ok = false; break; }
        }
        if (ok) return (int)i;
    }

    return -1;
}

void print_function_signatures()
{
    if (function_signatures.empty())
    {
        if(debug) cout << "\n[No Function Signatures Registered]\n"
             << endl;
        return;
    }

    cout << "\n--- Function Signatures ---\n";
    cout << left << setw(20) << "Function Name"
         << setw(30) << "Parameter Types"
         << setw(15) << "Return Type"
         << setw(10) << "FunctionID" << endl;
    cout << "---------------------------------------------------------------\n";

    for (const auto &fs : function_signatures)
    {
        cout << left << setw(20) << fs.name;

        // Parameter types
        string paramStr;
        for (size_t i = 0; i < fs.params.size(); ++i)
        {
            paramStr += fs.params[i].to_string();
            if (i < fs.params.size() - 1)
                paramStr += ", ";
        }
        cout << setw(30) << paramStr;

        cout << setw(15) << fs.returnType.to_string();
        cout << setw(10) << fs.FunctionID << endl;
    }

    cout << "---------------------------------------------------------------\n"
         << endl;
}

void SymbolTable::print() const
{
    if (table.empty())
    {
        if(debug) cout << "\n[Symbol Table is empty]\n"
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
