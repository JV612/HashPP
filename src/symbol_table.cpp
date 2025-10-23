#include "symbol_table.h"
#include <iostream>
#include <iomanip>

using namespace std;

// Global symbol table stack

vector<SymbolTable *> symbolTableStack;
SymbolTable *globalSymbolTable = nullptr;
int next_scope_id = 0;

int semantic_error_count = 0;
bool current_function_has_return = false;
bool debug = false;
std::vector<FunctionSignature> function_signatures;
FunctionSignature *current_function_signature = nullptr;

// ============================================================================
// Enum Type Implementation
// ============================================================================

std::unordered_map<std::string, EnumType *> enum_registry;

void EnumType::add_member(const std::string &member_name, int value)
{
    members[member_name] = value;
    next_value = value + 1;
}

int EnumType::get_member_value(const std::string &member_name) const
{
    auto it = members.find(member_name);
    if (it != members.end())
    {
        return it->second;
    }
    return 0; // Should not happen if used correctly
}

bool EnumType::has_member(const std::string &member_name) const
{
    return members.find(member_name) != members.end();
}

void register_enum(const std::string &enum_name, EnumType *enum_type)
{
    enum_registry[enum_name] = enum_type;
}

EnumType *lookup_enum(const std::string &enum_name)
{
    auto it = enum_registry.find(enum_name);
    if (it != enum_registry.end())
    {
        return it->second;
    }
    return nullptr;
}

bool is_enum_member(const std::string &identifier)
{
    // Check all registered enums for this member
    for (const auto &pair : enum_registry)
    {
        if (pair.second->has_member(identifier))
        {
            return true;
        }
    }
    return false;
}

