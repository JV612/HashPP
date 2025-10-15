#ifndef AST_H
#define AST_H

#include <string>
#include <vector>
#include "tac.h"

// ============================================================================
// AST Header - Phase 1 Compiler
// ============================================================================
// Abstract Syntax Tree node definitions for:
//   - PrimaryExpression: identifiers, constants (int, char, double)
//   - BinaryExpression: arithmetic, bitwise, comparison operators
//   - UnaryExpression: prefix inc/dec, negation, bitwise NOT
//   - AssignmentExpression: variable = expression
//   - VariableDeclaration: type var = initializer
//
// Each node tracks:
//   - Type information (for type checking)
//   - TAC code (instruction list)
//   - TAC result (operand where result is stored)
//   - Location (line, column for error messages)
// ============================================================================

// Forward declarations
class Type;

// ============================================================================
// Base AST Node
// ============================================================================

class ASTNode
{
public:
    int line_no;
    int column_no;

    ASTNode() : line_no(0), column_no(0) {}
    virtual ~ASTNode() {}

    virtual std::string to_string() const = 0;
};

// ============================================================================
// Expression Nodes (All expressions inherit from Expression base class)
// ============================================================================

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

    // Generate TAC for this expression
    virtual void generate_tac() = 0;
};

// Primary Expression - identifiers, constants, (expr)
class PrimaryExpression : public Expression
{
public:
    enum PrimaryType
    {
        PRIM_IDENTIFIER,
        PRIM_INT_CONSTANT,
        PRIM_CHAR_CONSTANT,
        PRIM_FLOAT_CONSTANT,
        PRIM_PAREN_EXPR
    };

    PrimaryType prim_type;
    std::string name;   // For PRIM_IDENTIFIER
    int int_value;      // For PRIM_INT_CONSTANT
    char char_value;    // For PRIM_CHAR_CONSTANT
    double float_value; // For PRIM_FLOAT_CONSTANT (stores both float and double)
    Expression *expr;   // For PRIM_PAREN_EXPR

    PrimaryExpression(const std::string &id_name); // Identifier
    PrimaryExpression(int value);                  // Int constant
    PrimaryExpression(char value);                 // Char constant
    PrimaryExpression(double value);               // Float/double constant
    PrimaryExpression(Expression *e);              // Parenthesized
    virtual ~PrimaryExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

// Binary Expression - left op right
class BinaryExpression : public Expression
{
public:
    TACOp op;
    Expression *left;
    Expression *right;

    BinaryExpression(Expression *l, TACOp operation, Expression *r);
    virtual ~BinaryExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

// Unary Expression - op expr
class UnaryExpression : public Expression
{
public:
    TACOp op;
    Expression *expr;

    UnaryExpression(TACOp operation, Expression *e);
    virtual ~UnaryExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

// Assignment Expression - lhs = rhs
class AssignmentExpression : public Expression
{
public:
    std::string lhs_name; // Variable name
    Expression *rhs;      // Right-hand side expression

    AssignmentExpression(const std::string &var, Expression *rhs_expr);
    virtual ~AssignmentExpression();

    std::string to_string() const override;
    void generate_tac() override;
};

// ============================================================================
// Statement Nodes
// ============================================================================

class Statement : public ASTNode
{
public:
    std::vector<TACInstruction *> code; // TAC generated for this statement
    InstructionList nextlist;           // List of gotos to be backpatched to next statement

    Statement() {}
    virtual ~Statement();

    virtual void generate_tac() = 0;
};

// Marker - captures current instruction position (for M non-terminal)
class Marker
{
public:
    int instr; // Instruction number where marker was placed

    Marker() : instr(tacGen.nextinstr()) {}
};

// If Statement (if-else)
class IfStatement : public Statement
{
public:
    Expression *condition;
    Statement *then_stmt;
    Statement *else_stmt; // nullptr if no else clause

    IfStatement(Expression *cond, Statement *then_s, Statement *else_s = nullptr);
    virtual ~IfStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

// While Statement
class WhileStatement : public Statement
{
public:
    Expression *condition;
    Statement *body;

    WhileStatement(Expression *cond, Statement *body_stmt);
    virtual ~WhileStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

// Expression Statement (expression followed by semicolon)
class ExpressionStatement : public Statement
{
public:
    Expression *expr; // nullptr for empty statement (just semicolon)

    ExpressionStatement(Expression *e = nullptr);
    virtual ~ExpressionStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

// Compound Statement (block with multiple statements)
class CompoundStatement : public Statement
{
public:
    std::vector<Statement *> statements;

    CompoundStatement();
    virtual ~CompoundStatement();

    void add_statement(Statement *stmt);
    std::string to_string() const override;
    void generate_tac() override;
};

// ============================================================================
// Declaration Nodes
// ============================================================================

class Declaration : public ASTNode
{
public:
    Type *decl_type;
    std::vector<TACInstruction *> code; // TAC generated for initializers

    Declaration() : decl_type(nullptr) {}
    virtual ~Declaration();

    virtual void generate_tac() = 0;
};

// Simple variable declaration
class VariableDeclaration : public Declaration
{
public:
    std::string var_name;
    Expression *initializer; // nullptr if no initializer

    VariableDeclaration(Type *t, const std::string &name, Expression *init = nullptr);
    virtual ~VariableDeclaration();

    std::string to_string() const override;
    void generate_tac() override;
};

// ============================================================================
// Helper Functions
// ============================================================================

// Create expression nodes
PrimaryExpression *create_primary_expression(const std::string &name);
PrimaryExpression *create_primary_expression(int value);
PrimaryExpression *create_primary_expression(char value);
PrimaryExpression *create_primary_expression(double value);

// Overloaded versions with location info
PrimaryExpression *create_primary_expression(const std::string &name, int line, int col);
PrimaryExpression *create_primary_expression(int value, int line, int col);
PrimaryExpression *create_primary_expression(char value, int line, int col);
PrimaryExpression *create_primary_expression(double value, int line, int col);

PrimaryExpression *create_paren_expression(Expression *expr);

BinaryExpression *create_binary_expression(Expression *left, TACOp op, Expression *right);
UnaryExpression *create_unary_expression(TACOp op, Expression *expr);
AssignmentExpression *create_assignment_expression(const std::string &var, Expression *rhs);

// Create declaration nodes
VariableDeclaration *create_variable_declaration(Type *type, const std::string &name,
                                                 Expression *init = nullptr);

// Create statement nodes
IfStatement *create_if_statement(Expression *cond, Statement *then_stmt, Statement *else_stmt = nullptr);
WhileStatement *create_while_statement(Expression *cond, Statement *body);
ExpressionStatement *create_expression_statement(Expression *expr = nullptr);
CompoundStatement *create_compound_statement();

#endif // AST_H
