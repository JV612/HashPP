#ifndef DECLARATION_H
#define DECLARATION_H

#include "ast_base.h"
#include "expression.h"
#include "tac.h"
#include <vector>
#include <string>

// Forward declaration for Type (from symbol_table.h)
class Type;

// ============================================================================
// Declaration Nodes
// ============================================================================

/**
 * Base class for all declarations
 *
 * Every declaration has:
 * - decl_type: The type being declared (int, char, float, etc.)
 * - code: TAC instructions generated (for initializers)
 *
 * generate_tac() inserts into symbol table and generates initialization code
 */
class Declaration : public ASTNode
{
public:
    Type *decl_type;                    // Type of the declared entity
    std::vector<TACInstruction *> code; // TAC generated for initializers

    Declaration() : decl_type(nullptr) {}
    virtual ~Declaration();

    /**
     * Insert symbol into symbol table (called during parsing)
     * Separate from generate_tac() to allow symbol insertion
     * during parsing while deferring TAC generation
     */
    virtual void insert_symbol() = 0;

    /**
     * Generate Three-Address Code for this declaration
     * Typically:
     * 1. Insert into symbol table (unless already done via insert_symbol())
     * 2. If initializer present, generate assignment code
     */
    virtual void generate_tac() = 0;
};

/**
 * Variable Declaration - Simple variable declaration with optional initializer
 *
 * Syntax: type varname [= initializer];
 * Examples:
 *   int x;           // No initializer
 *   int y = 42;      // With initializer
 *   char c = 'a';
 *
 * generate_tac():
 * - Inserts variable into symbol table with its type
 * - If initializer present:
 *   * Generates code for initializer expression
 *   * Checks type compatibility (warns for implicit conversions)
 *   * Emits TAC_ASSIGN to initialize the variable
 */
class VariableDeclaration : public Declaration
{
public:
    std::string var_name;    // Name of the variable
    Expression *initializer; // Initial value (nullptr if no initializer)
    Symbol *inserted_symbol = nullptr; // Cached symbol from insert stage
    bool is_static = false;  // Whether this is a static variable

    VariableDeclaration(Type *t, const std::string &name, Expression *init = nullptr, bool static_var = false);
    virtual ~VariableDeclaration();

    std::string to_string() const override;
    void insert_symbol() override;
    void generate_tac() override;
};

// ============================================================================
// Helper Functions - Declaration Node Creation
// ============================================================================

VariableDeclaration *create_variable_declaration(Type *type, const std::string &name,
                                                 Expression *init = nullptr);

#endif // DECLARATION_H
