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

    int currentScope; // Current active scope level for lookups
    int scopeCounter; // Monotonically increasing scope ID (never reused)
    int currentOffset;

public:
    SymbolTable();
    ~SymbolTable();

    // Core operations
    void insert(const std::string &name, Type type);
    Symbol *lookup(const std::string &name);

    // Scope management
    void enterScope();
    void exitScope();
    void exitScopeKeepSymbols(); // Exit scope but keep symbols for TAC generation

    // Utilities
    void print() const;
    int getCurrentScope() const { return currentScope; }
};

// Global symbol table instance
extern SymbolTable symbolTable;

#endif // SYMBOL_TABLE_H
