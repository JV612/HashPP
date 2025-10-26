#include "symbol_table.h"
#include "diagnostics.h"
#include <iostream>
#include <iomanip>

using namespace std;

extern int yylineno;

namespace
{
int effective_line(int line)
{
    return line > 0 ? line : yylineno;
}
}

#define SEM_ERROR(line, ...) report_semantic_error(effective_line(line), __VA_ARGS__)

// Global symbol table stack

vector<SymbolTable *> symbolTableStack;
SymbolTable *globalSymbolTable = nullptr;
int next_scope_id = 0;

int semantic_error_count = 0;
bool current_function_has_return = false;
bool debug = false;
bool function_debug = false;
bool method_debug = false;
bool symbol_debug = false;
bool ast_debug = false;
std::vector<FunctionSignature> function_signatures;
FunctionSignature *current_function_signature = nullptr;

// Method-related globals
std::vector<MethodSignature> method_signatures;
MethodSignature *current_method_signature = nullptr;

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
// Struct Type Implementation
// ============================================================================

StructType *current_struct = nullptr;

void StructType::add_member(const std::string &member_name, Type *member_type, int line)
{
    // Check for duplicate member names
    if (has_member(member_name))
    {
        SEM_ERROR(line, "Duplicate member '%s' in struct '%s'",
                  member_name.c_str(), name.c_str());
        semantic_error_count++;
        delete member_type; // Clean up the duplicate type
        return;
    }
    members.push_back(std::make_pair(member_name, member_type));
}

int StructType::get_member_offset(const std::string &member_name) const
{
    auto it = member_offsets.find(member_name);
    if (it != member_offsets.end())
    {
        return it->second;
    }
    return 0; // Should not happen if used correctly
}

Type *StructType::get_member_type(const std::string &member_name) const
{
    for (const auto &member : members)
    {
        if (member.first == member_name)
        {
            return member.second;
        }
    }
    return nullptr; // Should not happen if used correctly
}

bool StructType::has_member(const std::string &member_name) const
{
    for (const auto &member : members)
    {
        if (member.first == member_name)
        {
            return true;
        }
    }
    return false;
}

void StructType::finalize()
{
    // Calculate offsets and total size
    member_offsets.clear();
    total_size = 0;

    if (is_union)
    {
        // Union: all members start at offset 0, size is max(member size)
        int max_size = 0;
        for (auto &member : members)
        {
            member_offsets[member.first] = 0;
            int member_size = member.second->get_total_size();
            if (member_size > max_size)
                max_size = member_size;
        }
        total_size = max_size;
    }
    else
    {
        // Struct: members laid out sequentially
        int current_offset = 0;
        for (auto &member : members)
        {
            member_offsets[member.first] = current_offset;
            int member_size = member.second->get_total_size();
            current_offset += member_size;
        }
        total_size = current_offset;
    }
}

// ============================================================================
// Scope-aware Struct Management Functions
// ============================================================================

void register_struct_in_scope(const std::string &struct_name, StructType *struct_type)
{
    SymbolTable *scope = current_scope();
    if (!scope)
    {
        // At global level, use global symbol table
        scope = globalSymbolTable;
        if (debug)
            printf("[Struct Registry] Registering '%s' in global scope\n", struct_name.c_str());
    }
    else
    {
        if (debug)
            printf("[Struct Registry] Registering '%s' in scope %d\n", struct_name.c_str(), scope->Scopelevel);
    }
    if (scope)
    {
        scope->register_struct(struct_name, struct_type);
    }
}

StructType *lookup_struct_in_scope(const std::string &struct_name)
{
    // Start from current scope and walk up to parent scopes
    SymbolTable *scope = current_scope();
    if (!scope)
    {
        // At global level, start with global symbol table
        scope = globalSymbolTable;
    }

    while (scope)
    {
        StructType *st = scope->lookup_struct_local(struct_name);
        if (st)
        {
            return st;
        }
        scope = scope->Parent;
    }

    return nullptr;
}

bool struct_exists_in_current_scope(const std::string &struct_name)
{
    SymbolTable *scope = current_scope();
    if (scope)
    {
        return scope->lookup_struct_local(struct_name) != nullptr;
    }

    // Check global scope if no current scope
    if (globalSymbolTable)
    {
        return globalSymbolTable->lookup_struct_local(struct_name) != nullptr;
    }

    return false;
}

// ============================================================================
// Class Type Support
// ============================================================================

