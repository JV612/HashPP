#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <list>

// Simple Type representation for Phase 1

enum PrimitiveType
{
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_CHAR,
    TYPE_VOID,
    TYPE_ERROR
};

class Type
{
public:
    PrimitiveType base_type;
    int pointer_level; // 0 = not pointer, 1 = *, 2 = **, etc.
    bool is_const;

    // Array support
    bool is_array;
    int array_dim;                // Number of dimensions (0 for non-array)
    std::vector<int> array_sizes; // Size of each dimension [5][3][2]

    Type() : base_type(TYPE_ERROR), pointer_level(0), is_const(false),
             is_array(false), array_dim(0) {}
    Type(PrimitiveType bt, int ptr = 0, bool c = false)
        : base_type(bt), pointer_level(ptr), is_const(c),
          is_array(false), array_dim(0) {}

    std::string to_string() const;
    int get_size() const;         // Size in bytes for a single element
    int get_total_size() const;   // Total size including all array elements
    int get_element_size() const; // Size of element for pointer/array arithmetic

    // Phase 1: Type checking helper methods
    // These methods support basic type compatibility checking
    bool is_numeric() const;                    // int, float, or char (all arithmetic types)
    bool is_integer() const;                    // int or char (for %, bitwise ops)
    bool is_error() const;                      // Check if this is an error type
    bool is_pointer() const;                    // Check if this is a pointer type
    Type promote_with(const Type &other) const; // Get promoted type for binary ops
};

// Symbol entry
class Symbol
{
public:
    std::string name;
    Type type;
    int scope;
    int offset; // Memory offset

    Symbol(std::string n, Type t, int s, int off)
        : name(n), type(t), scope(s), offset(off) {}
};

// Minimal Symbol Table for Phase 1
class SymbolTable
{
private:
    // Hash table with list for handling scope/shadowing
    std::unordered_map<std::string, std::list<Symbol *>> table;

public:
    int Scopelevel;   // scope level of SymbolTable
    int scopeCounter; // Monotonically increasing scope ID (never reused)
    int currentOffset;
    std::string FunctionName;

    SymbolTable *Parent; // Pointer to parent symbol table for nested scopes

    SymbolTable();
    ~SymbolTable();

    // Core operations
    // Insert returns the created symbol for later use (e.g., TAC mangling)
    Symbol *insert(const std::string &name, Type type);
    // Lookup only within this table (no parent traversal)
    Symbol *lookup(const std::string &name);

    // Utilities
    void print() const;
    int getCurrentScope() const { return Scopelevel; }
};

// Global stack of symbol tables for nested scopes
extern std::vector<SymbolTable *> symbolTableStack;

// Global unique scope id generator
extern int next_scope_id;

// Helper APIs for externalized scope management and lookups
// - push a new scope whose parent is current top (if any); returns new current scope
SymbolTable *push_scope(const std::string &functionName = "");
// - pop current scope (does not delete it; symbols are kept for TAC/mangling)
void pop_scope();
// - get current scope (top of stack or nullptr)
SymbolTable *current_scope();
// - get global scope (bottom of stack or nullptr)
SymbolTable *global_scope();
// - recursive lookup starting from current scope up to parents
Symbol *lookup_symbol(const std::string &name);

// Utility: mangle an identifier for TAC using the symbol's scope (name_scope)
std::string mangle_for_tac(const std::string &name, const Symbol *sym);

// Global semantic error counter (increment on semantic errors like redeclaration)
extern int semantic_error_count;

// Track if current function has a return statement (for return checking)
extern bool current_function_has_return;

// Global debug flag for controlling debug output
extern bool debug;

// ===================== Function Registry =====================
struct FunctionSignature
{
    std::string name;
    std::vector<Type> params; // parameter types in order
    Type returnType;          // function return type
    int FunctionID;           // unique function ID for overloading
};

// magle functionID for TAC

std::string mangle_function_for_tac(const std::string &name, const FunctionSignature &fs);

// Global list of declared function signatures
extern std::vector<FunctionSignature> function_signatures;

// Register a function (name + params + return type)
FunctionSignature *register_function(const std::string &name, const std::vector<Type> &params, const Type &retType);

// Find function by name and arg type list; returns index or -1
int find_function_match(const std::string &name, const std::vector<Type> &argTypes);

void print_function_signatures();

// Register built-in I/O functions
void register_builtin_io_functions();

#endif // SYMBOL_TABLE_H
