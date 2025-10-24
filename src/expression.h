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
        PRIM_STRING_LITERAL, // String literal (e.g., "hello")
        PRIM_BOOL_CONSTANT,  // Boolean literal (true/false)
        PRIM_NULL_CONSTANT,  // Null pointer constant (null/nullptr)
        PRIM_PAREN_EXPR      // Parenthesized expression (e.g., (x + 1))
    };

    PrimaryType prim_type;    // Which kind of primary expression
    std::string name;         // For PRIM_IDENTIFIER
    int int_value;            // For PRIM_INT_CONSTANT
    char char_value;          // For PRIM_CHAR_CONSTANT
    double float_value;       // For PRIM_FLOAT_CONSTANT (stores both float and double)
    std::string string_value; // For PRIM_STRING_LITERAL
    bool bool_value;          // For PRIM_BOOL_CONSTANT
    Expression *expr;         // For PRIM_PAREN_EXPR
    Symbol *symbol_ref;       // For PRIM_IDENTIFIER - cached symbol lookup result

    // Constructors for each type
    PrimaryExpression(const std::string &id_name);                     // Identifier
    PrimaryExpression(int value);                                      // Int constant
    PrimaryExpression(char value);                                     // Char constant
    PrimaryExpression(double value);                                   // Float/double constant
    PrimaryExpression(const std::string &str, bool is_string_literal); // String literal
    PrimaryExpression(bool value);                                     // Bool constant
    PrimaryExpression();                                               // Null constant (special constructor)
    PrimaryExpression(Expression *e);                                  // Parenthesized
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

private:
    // Helper methods for pointer arithmetic
    void handle_pointer_plus_integer(Expression *ptr_expr, Expression *int_expr);
    void handle_pointer_minus_integer(Expression *ptr_expr, Expression *int_expr);
    void handle_pointer_minus_pointer(Expression *left_ptr, Expression *right_ptr);
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
 * Postfix Expression - Postfix increment/decrement
 *
 * Represents: expr++ or expr--
 * Examples: x++, arr[i]++, (*p)++
 *
 * Key difference from prefix:
 * - Returns the OLD value before increment/decrement
 * - Increments/decrements the variable afterward
 *
 * generate_tac():
 * 1. Evaluate the operand expression
 * 2. Save current value to temporary
 * 3. Emit increment/decrement on the variable
 * 4. Result is the saved temporary (old value)
 */
class PostfixExpression : public Expression
{
public:
    TACOp op;         // TAC_POST_INC or TAC_POST_DEC
    Expression *expr; // The operand being modified

    PostfixExpression(TACOp operation, Expression *e);
    virtual ~PostfixExpression();

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

/**
 * Array Initializer Expression - {expr1, expr2, ...}
 * Handles array initialization lists like {1, 2, 3, 4, 5}
 */
class ArrayInitializerExpression : public Expression
{
public:
    std::vector<Expression *> initializers; // List of initializer expressions

    ArrayInitializerExpression(const std::vector<Expression *> &init_list);
    virtual ~ArrayInitializerExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

// ============================================================================
// Function Call Expression - f(arg1, arg2, ...)
// ============================================================================
class CallExpression : public Expression
{
public:
    std::string func_name;
    std::vector<Expression *> args;

    CallExpression(const std::string &name, const std::vector<Expression *> &a)
        : func_name(name), args(a) {}
    virtual ~CallExpression()
    {
        for (auto *e : args)
            delete e;
    }

    std::string to_string() const override
    {
        return func_name + "(...)"; // simplified
    }
    void generate_tac() override;
};

CallExpression *create_call_expression(const std::string &name, const std::vector<Expression *> &args);

// ============================================================================
// Member Access Expressions - struct.member and ptr->member
// ============================================================================

/**
 * Member Access Expression - struct.member
 *
 * Represents: struct_var.member_name
 * Example: node.value
 *
 * generate_tac():
 * - Lookup struct type and get member offset
 * - Generate address arithmetic: base_addr + offset
 * - Load value from computed address
 */
class MemberAccessExpression : public Expression
{
public:
    Expression *struct_expr; // The struct variable expression
    std::string member_name; // The member being accessed

    MemberAccessExpression(Expression *s_expr, const std::string &member);
    virtual ~MemberAccessExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Member Access Pointer Expression - ptr->member
 *
 * Represents: ptr->member_name (equivalent to (*ptr).member_name)
 * Example: node->next
 *
 * generate_tac():
 * - Dereference pointer to get struct
 * - Lookup struct type and get member offset
 * - Generate address arithmetic: base_addr + offset
 * - Load value from computed address
 */
class MemberAccessPtrExpression : public Expression
{
public:
    Expression *ptr_expr;    // The pointer to struct expression
    std::string member_name; // The member being accessed

    MemberAccessPtrExpression(Expression *p_expr, const std::string &member);
    virtual ~MemberAccessPtrExpression();

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
PrimaryExpression *create_string_literal_expression(const std::string &str);

// Overloaded versions with location info
PrimaryExpression *create_primary_expression(const std::string &name, int line, int col);
PrimaryExpression *create_primary_expression(int value, int line, int col);
PrimaryExpression *create_primary_expression(char value, int line, int col);
PrimaryExpression *create_primary_expression(double value, int line, int col);

// Create parenthesized expression
PrimaryExpression *create_paren_expression(Expression *expr);

// Create special constants
PrimaryExpression *create_bool_constant_expression(bool value);
PrimaryExpression *create_null_constant_expression();

// Create other expressions
BinaryExpression *create_binary_expression(Expression *left, TACOp op, Expression *right);
UnaryExpression *create_unary_expression(TACOp op, Expression *expr);
PostfixExpression *create_postfix_expression(TACOp op, Expression *expr);
AssignmentExpression *create_assignment_expression(const std::string &var, Expression *rhs);
GeneralAssignmentExpression *create_general_assignment_expression(Expression *lhs, Expression *rhs);
ArrayAccessExpression *create_array_access_expression(Expression *array, Expression *index);
ArrayInitializerExpression *create_array_initializer_expression(const std::vector<Expression *> &init_list);
MemberAccessExpression *create_member_access_expression(Expression *struct_expr, const std::string &member);
MemberAccessPtrExpression *create_member_access_ptr_expression(Expression *ptr_expr, const std::string &member);

#endif // EXPRESSION_H