ClassType *current_class = nullptr;
AccessLevel current_access_level = ACCESS_PUBLIC;

void ClassType::add_member(const std::string &member_name, Type *member_type, int line, AccessLevel access)
{
    // Check for duplicate member names
    if (has_member(member_name))
    {
        SEM_ERROR(line, "Duplicate member '%s' in class '%s'",
                  member_name.c_str(), name.c_str());
        semantic_error_count++;
        delete member_type; // Clean up the duplicate type
        return;
    }
    members.push_back(std::make_pair(member_name, member_type));
    member_access[member_name] = access;
}

int ClassType::get_member_offset(const std::string &member_name) const
{
    auto it = member_offsets.find(member_name);
    if (it != member_offsets.end())
    {
        return it->second;
    }
    return 0; // Should not happen if used correctly
}

Type *ClassType::get_member_type(const std::string &member_name) const
{
    for (const auto &member : members)
    {
        if (member.first == member_name)
        {
            return member.second;
        }
    }
    return nullptr; // Should not happen if used correctly
}

bool ClassType::has_member(const std::string &member_name) const
{
    for (const auto &member : members)
    {
        if (member.first == member_name)
        {
            return true;
        }
    }
    return false;
}

AccessLevel ClassType::get_member_access(const std::string &member_name) const
{
    auto it = member_access.find(member_name);
    if (it != member_access.end())
    {
        return it->second;
    }
    return ACCESS_PUBLIC; // Default for safety
}

void ClassType::finalize()
{
    // Calculate offsets and total size
    // Classes use sequential layout (like structs)
    member_offsets.clear();
    total_size = 0;

    int current_offset = 0;
    for (auto &member : members)
    {
        member_offsets[member.first] = current_offset;
        int member_size = member.second->get_total_size();
        current_offset += member_size;
    }
    total_size = current_offset;
}

void ClassType::add_method(MethodSignature *method)
{
    methods.push_back(method);
}

bool ClassType::has_method(const std::string &method_name) const
{
    for (const auto *method : methods)
    {
        if (method->method_name == method_name)
        {
            return true;
        }
    }
    return false;
}

std::vector<MethodSignature *> ClassType::get_methods(const std::string &method_name) const
{
    std::vector<MethodSignature *> matching_methods;
    for (auto *method : methods)
    {
        if (method->method_name == method_name)
        {
            matching_methods.push_back(method);
        }
    }
    return matching_methods;
}

// ============================================================================
// Scope-aware Class Management Functions
// ============================================================================

void register_class_in_scope(const std::string &class_name, ClassType *class_type)
{
    SymbolTable *scope = current_scope();
    if (!scope)
    {
        // At global level, use global symbol table
        scope = globalSymbolTable;
        if (debug)
            printf("[Class Registry] Registering '%s' in global scope\n", class_name.c_str());
    }
    else
    {
        if (debug)
            printf("[Class Registry] Registering '%s' in scope %d\n", class_name.c_str(), scope->Scopelevel);
    }
    if (scope)
    {
        scope->register_class(class_name, class_type);
    }
}

ClassType *lookup_class_in_scope(const std::string &class_name)
{
    // Start from current scope and walk up to parent scopes
    SymbolTable *scope = current_scope();
    if (!scope)
    {
        // At global level, start with global symbol table
        scope = globalSymbolTable;
    }

    while (scope)
    {
        ClassType *ct = scope->lookup_class_local(class_name);
        if (ct)
        {
            return ct;
        }
        scope = scope->Parent;
    }

    return nullptr;
}

bool class_exists_in_current_scope(const std::string &class_name)
{
    SymbolTable *scope = current_scope();
    if (scope)
    {
        return scope->lookup_class_local(class_name) != nullptr;
    }

    // Check global scope if no current scope
    if (globalSymbolTable)
    {
        return globalSymbolTable->lookup_class_local(class_name) != nullptr;
    }

    return false;
}

// ============================================================================
// Type Implementation
// ============================================================================

