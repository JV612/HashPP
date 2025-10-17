#ifndef EXPRESSION_H
#define EXPRESSION_H

#include "ast_base.h"
#include "tac.h"
#include <vector>

// Forward declaration for Type (from symbol_table.h)
class Type;
class Symbol;

// ============================================================================
// Expression Nodes - All expressions inherit from Expression base class
// ============================================================================

/**
 * Base class for all expressions
 *
 * Every expression has:
 * - A type (from type checking)
 * - A result (TAC operand where value is stored)
 * - Code (list of TAC instructions generated)
 * - truelist/falselist (for boolean expressions - backpatching)
 *
 * generate_tac() produces Three-Address Code for the expression
 */
class Expression : public ASTNode
{
public:
    Type *type;                         // Type of expression result
    TACOperand *result;                 // TAC operand for result
    std::vector<TACInstruction *> code; // TAC code generated

    // For boolean expressions (backpatching)
    InstructionList truelist;  // List of instructions to backpatch when true
    InstructionList falselist; // List of instructions to backpatch when false

    Expression() : type(nullptr), result(nullptr) {}
    virtual ~Expression();

    /**
     * Generate Three-Address Code for this expression
     * Sets: code, result, type, and optionally truelist/falselist
     */
    virtual void generate_tac() = 0;
};

/**
 * Primary Expression - Leaf nodes (identifiers, constants, parenthesized expressions)
 *
 * Represents:
 * - Identifiers (variable names)
 * - Integer constants
 * - Character constants
 * - Floating-point constants
 * - Parenthesized expressions like (expr)
 *
 * generate_tac():
 * - For identifiers: looks up in symbol table, creates identifier operand
 * - For constants: creates constant operand with appropriate type
 * - For paren expr: delegates to inner expression
 */
class PrimaryExpression : public Expression
{
public:
    enum PrimaryType
    {
        PRIM_IDENTIFIER,     // Variable name
        PRIM_INT_CONSTANT,   // Integer literal (e.g., 42)
        PRIM_CHAR_CONSTANT,  // Character literal (e.g., 'a', '\n')
        PRIM_FLOAT_CONSTANT, // Float/double literal (e.g., 3.14)
        PRIM_PAREN_EXPR      // Parenthesized expression (e.g., (x + 1))
    };

    PrimaryType prim_type; // Which kind of primary expression
    std::string name;      // For PRIM_IDENTIFIER
    int int_value;         // For PRIM_INT_CONSTANT
    char char_value;       // For PRIM_CHAR_CONSTANT
    double float_value;    // For PRIM_FLOAT_CONSTANT (stores both float and double)
    Expression *expr;      // For PRIM_PAREN_EXPR
    Symbol *symbol_ref;    // For PRIM_IDENTIFIER - cached symbol lookup result

    // Constructors for each type
    PrimaryExpression(const std::string &id_name); // Identifier
    PrimaryExpression(int value);                  // Int constant
    PrimaryExpression(char value);                 // Char constant
    PrimaryExpression(double value);               // Float/double constant
    PrimaryExpression(Expression *e);              // Parenthesized
    virtual ~PrimaryExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Binary Expression - Two operands with an operator
 *
 * Represents: left op right
 * Examples: a + b, x < y, p && q
 *
 * Operators supported:
 * - Arithmetic: +, -, *, /, %
 * - Bitwise: &, |, ^, <<, >>
 * - Comparison: <, >, <=, >=, ==, !=
 * - Logical: &&, || (use short-circuit evaluation with backpatching)
 *
 * generate_tac():
 * - For &&, ||: special handling with backpatching (short-circuit)
 * - For comparisons: generates comparison + conditional jumps (truelist/falselist)
 * - For others: generates single TAC instruction with result temp
 * - Performs type checking and type promotion
 */
class BinaryExpression : public Expression
{
public:
    TACOp op;          // Operator (TAC_ADD, TAC_MUL, TAC_LT, etc.)
    Expression *left;  // Left operand
    Expression *right; // Right operand

    BinaryExpression(Expression *l, TACOp operation, Expression *r);
    virtual ~BinaryExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Unary Expression - Single operand with prefix operator
 *
 * Represents: op expr
 * Examples: -x, +y, ~flags, !condition, ++i, --j
 *
 * Operators supported:
 * - Arithmetic: -, + (unary minus/plus)
 * - Bitwise: ~ (bitwise NOT)
 * - Logical: ! (logical NOT)
 * - Increment/Decrement: ++, -- (prefix only)
 *
 * generate_tac():
 * - For ++/-: modifies variable in place (x = x + 1)
 * - For others: creates temp with result (t = -x)
 * - Performs type checking
 */
class UnaryExpression : public Expression
{
public:
    TACOp op;         // Operator (TAC_UMINUS, TAC_LOGICAL_NOT, etc.)
    Expression *expr; // Operand

    UnaryExpression(TACOp operation, Expression *e);
    virtual ~UnaryExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Assignment Expression - Variable assignment
 *
 * Represents: lhs = rhs
 * Example: x = y + 5
 *
 * generate_tac():
 * - Looks up lhs variable in symbol table
 * - Generates code for rhs expression
 * - Checks type compatibility (with warnings for implicit conversions)
 * - Emits TAC_ASSIGN instruction
 * - Result is the lhs variable
 */
class AssignmentExpression : public Expression
{
public:
    std::string lhs_name; // Variable name (left-hand side)
    Expression *rhs;      // Right-hand side expression
    Symbol *lhs_symbol;   // Cached symbol lookup for LHS

    AssignmentExpression(const std::string &var, Expression *rhs_expr);
    virtual ~AssignmentExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * General Assignment Expression - LHS can be any lvalue expression
 * Handles: *ptr = val, arr[i] = val, etc.
 */
class GeneralAssignmentExpression : public Expression
{
public:
    Expression *lhs; // Left-hand side expression (must be lvalue)
    Expression *rhs; // Right-hand side expression

    GeneralAssignmentExpression(Expression *lhs_expr, Expression *rhs_expr);
    virtual ~GeneralAssignmentExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Array Access Expression - arr[index]
 * Generates: offset = index * element_size, addr = arr + offset, result = *addr
 */
class ArrayAccessExpression : public Expression
{
public:
    Expression *array; // Array or pointer expression
    Expression *index; // Index expression

    ArrayAccessExpression(Expression *arr, Expression *idx);
    virtual ~ArrayAccessExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

// ============================================================================
// Helper Functions - Expression Node Creation
// ============================================================================

// Create primary expressions
PrimaryExpression *create_primary_expression(const std::string &name);
PrimaryExpression *create_primary_expression(int value);
PrimaryExpression *create_primary_expression(char value);
PrimaryExpression *create_primary_expression(double value);

// Overloaded versions with location info
PrimaryExpression *create_primary_expression(const std::string &name, int line, int col);
PrimaryExpression *create_primary_expression(int value, int line, int col);
PrimaryExpression *create_primary_expression(char value, int line, int col);
PrimaryExpression *create_primary_expression(double value, int line, int col);

// Create parenthesized expression
PrimaryExpression *create_paren_expression(Expression *expr);

// Create other expressions
BinaryExpression *create_binary_expression(Expression *left, TACOp op, Expression *right);
UnaryExpression *create_unary_expression(TACOp op, Expression *expr);
AssignmentExpression *create_assignment_expression(const std::string &var, Expression *rhs);
GeneralAssignmentExpression *create_general_assignment_expression(Expression *lhs, Expression *rhs);
ArrayAccessExpression *create_array_access_expression(Expression *array, Expression *index);

#endif // EXPRESSION_H