int get_enum_member_value(const std::string &identifier)
{
    // Search all registered enums for this member
    for (const auto &pair : enum_registry)
    {
        if (pair.second->has_member(identifier))
        {
            return pair.second->get_member_value(identifier);
        }
    }
    return 0; // Should not happen if is_enum_member was checked first
}

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
    case TYPE_ENUM:
        result += "enum";
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
    case TYPE_ENUM:
        return 4; // Enums are stored as integers
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

    // Multiply by all dimensions. If any dimension is non-positive (0 or -1)
    // it means the total size is unknown at compile-time (VLA or unspecified),
    // so return 0 to indicate unknown/variable size.
    int total = base_size;
    for (int dim_size : array_sizes)
    {
        if (dim_size <= 0)
            return 0; // Unknown total size (runtime-sized or unspecified)
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
 * Returns true for int, float, char, and enum (enums are treated as ints)
 */
bool Type::is_numeric() const
{
    // Pointers are not numeric
    if (pointer_level > 0)
        return false;

    return base_type == TYPE_INT ||
           base_type == TYPE_FLOAT ||
           base_type == TYPE_CHAR ||
           base_type == TYPE_ENUM;
}

/**
 * Check if this type is an integer type (for modulo, bitwise ops)
 * Returns true for int, char, and enum (no floating point)
 */
bool Type::is_integer() const
{
    // Pointers are not integers
    if (pointer_level > 0)
        return false;

    return base_type == TYPE_INT ||
           base_type == TYPE_CHAR ||
           base_type == TYPE_ENUM;
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

    // Update offset for next variable. For arrays try to use total size.
    // If total size is unknown at compile time (e.g., VLA marked by 0),
    // fall back to reserving one element's worth of space (element size)
    int total_size = type.get_total_size();
    if (total_size > 0)
    {
        currentOffset += total_size;
    }
    else
    {
        int elem = type.get_element_size();
        if (elem > 0)
            currentOffset += elem; // reserve at least one element
        // else leave offset unchanged
    }

    if (debug)
    {
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
    if (debug)
    {
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
    if (debug)
        cout << "[Scope] Exiting scope " << top->Scopelevel << " (keeping symbols)" << endl;
    // Print the symbol table for the scope being popped
    top->print();
    symbolTableStack.pop_back();
    // Do not delete 'top' so that symbols remain available for TAC
}

Symbol *lookup_symbol(const std::string &name)
{
    // First, search through local scopes (stack)
    for (SymbolTable *st = current_scope(); st != nullptr; st = st->Parent)
    {
        Symbol *s = st->lookup(name);
        if (s)
            return s;
    }
    
    // Second, check function static variables if we're in a function
    if (current_function_signature)
    {
        // Get current scope level for static variable lookup
        int currentScopeLevel = current_scope() ? current_scope()->Scopelevel : 0;
        Symbol *s = lookup_function_static_symbol(name, currentScopeLevel);
        if (s)
            return s;
    }
    
    // Third, check global symbol table
    if (globalSymbolTable)
    {
        Symbol *s = globalSymbolTable->lookup(name);
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
    if (&fs == nullptr)
        return name;
    else
        return name + "_" + std::to_string(fs.FunctionID);
}

// ============================================================================
// Global Symbol Table Management
// ============================================================================

void init_global_symbol_table()
{
    if (!globalSymbolTable)
    {
        globalSymbolTable = new SymbolTable();
        globalSymbolTable->Scopelevel = 0; // Global scope level
        globalSymbolTable->FunctionName = "Global";
        if (debug)
            cout << "[Global] Initialized global symbol table" << endl;
    }
}

void cleanup_global_symbol_table()
{
    if (globalSymbolTable)
    {
        delete globalSymbolTable;
        globalSymbolTable = nullptr;
        if (debug)
            cout << "[Global] Cleaned up global symbol table" << endl;
    }
}

SymbolTable *get_global_symbol_table()
{
    return globalSymbolTable;
}

Symbol *insert_global_symbol(const std::string &name, Type type)
{
    if (!globalSymbolTable)
        init_global_symbol_table();
    
    Symbol *sym = globalSymbolTable->insert(name, type);
    if (sym)
    {
        sym->is_static = true; // All global variables are treated as static
        if (debug)
            cout << "[Global] Inserted global symbol: " << name << " (type: " << type.to_string() << ")" << endl;
    }
    return sym;
}

// ============================================================================
// Function Static Variable Management
// ============================================================================

void set_current_function(FunctionSignature *func)
{
    current_function_signature = func;
    if (debug && func)
        cout << "[Static] Set current function: " << func->name << endl;
}

FunctionSignature *get_current_function()
{
    return current_function_signature;
}

Symbol *insert_function_static_symbol(const std::string &varName, Type type, int scopeLevel)
{
    if (!current_function_signature)
    {
        cerr << "[Error] Cannot insert static variable '" << varName << "' - no current function" << endl;
        return nullptr;
    }
    
    // Initialize static table for this function if not exists
    if (!current_function_signature->static_table)
    {
        current_function_signature->static_table = new SymbolTable();
        // Static table doesn't need its own scope ID - it's just a storage mechanism
        // Static variables will use the scope ID where they're declared (scopeLevel parameter)
        current_function_signature->static_table->Scopelevel = 0; // Not used for static table itself
        current_function_signature->static_table->FunctionName = current_function_signature->name + "_static";
        if (debug)
            cout << "[Static] Created static table for function: " << current_function_signature->name << endl;
    }
    
    // Create unique name with scope level to handle nested scopes
    string uniqueName = varName + "_scope" + to_string(scopeLevel);
    
    // Check for redeclaration in the same scope
    Symbol *existing = current_function_signature->static_table->lookup(uniqueName);
    if (existing)
    {
        cerr << "[Semantic Error] Redeclaration of static variable '" << varName 
             << "' in scope " << scopeLevel << " of function '" 
             << current_function_signature->name << "'" << endl;
        semantic_error_count++;
        return nullptr;
    }
    
    // Insert the static variable
    Symbol *sym = current_function_signature->static_table->insert(uniqueName, type);
    if (sym)
    {
        sym->is_static = true;
        // Override scope to be the declaring scope, not the static table's scope
        sym->scope = scopeLevel;
        // Store original name for lookup purposes
        sym->name = varName; // Keep original name for lookup purposes
        if (debug)
            cout << "[Static] Inserted function static: " << varName 
                 << " (unique: " << uniqueName << ") in " << current_function_signature->name 
                 << " scope " << scopeLevel << endl;
    }
    return sym;
}

Symbol *lookup_function_static_symbol(const std::string &varName, int scopeLevel)
{
    if (!current_function_signature || !current_function_signature->static_table)
        return nullptr;
    
    // Try to find with scope-specific name first
    string uniqueName = varName + "_scope" + to_string(scopeLevel);
    Symbol *sym = current_function_signature->static_table->lookup(uniqueName);
    if (sym)
        return sym;
    
    // If not found with current scope, try broader search through all scopes
    // (for when variable is declared in outer scope but accessed in inner scope)
    for (int scope = scopeLevel; scope >= 0; scope--)
    {
        string searchName = varName + "_scope" + to_string(scope);
        sym = current_function_signature->static_table->lookup(searchName);
        if (sym)
            return sym;
    }
    
    return nullptr;
}

// ===================== Function Registry =====================
FunctionSignature *register_function(const std::string &name, const std::vector<Type> &params, const Type &retType)
{

    // first check if function with same name exists

    int index = find_function_match(name, params);

    if (index != -1)
    {
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
    function_signatures.push_back(FunctionSignature{name, params, retType, max_id, nullptr});

    return &function_signatures.back();
}

static bool type_compatible(const Type &expected, const Type &actual)
{
    if (expected.is_error() || actual.is_error())
        return false;

    // Handle array decay: array T[N] decays to pointer T*
    if (expected.is_pointer() && actual.is_array)
    {
        // Array decays to pointer of same base type
        // char[100] -> char*
        return expected.base_type == actual.base_type && expected.pointer_level == 1;
    }

    if (expected.is_pointer() || expected.is_array || actual.is_pointer() || actual.is_array)
    {
        // For now require exact pointer level and base type match
        return expected.pointer_level == actual.pointer_level && expected.base_type == actual.base_type && expected.is_array == actual.is_array && expected.array_dim == actual.array_dim;
    }
    if (expected.base_type == actual.base_type)
        return true;
    // Allow numeric promotions (char < int < float)
    if (expected.is_numeric() && actual.is_numeric())
        return true;
    return false;
}

int find_function_match(const std::string &name, const std::vector<Type> &argTypes)
{
    for (size_t i = 0; i < function_signatures.size(); ++i)
    {
        const auto &fs = function_signatures[i];
        if (fs.name != name)
            continue;

        if (fs.params.size() != argTypes.size())
            continue;
        bool ok = true;
        for (size_t j = 0; j < argTypes.size(); ++j)
        {
            if (!type_compatible(fs.params[j], argTypes[j]))
            {
                ok = false;
                break;
            }
        }
        if (ok)
            return (int)i;
    }

    return -1;
}

void print_function_signatures()
{
    if (function_signatures.empty())
    {
        if (debug)
            cout << "\n[No Function Signatures Registered]\n"
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
        if (debug)
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

// ============================================================================
// Built-in I/O Functions Registration
// ============================================================================

void register_builtin_io_functions()
{
    // ========== PRINT FUNCTIONS ==========

    // print_int(int)
    vector<Type> print_int_params = {Type(TYPE_INT)};
    register_function("print_int", print_int_params, Type(TYPE_VOID));

    // print_double(double)
    vector<Type> print_double_params = {Type(TYPE_FLOAT)};
    register_function("print_double", print_double_params, Type(TYPE_VOID));

    // print_char(char)
    vector<Type> print_char_params = {Type(TYPE_CHAR)};
    register_function("print_char", print_char_params, Type(TYPE_VOID));

    // print_string(char*)
    vector<Type> print_string_params = {Type(TYPE_CHAR, 1)}; // char* (pointer_level=1)
    register_function("print_string", print_string_params, Type(TYPE_VOID));

    // print_newline()
    vector<Type> print_newline_params = {}; // No parameters
    register_function("print_newline", print_newline_params, Type(TYPE_VOID));

    // ========== SCAN FUNCTIONS ==========

    // scan_int() -> int
    vector<Type> scan_int_params = {};
    register_function("scan_int", scan_int_params, Type(TYPE_INT));

    // scan_double() -> double
    vector<Type> scan_double_params = {};
    register_function("scan_double", scan_double_params, Type(TYPE_FLOAT));

    // scan_char() -> char
    vector<Type> scan_char_params = {};
    register_function("scan_char", scan_char_params, Type(TYPE_CHAR));

    // scan_string(char*)
    vector<Type> scan_string_params = {Type(TYPE_CHAR, 1)}; // char* parameter
    register_function("scan_string", scan_string_params, Type(TYPE_VOID));

    if (debug)
        cout << "[COMPILER] Registered 9 built-in I/O functions" << endl;
}