string Type::to_string() const
{
    string result;

    if (is_const)
        result = "const ";

    if (is_class)
    {
        result += "class " + class_name;
    }
    else if (is_struct)
    {
        if (is_union)
            result += "union " + struct_name;
        else
            result += "struct " + struct_name;
    }
    else
    {
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
        case TYPE_BOOL:
            result += "bool";
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

    // Class types - use direct pointer if available
    if (is_class)
    {
        ClassType *ct = class_type_ptr;
        if (!ct)
        {
            // Fallback to scope-based lookup
            ct = lookup_class_in_scope(class_name);
        }
        if (ct)
        {
            return ct->total_size;
        }
        return 0; // Error case - class not found
    }

    // Struct types - prefer using direct pointer if available
    if (is_struct)
    {
        StructType *st = struct_type_ptr;
        if (!st)
        {
            // Fallback to scope-based lookup
            st = lookup_struct_in_scope(struct_name);
        }
        if (st)
        {
            return st->total_size;
        }
        return 0; // Error case - struct not found
    }

    switch (base_type)
    {
    case TYPE_INT:
        return 4;
    case TYPE_FLOAT:
        return 4;
    case TYPE_CHAR:
        return 1;
    case TYPE_BOOL:
        return 1; // Bools are stored as single bytes
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
           base_type == TYPE_BOOL ||
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
           base_type == TYPE_BOOL ||
           base_type == TYPE_ENUM;
}

/**
 * Check if this is an error type (for error propagation)
 */
bool Type::is_error() const
{
    // Struct types, union types, and class types have base_type==TYPE_ERROR but are not actual errors
    // if is_struct (which includes unions) or is_class is true
    if (is_struct || is_union || is_class)
        return false;
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

    // Treat bool in arithmetic as promoted to int (C-style integer promotion)
    if (base_type == TYPE_BOOL || other.base_type == TYPE_BOOL)
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

    // Clean up all structs in this scope
    for (auto &pair : struct_types)
    {
        delete pair.second;
    }
}

bool SymbolTable::register_struct(const std::string &name, StructType *st)
{
    // Check if struct already exists in THIS scope
    if (struct_types.find(name) != struct_types.end())
    {
        return false; // Already exists
    }
    struct_types[name] = st;
    return true;
}

StructType *SymbolTable::lookup_struct_local(const std::string &name)
{
    auto it = struct_types.find(name);
    if (it != struct_types.end())
    {
        return it->second;
    }
    return nullptr;
}

bool SymbolTable::register_class(const std::string &name, ClassType *ct)
{
    // Check if class already exists in THIS scope
    if (class_types.find(name) != class_types.end())
    {
        return false; // Already exists
    }
    class_types[name] = ct;
    return true;
}

ClassType *SymbolTable::lookup_class_local(const std::string &name)
{
    auto it = class_types.find(name);
    if (it != class_types.end())
    {
        return it->second;
    }
    return nullptr;
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
    if (!parent)
    {
        // If stack is empty, use global symbol table as parent
        parent = globalSymbolTable;
    }
    SymbolTable *child = new SymbolTable();
    child->Parent = parent;
    child->Scopelevel = ++next_scope_id; // unique scope id
    child->FunctionName = functionName;
    symbolTableStack.push_back(child);
    if (debug)
    {
        cout << "[Scope] Entered new scope " << child->Scopelevel
             << (parent ? string(" (parent ") + to_string(parent->Scopelevel) + ")" : " (no parent)")
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
        semantic_error_count++;
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

// ============================================================================
// Unified Type Compatibility Checking
// ============================================================================

bool is_type_compatible(const Type &target_type, const Type &source_type, bool allow_implicit_conversions)
{
    // Error propagation
    if (target_type.is_error() || source_type.is_error())
        return false;

    // ========================================================================
    // EXACT TYPE MATCH
    // ========================================================================
    if (target_type.base_type == source_type.base_type &&
        target_type.pointer_level == source_type.pointer_level &&
        target_type.is_array == source_type.is_array &&
        target_type.is_struct == source_type.is_struct &&
        target_type.is_class == source_type.is_class)
    {
        // For struct/union types, also check that names and union flag match
        if (target_type.is_struct && source_type.is_struct)
        {
            return (target_type.struct_name == source_type.struct_name && 
                    target_type.is_union == source_type.is_union);
        }
        // For class types, check that names match
        else if (target_type.is_class && source_type.is_class)
        {
            return (target_type.class_name == source_type.class_name);
        }
        // For primitive types (int, float, char, etc.) or identical complex types
        else if (!target_type.is_struct && !source_type.is_struct && 
                 !target_type.is_class && !source_type.is_class)
        {
            return true;
        }
        
        return false; // Different complex type categories
    }

    // If not allowing implicit conversions, stop here
    if (!allow_implicit_conversions)
    {
        return false;
    }

    // ========================================================================
    // POINTER COMPATIBILITY RULES (C-style)
    // ========================================================================
    
    // Rule 1: void* is compatible with any pointer type (both directions)
    if (target_type.pointer_level > 0 && source_type.pointer_level > 0)
    {
        // void* -> T* (any pointer type)
        if (source_type.base_type == TYPE_VOID && source_type.pointer_level == 1 &&
            target_type.pointer_level == 1)
        {
            return true;
        }
        
        // T* -> void* (any pointer type to void*)
        if (target_type.base_type == TYPE_VOID && target_type.pointer_level == 1 &&
            source_type.pointer_level == 1)
        {
            return true;
        }
    }
    
    // Rule 2: Null constant (represented as void*) can be assigned to any pointer type
    if (target_type.pointer_level > 0 &&
        source_type.base_type == TYPE_VOID && source_type.pointer_level == 1)
    {
        return true; // null constant to any pointer
    }

    // ========================================================================
    // ARRAY DECAY TO POINTER
    // ========================================================================
    if (source_type.is_array && 
        target_type.pointer_level > 0 && 
        !target_type.is_array &&
        source_type.array_dim == 1 &&
        target_type.pointer_level == (source_type.pointer_level + 1))
    {
        // Array element type must match pointer target type
        if (target_type.base_type == source_type.base_type &&
            target_type.is_struct == source_type.is_struct &&
            target_type.is_class == source_type.is_class)
        {
            // For struct/class arrays, names must match too
            if (target_type.is_struct && source_type.is_struct)
            {
                return (target_type.struct_name == source_type.struct_name && 
                        target_type.is_union == source_type.is_union);
            }
            else if (target_type.is_class && source_type.is_class)
            {
                return (target_type.class_name == source_type.class_name);
            }
            else if (!target_type.is_struct && !source_type.is_struct && 
                     !target_type.is_class && !source_type.is_class)
            {
                return true; // Primitive array decay
            }
        }
    }

    // ========================================================================
    // STRICT RULES: NO IMPLICIT CONVERSIONS FOR STRUCTURED TYPES
    // ========================================================================
    
    // Struct/Class/Union types: NO implicit conversions between different types
    // Note: Enums are excluded here because they should be compatible with integers
    if ((target_type.is_struct || target_type.is_class) ||
        (source_type.is_struct || source_type.is_class))
    {
        // These types can only be compatible through exact match (handled above)
        return false;
    }
    
    // ========================================================================
    // ENUM ↔ INTEGER COMPATIBILITY (C-style)
    // ========================================================================
    if (target_type.pointer_level == 0 && source_type.pointer_level == 0 &&
        !target_type.is_array && !source_type.is_array &&
        !target_type.is_struct && !source_type.is_struct &&
        !target_type.is_class && !source_type.is_class)
    {
        // enum -> int, char, bool conversions
        if (source_type.base_type == TYPE_ENUM && 
            (target_type.base_type == TYPE_INT || target_type.base_type == TYPE_CHAR || target_type.base_type == TYPE_BOOL))
        {
            return true;
        }
        
        // int, char -> enum conversions  
        if (target_type.base_type == TYPE_ENUM &&
            (source_type.base_type == TYPE_INT || source_type.base_type == TYPE_CHAR))
        {
            return true;
        }
    }

    // ========================================================================
    // NUMERIC TYPE CONVERSIONS (C-style: int, char, float, bool)
    // ========================================================================  
    if (target_type.pointer_level == 0 && source_type.pointer_level == 0 &&
        !target_type.is_array && !source_type.is_array &&
        !target_type.is_struct && !source_type.is_struct &&
        !target_type.is_class && !source_type.is_class &&
        target_type.base_type != TYPE_ENUM && source_type.base_type != TYPE_ENUM &&
        target_type.is_numeric() && source_type.is_numeric())
    {
        return true; // Allow numeric conversions: int ↔ char ↔ float ↔ bool
    }

    // ========================================================================
    // ALL OTHER CASES: INCOMPATIBLE
    // ========================================================================
    return false;
}

bool should_warn_implicit_conversion(const Type &target_type, const Type &source_type)
{
    // Warn for potentially lossy numeric conversions (excluding enum conversions)
    if (target_type.pointer_level == 0 && source_type.pointer_level == 0 &&
        !target_type.is_array && !source_type.is_array &&
        !target_type.is_struct && !source_type.is_struct &&
        !target_type.is_class && !source_type.is_class &&
        target_type.base_type != TYPE_ENUM && source_type.base_type != TYPE_ENUM &&
        target_type.is_numeric() && source_type.is_numeric())
    {
        // Warn for potentially lossy conversions
        // float/double -> int (truncation)
        if (source_type.base_type == TYPE_FLOAT && target_type.is_integer())
            return true;
        
        // Warn for any non-exact numeric type conversion
        if (target_type.base_type != source_type.base_type)
            return true;
    }
    
    // Warn for enum ↔ integer conversions (valid but potentially unsafe)
    if (target_type.pointer_level == 0 && source_type.pointer_level == 0 &&
        !target_type.is_array && !source_type.is_array &&
        !target_type.is_struct && !source_type.is_struct &&
        !target_type.is_class && !source_type.is_class)
    {
        // enum -> int/char/bool (warn about potential value range issues)
        if (source_type.base_type == TYPE_ENUM && 
            (target_type.base_type == TYPE_INT || target_type.base_type == TYPE_CHAR || target_type.base_type == TYPE_BOOL))
        {
            return true;
        }
        
        // int/char -> enum (warn about potential invalid enum values)
        if (target_type.base_type == TYPE_ENUM &&
            (source_type.base_type == TYPE_INT || source_type.base_type == TYPE_CHAR))
        {
            return true;
        }
    }
    
    // Warn for pointer conversions involving void*
    if (target_type.pointer_level > 0 && source_type.pointer_level > 0)
    {
        // void* <-> T* conversions (warn about potential loss of type safety)
        if ((source_type.base_type == TYPE_VOID && source_type.pointer_level == 1 &&
             target_type.pointer_level == 1 && target_type.base_type != TYPE_VOID) ||
            (target_type.base_type == TYPE_VOID && target_type.pointer_level == 1 &&
             source_type.pointer_level == 1 && source_type.base_type != TYPE_VOID))
        {
            return true;
        }
    }
    
    return false;
}

// Backward compatibility wrapper for existing code
static bool type_compatible(const Type &expected, const Type &actual)
{
    return is_type_compatible(expected, actual, true);
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
    if (!function_debug) return;
    
    if (function_signatures.empty())
    {
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

// ===================== Method Registry =====================

MethodSignature *register_method(const std::string &class_name, const std::string &method_name,
                                  const std::vector<Type> &params, const Type &retType, 
                                  bool is_constructor, bool is_destructor, AccessLevel access)
{
    // Constructor/Destructor validation
    if (is_constructor) {
        // Constructor must have same name as class and void return type
        if (method_name != class_name) {
            cerr << "[Semantic Error] Constructor name '" << method_name << "' must match class name '" << class_name << "'\n";
            semantic_error_count++;
            return nullptr;
        }
        if (retType.base_type != TYPE_VOID) {
            cerr << "[Semantic Error] Constructor '" << class_name << "::" << method_name << "' must have void return type\n";
            semantic_error_count++;
            return nullptr;
        }
    }
    
    if (is_destructor) {
        // Destructor must have ~ClassName name, no parameters, and void return type
        if (method_name != ("~" + class_name)) {
            cerr << "[Semantic Error] Destructor name '" << method_name << "' must be '~" << class_name << "'\n";
            semantic_error_count++;
            return nullptr;
        }
        if (!params.empty()) {
            cerr << "[Semantic Error] Destructor '" << class_name << "::" << method_name << "' cannot have parameters\n";
            semantic_error_count++;
            return nullptr;
        }
        if (retType.base_type != TYPE_VOID) {
            cerr << "[Semantic Error] Destructor '" << class_name << "::" << method_name << "' must have void return type\n";
            semantic_error_count++;
            return nullptr;
        }
    }
    
    // Check if method with same signature already exists in this class
    MethodSignature *existing = find_method_match(class_name, method_name, params);
    if (existing)
    {
        cerr << "[Semantic Error] Redeclaration of method '" << class_name << "::" << method_name
             << "' with same parameter types\n";
        semantic_error_count++;
        return nullptr;
    }

    // Validation: No regular method should have the same name as class (only constructors allowed)
    if (!is_constructor && method_name == class_name) {
        cerr << "[Semantic Error] Method '" << class_name << "::" << method_name 
             << "' cannot have same name as class (only constructors allowed)\n";
        semantic_error_count++;
        return nullptr;
    }

    // Find next unique MethodID for this method name in this class
    int max_id = 0;
    for (const auto &ms : method_signatures)
    {
        if (ms.class_name == class_name && ms.method_name == method_name && ms.MethodID >= max_id)
        {
            max_id = ms.MethodID + 1;
        }
    }

    // Create method signature
    MethodSignature new_method;
    new_method.class_name = class_name;
    new_method.method_name = method_name;
    new_method.params = params;
    new_method.returnType = retType;
    new_method.MethodID = max_id;
    new_method.access_level = access;
    new_method.is_constructor = is_constructor;
    new_method.is_destructor = is_destructor;
    
    // Generate mangled name for TAC
    new_method.mangled_name = mangle_method_for_tac(class_name, method_name, new_method);
    
    // Add to global registry
    method_signatures.push_back(new_method);
    
    const char* method_type = is_constructor ? "constructor" : (is_destructor ? "destructor" : "method");
    if (debug)
    {
        printf("[Method Registry] Registered %s '%s::%s' with ID %d, mangled as '%s'\n",
               method_type, class_name.c_str(), method_name.c_str(), max_id, new_method.mangled_name.c_str());
    }
    
    return &method_signatures.back();
}

MethodSignature *find_method_match(const std::string &class_name, const std::string &method_name,
                                     const std::vector<Type> &argTypes)
{
    for (auto &ms : method_signatures)
    {
        if (ms.class_name != class_name || ms.method_name != method_name)
            continue;

        if (ms.params.size() != argTypes.size())
            continue;

        bool ok = true;
        for (size_t j = 0; j < argTypes.size(); ++j)
        {
            if (!type_compatible(ms.params[j], argTypes[j]))
            {
                ok = false;
                break;
            }
        }
        if (ok)
            return &ms;
    }

    return nullptr;
}

std::string mangle_method_for_tac(const std::string &class_name, const std::string &method_name, const MethodSignature &ms)
{
    // Mangle as: ClassName_methodName_ID
    // For more sophisticated mangling, could include parameter types
    return class_name + "_" + method_name + "_" + std::to_string(ms.MethodID);
}

void print_method_signatures()
{
    if (!method_debug) return;
    
    if (method_signatures.empty())
    {
        cout << "\n[No Method Signatures Registered]\n" << endl;
        return;
    }

    cout << "\n--- Method Signatures ---\n";
    cout << left << setw(20) << "Class::Method"
         << setw(30) << "Parameter Types"
         << setw(15) << "Return Type"
         << setw(12) << "Type"
         << setw(15) << "Mangled Name" << endl;
    cout << "------------------------------------------------------------------------------------\n";

    for (const auto &ms : method_signatures)
    {
        cout << left << setw(20) << (ms.class_name + "::" + ms.method_name);

        // Parameter types (excluding implicit 'this')
        string paramStr;
        for (size_t i = 0; i < ms.params.size(); ++i)
        {
            paramStr += ms.params[i].to_string();
            if (i < ms.params.size() - 1)
                paramStr += ", ";
        }
        if (paramStr.empty())
            paramStr = "void";
        cout << setw(30) << paramStr;

        cout << setw(15) << ms.returnType.to_string();
        
        // Method type
        string methodType = "method";
        if (ms.is_constructor) methodType = "constructor";
        else if (ms.is_destructor) methodType = "destructor";
        cout << setw(12) << methodType;
        
        cout << setw(15) << ms.mangled_name << endl;
    }

    cout << "------------------------------------------------------------------------------------\n" << endl;
}

void SymbolTable::print() const
{
    if (!symbol_debug) return;
    
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

// ============================================================================
// Bool Compatibility Checking
// ============================================================================

bool is_bool_compatible(const Type& type) {
    // C-style bool compatibility rules:
    // - Numeric types (int, char, float, double) are bool-compatible (0 = false, non-zero = true)
    // - Pointers are bool-compatible (NULL = false, non-NULL = true)  
    // - Enums are bool-compatible (like integers)
    // - bool is obviously bool-compatible
    // - struct/class/union are NOT bool-compatible (C standard)
    
    if (type.is_error()) {
        return false;
    }
    
    // Pointers (including arrays which decay to pointers) are bool-compatible
    if (type.is_pointer() || type.is_array) {
        return true;
    }
    
    // Check base types
    switch (type.base_type) {
        case TYPE_INT:
        case TYPE_CHAR:  
        case TYPE_FLOAT:
        case TYPE_BOOL:
        case TYPE_ENUM:
            return true;
            
        case TYPE_VOID:
            // void is not bool-compatible unless it's a pointer (handled above)
            return false;
            
        default:
            // struct/class/union are not bool-compatible
            return false;
    }
}
