#include "ast.h"
#include "symbol_table.h"
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

// ============================================================================
// AST Implementation - Phase 1 Compiler
// ============================================================================
// This file contains:
//   1. Expression Base Class
//   2. Primary Expressions (identifiers, constants)
//   3. Binary Expressions (arithmetic, bitwise, comparison)
//   4. Unary Expressions (prefix inc/dec, negation, bitwise NOT)
//   5. Assignment Expressions
//   6. Variable Declarations
//   7. Helper Functions for AST creation
//
// Each expression node implements:
//   - Constructor/Destructor
//   - to_string() for debugging
//   - generate_tac() for code generation with type checking
// ============================================================================

// ============================================================================
// Expression Base Class
// ============================================================================

Expression::~Expression()
{
    delete type;
    delete result;
    // Note: code vector contains pointers managed by TACGenerator
}

// ============================================================================
// PRIMARY EXPRESSIONS (Identifiers, Constants)
// Handles: variables, int literals, char literals, float/double literals
// ============================================================================

PrimaryExpression::PrimaryExpression(const string &id_name)
    : prim_type(PRIM_IDENTIFIER), name(id_name), int_value(0), char_value('\0'), float_value(0.0), expr(nullptr)
{
}

PrimaryExpression::PrimaryExpression(int value)
    : prim_type(PRIM_INT_CONSTANT), int_value(value), char_value('\0'), float_value(0.0), expr(nullptr)
{
}

PrimaryExpression::PrimaryExpression(char value)
    : prim_type(PRIM_CHAR_CONSTANT), int_value(0), char_value(value), float_value(0.0), expr(nullptr)
{
}

PrimaryExpression::PrimaryExpression(double value)
    : prim_type(PRIM_FLOAT_CONSTANT), int_value(0), char_value('\0'), float_value(value), expr(nullptr)
{
}

PrimaryExpression::PrimaryExpression(Expression *e)
    : prim_type(PRIM_PAREN_EXPR), int_value(0), char_value('\0'), float_value(0.0), expr(e)
{
}

PrimaryExpression::~PrimaryExpression()
{
    if (expr)
        delete expr;
}

string PrimaryExpression::to_string() const
{
    switch (prim_type)
    {
    case PRIM_IDENTIFIER:
        return name;
    case PRIM_INT_CONSTANT:
        return std::to_string(int_value);
    case PRIM_CHAR_CONSTANT:
        // Show as character literal with proper escaping
        if (char_value == '\n')
            return "'\\n'";
        if (char_value == '\t')
            return "'\\t'";
        if (char_value == '\r')
            return "'\\r'";
        if (char_value == '\\')
            return "'\\\\'";
        if (char_value == '\'')
            return "'\\''";
        if (char_value == '\0')
            return "'\\0'";
        return "'" + std::string(1, char_value) + "'";
    case PRIM_FLOAT_CONSTANT:
        return std::to_string(float_value);
    case PRIM_PAREN_EXPR:
        return "(" + expr->to_string() + ")";
    }
    return "";
}

void PrimaryExpression::generate_tac()
{
    switch (prim_type)
    {
    case PRIM_IDENTIFIER:
    {
        // Look up in symbol table
        Symbol *sym = symbolTable.lookup(name);
        if (!sym)
        {
            cerr << "Error: Undefined variable '" << name << "'" << endl;
            result = new TACOperand(TACOperand::OPERAND_IDENTIFIER, name);
        }
        else
        {
            result = new TACOperand(TACOperand::OPERAND_IDENTIFIER, name);
            type = new Type(sym->type);
        }
        break;
    }

    case PRIM_INT_CONSTANT:
    {
        result = new TACOperand(TACOperand::OPERAND_CONSTANT, std::to_string(int_value));
        type = new Type(TYPE_INT);
        break;
    }

    case PRIM_CHAR_CONSTANT:
    {
        // Create character literal representation for TAC
        string char_repr;
        if (char_value == '\n')
            char_repr = "'\\n'";
        else if (char_value == '\t')
            char_repr = "'\\t'";
        else if (char_value == '\r')
            char_repr = "'\\r'";
        else if (char_value == '\\')
            char_repr = "'\\\\'";
        else if (char_value == '\'')
            char_repr = "'\\''";
        else if (char_value == '\0')
            char_repr = "'\\0'";
        else
            char_repr = "'" + string(1, char_value) + "'";

        result = new TACOperand(TACOperand::OPERAND_CONSTANT, char_repr);
        type = new Type(TYPE_CHAR);

        int ascii_value = (int)char_value;
        cout << "[AST] Character constant: " << char_repr
             << " (ASCII: " << ascii_value << ")" << endl;
        break;
    }

    case PRIM_FLOAT_CONSTANT:
    {
        result = new TACOperand(TACOperand::OPERAND_CONSTANT, std::to_string(float_value));
        type = new Type(TYPE_FLOAT);
        cout << "[AST] Float constant: " << float_value << endl;
        break;
    }

    case PRIM_PAREN_EXPR:
    {
        // Generate code for inner expression
        expr->generate_tac();
        result = expr->result;
        type = expr->type;
        code = expr->code;
        // IMPORTANT: Copy truelist/falselist for boolean expressions
        truelist = expr->truelist;
        falselist = expr->falselist;
        break;
    }
    }
}

// ============================================================================
// BINARY EXPRESSIONS (Two operands with operator)
// Handles: +, -, *, /, %, &, |, ^, <<, >>, <, >, <=, >=, ==, !=
// Type checking: Validates operand types and performs type promotion
// Result: Temporary variable with computed value
// ============================================================================

BinaryExpression::BinaryExpression(Expression *l, TACOp operation, Expression *r)
    : op(operation), left(l), right(r)
{
}

BinaryExpression::~BinaryExpression()
{
    delete left;
    delete right;
}

string BinaryExpression::to_string() const
{
    string op_str;
    switch (op)
    {
    case TAC_ADD:
        op_str = "+";
        break;
    case TAC_SUB:
        op_str = "-";
        break;
    case TAC_MUL:
        op_str = "*";
        break;
    case TAC_DIV:
        op_str = "/";
        break;
    case TAC_MOD:
        op_str = "%";
        break;
    default:
        op_str = "?";
        break;
    }
    return left->to_string() + " " + op_str + " " + right->to_string();
}

void BinaryExpression::generate_tac()
{
    // ========================================================================
    // SPECIAL HANDLING FOR BOOLEAN OPERATORS (short-circuit evaluation)
    // ========================================================================
    if (op == TAC_LOGICAL_AND)
    {
        // E -> E1 && M E2
        // E1.truelist is backpatched to M (start of E2)
        // E.falselist = merge(E1.falselist, E2.falselist)
        // E.truelist = E2.truelist

        left->generate_tac();
        code = left->code;

        // M: marker - current position before E2
        int M = tacGen.nextinstr();
        backpatch(left->truelist, M);

        right->generate_tac();
        code.insert(code.end(), right->code.begin(), right->code.end());

        // Merge lists
        falselist = merge(left->falselist, right->falselist);
        truelist = right->truelist;

        // No result operand for boolean control flow
        result = nullptr;
        type = new Type(TYPE_INT);

        // Type checking
        if (!left->type || !right->type || left->type->is_error() || right->type->is_error())
        {
            type = new Type(TYPE_ERROR);
        }
        return;
    }

    if (op == TAC_LOGICAL_OR)
    {
        // E -> E1 || M E2
        // E1.falselist is backpatched to M (start of E2)
        // E.truelist = merge(E1.truelist, E2.truelist)
        // E.falselist = E2.falselist

        left->generate_tac();
        code = left->code;

        // M: marker - current position before E2
        int M = tacGen.nextinstr();
        backpatch(left->falselist, M);

        right->generate_tac();
        code.insert(code.end(), right->code.begin(), right->code.end());

        // Merge lists
        truelist = merge(left->truelist, right->truelist);
        falselist = right->falselist;

        // No result operand for boolean control flow
        result = nullptr;
        type = new Type(TYPE_INT);

        // Type checking
        if (!left->type || !right->type || left->type->is_error() || right->type->is_error())
        {
            type = new Type(TYPE_ERROR);
        }
        return;
    }

    // ========================================================================
    // REGULAR OPERATORS: Generate code for both operands first
    // ========================================================================
    left->generate_tac();
    right->generate_tac();

    // ========================================================================
    // STEP 2: TYPE CHECKING (Phase 1)
    // Validates operand types and determines result type
    // ========================================================================

    // Check if operands have types
    if (!left->type || !right->type)
    {
        fprintf(stderr, "[Type Error] Line %d: Missing type information in binary expression\n",
                line_no);
        type = new Type(TYPE_ERROR);
        return;
    }

    // Error propagation: if either operand is already an error, propagate it
    if (left->type->is_error() || right->type->is_error())
    {
        type = new Type(TYPE_ERROR);
        return;
    }

    // Operator-specific type checking
    const char *op_name = nullptr;
    switch (op)
    {
    case TAC_ADD:
        op_name = "+";
        break;
    case TAC_SUB:
        op_name = "-";
        break;
    case TAC_MUL:
        op_name = "*";
        break;
    case TAC_DIV:
        op_name = "/";
        break;
    case TAC_MOD:
        op_name = "%";
        break;
    case TAC_BITWISE_AND:
        op_name = "&";
        break;
    case TAC_BITWISE_OR:
        op_name = "|";
        break;
    case TAC_BITWISE_XOR:
        op_name = "^";
        break;
    case TAC_LEFT_SHIFT:
        op_name = "<<";
        break;
    case TAC_RIGHT_SHIFT:
        op_name = ">>";
        break;
    case TAC_LT:
        op_name = "<";
        break;
    case TAC_GT:
        op_name = ">";
        break;
    case TAC_LE:
        op_name = "<=";
        break;
    case TAC_GE:
        op_name = ">=";
        break;
    case TAC_EQ:
        op_name = "==";
        break;
    case TAC_NE:
        op_name = "!=";
        break;
    case TAC_LOGICAL_AND:
        op_name = "&&";
        break;
    case TAC_LOGICAL_OR:
        op_name = "||";
        break;
    default:
        op_name = "unknown";
        break;
    }

    // Modulo operator requires integer operands only
    if (op == TAC_MOD)
    {
        if (!left->type->is_integer() || !right->type->is_integer())
        {
            fprintf(stderr, "[Type Error] Line %d: Modulo operator '%%' requires integer operands, got %s and %s\n",
                    line_no, left->type->to_string().c_str(), right->type->to_string().c_str());
            type = new Type(TYPE_ERROR);
            return;
        }
    }
    // Bitwise operators require integer operands (int/char, not float/double)
    else if (op == TAC_BITWISE_AND || op == TAC_BITWISE_OR || op == TAC_BITWISE_XOR ||
             op == TAC_LEFT_SHIFT || op == TAC_RIGHT_SHIFT)
    {
        if (!left->type->is_integer() || !right->type->is_integer())
        {
            fprintf(stderr, "[Type Error] Line %d: Bitwise operator '%s' requires integer operands, got %s and %s\n",
                    line_no, op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            type = new Type(TYPE_ERROR);
            return;
        }
    }
    // Comparison operators require numeric operands
    else if (op == TAC_LT || op == TAC_GT || op == TAC_LE || op == TAC_GE ||
             op == TAC_EQ || op == TAC_NE)
    {
        if (!left->type->is_numeric() || !right->type->is_numeric())
        {
            fprintf(stderr, "[Type Error] Line %d: Comparison operator '%s' requires numeric operands, got %s and %s\n",
                    line_no, op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            type = new Type(TYPE_ERROR);
            return;
        }
    }
    // Logical operators require numeric operands (C semantics: any numeric is "truthy")
    else if (op == TAC_LOGICAL_AND || op == TAC_LOGICAL_OR)
    {
        if (!left->type->is_numeric() || !right->type->is_numeric())
        {
            fprintf(stderr, "[Type Error] Line %d: Logical operator '%s' requires numeric operands, got %s and %s\n",
                    line_no, op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            type = new Type(TYPE_ERROR);
            return;
        }
    }
    // Other arithmetic operators require numeric operands
    else if (op == TAC_ADD || op == TAC_SUB || op == TAC_MUL || op == TAC_DIV)
    {
        if (!left->type->is_numeric() || !right->type->is_numeric())
        {
            fprintf(stderr, "[Type Error] Line %d: Operator '%s' requires numeric operands, got %s and %s\n",
                    line_no, op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            type = new Type(TYPE_ERROR);
            return;
        }
    }

    // Determine result type using promotion rules
    // Phase 1: For bitwise/comparison/logical ops, result is int; for arithmetic, use type promotion
    if (op == TAC_BITWISE_AND || op == TAC_BITWISE_OR || op == TAC_BITWISE_XOR ||
        op == TAC_LEFT_SHIFT || op == TAC_RIGHT_SHIFT || op == TAC_MOD ||
        op == TAC_LT || op == TAC_GT || op == TAC_LE || op == TAC_GE ||
        op == TAC_EQ || op == TAC_NE ||
        op == TAC_LOGICAL_AND || op == TAC_LOGICAL_OR)
    {
        // Bitwise, comparison, and logical operations result in int
        // Comparison/logical return 1 (true) or 0 (false) as standard C behavior
        type = new Type(TYPE_INT);
    }
    else
    {
        // Arithmetic operations use type promotion (char → int → float)
        type = new Type(left->type->promote_with(*right->type));
    }

    // ========================================================================
    // STEP 3: TAC GENERATION (follows lecture's emit pattern)
    // E.code = E1.code || E2.code || emit(op, E1.place, E2.place, E.place)
    // ========================================================================

    // Combine code from both sides
    code.insert(code.end(), left->code.begin(), left->code.end());
    code.insert(code.end(), right->code.begin(), right->code.end());

    // Check if this is a RELATIONAL expression (comparison operators only)
    // Logical AND/OR are handled separately above with short-circuit evaluation
    bool is_relational = (op == TAC_LT || op == TAC_GT || op == TAC_LE || op == TAC_GE ||
                          op == TAC_EQ || op == TAC_NE);

    if (is_relational)
    {
        // For relational expressions, generate comparison + conditional jumps
        // E.truelist and E.falselist for backpatching

        // Create new temporary for the comparison result
        TACOperand temp = tacGen.newTemp();
        result = new TACOperand(temp);

        // Emit the comparison operation
        tacGen.emit(op, *result, *left->result, *right->result);
        code.push_back(tacGen.getCode().back());

        // Generate "if temp goto ___" (truelist) and "goto ___" (falselist)
        int true_jump = tacGen.emit(TAC_IF_GOTO, TACOperand(), *result);
        int false_jump = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());

        truelist = makelist(true_jump);
        falselist = makelist(false_jump);
    }
    else
    {
        // Arithmetic/bitwise operations: compute value normally
        // Create new temporary for result
        TACOperand temp = tacGen.newTemp();
        result = new TACOperand(temp);

        // Emit the operation
        tacGen.emit(op, *result, *left->result, *right->result);

        // Add this instruction to our code
        code.push_back(tacGen.getCode().back());
    }
}

// ============================================================================
// UNARY EXPRESSIONS (Single operand with operator)
// Handles: -, +, ~, ++x, --x (prefix increment/decrement)
// Type checking: Validates operand type based on operator
// Special: Prefix inc/dec modify the variable in-place
// ============================================================================

UnaryExpression::UnaryExpression(TACOp operation, Expression *e)
    : op(operation), expr(e)
{
}

UnaryExpression::~UnaryExpression()
{
    delete expr;
}

string UnaryExpression::to_string() const
{
    string op_str;
    switch (op)
    {
    case TAC_UMINUS:
        op_str = "-";
        break;
    case TAC_UPLUS:
        op_str = "+";
        break;
    case TAC_BITWISE_NOT:
        op_str = "~";
        break;
    case TAC_LOGICAL_NOT:
        op_str = "!";
        break;
    case TAC_PRE_INC:
        op_str = "++";
        break;
    case TAC_PRE_DEC:
        op_str = "--";
        break;
    default:
        op_str = "?";
        break;
    }
    return op_str + expr->to_string();
}

void UnaryExpression::generate_tac()
{
    // Generate code for operand
    expr->generate_tac();

    // ========================================================================
    // Phase 1: Type Checking for Unary Operations
    // ========================================================================

    // Check if operand has a type
    if (!expr->type)
    {
        fprintf(stderr, "[Type Error] Line %d: Missing type information in unary expression\n",
                line_no);
        type = new Type(TYPE_ERROR);
        return;
    }

    // Error propagation
    if (expr->type->is_error())
    {
        type = new Type(TYPE_ERROR);
        return;
    }

    // Unary + and - require numeric operands
    if (op == TAC_UMINUS || op == TAC_UPLUS)
    {
        if (!expr->type->is_numeric())
        {
            const char *op_name = (op == TAC_UMINUS) ? "-" : "+";
            fprintf(stderr, "[Type Error] Line %d: Unary '%s' requires numeric operand, got %s\n",
                    line_no, op_name, expr->type->to_string().c_str());
            type = new Type(TYPE_ERROR);
            return;
        }
    }
    // Bitwise NOT requires integer operand
    else if (op == TAC_BITWISE_NOT)
    {
        if (!expr->type->is_integer())
        {
            fprintf(stderr, "[Type Error] Line %d: Bitwise NOT '~' requires integer operand, got %s\n",
                    line_no, expr->type->to_string().c_str());
            type = new Type(TYPE_ERROR);
            return;
        }
    }
    // Logical NOT requires numeric operand (C semantics: any numeric is "truthy")
    else if (op == TAC_LOGICAL_NOT)
    {
        if (!expr->type->is_numeric())
        {
            fprintf(stderr, "[Type Error] Line %d: Logical NOT '!' requires numeric operand, got %s\n",
                    line_no, expr->type->to_string().c_str());
            type = new Type(TYPE_ERROR);
            return;
        }
    }
    // Prefix increment/decrement require numeric operands
    else if (op == TAC_PRE_INC || op == TAC_PRE_DEC)
    {
        if (!expr->type->is_numeric())
        {
            const char *op_name = (op == TAC_PRE_INC) ? "++" : "--";
            fprintf(stderr, "[Type Error] Line %d: Prefix '%s' requires numeric operand, got %s\n",
                    line_no, op_name, expr->type->to_string().c_str());
            type = new Type(TYPE_ERROR);
            return;
        }
    }

    // Result type: bitwise NOT and logical NOT return int; others keep operand type
    if (op == TAC_BITWISE_NOT || op == TAC_LOGICAL_NOT)
    {
        // Bitwise NOT promotes char to int
        // Logical NOT returns int (1 for false, 0 for true in C)
        type = new Type(TYPE_INT);
    }
    else
    {
        type = new Type(*expr->type);
    }

    // ========================================================================
    // TAC Generation (only if type is valid)
    // ========================================================================

    // Copy code from operand
    code = expr->code;

    // Special handling for prefix increment/decrement
    if (op == TAC_PRE_INC || op == TAC_PRE_DEC)
    {
        // For ++x or --x, the operand should be a variable (lvalue)
        // Result is the variable itself after modification

        // The result is the variable being modified
        result = new TACOperand(*expr->result);

        // Emit: x = x + 1 or x = x - 1
        tacGen.emit(op, *result, TACOperand());
        code.push_back(tacGen.getCode().back());
    }
    else
    {
        // Regular unary operations: create new temporary
        TACOperand temp = tacGen.newTemp();
        result = new TACOperand(temp);

        // Emit the operation
        tacGen.emit(op, *result, *expr->result);
        code.push_back(tacGen.getCode().back());
    }
}

// ============================================================================
// ASSIGNMENT EXPRESSIONS (lvalue = rvalue)
// Handles: variable assignment with type compatibility checking
// Type checking: Validates lvalue exists and types are compatible
// Warnings: Emits warnings for implicit type conversions
// ============================================================================

AssignmentExpression::AssignmentExpression(const string &var, Expression *rhs_expr)
    : lhs_name(var), rhs(rhs_expr)
{
}

AssignmentExpression::~AssignmentExpression()
{
    delete rhs;
}

string AssignmentExpression::to_string() const
{
    return lhs_name + " = " + rhs->to_string();
}

void AssignmentExpression::generate_tac()
{
    // Generate code for right-hand side
    rhs->generate_tac();

    // ========================================================================
    // Phase 1: Type Checking for Assignment
    // ========================================================================

    // Check if RHS has a type
    if (!rhs->type)
    {
        fprintf(stderr, "[Type Error] Line %d: Missing type information in assignment\n",
                line_no);
        type = new Type(TYPE_ERROR);
        return;
    }

    // Error propagation from RHS
    if (rhs->type->is_error())
    {
        type = new Type(TYPE_ERROR);
        return;
    }

    // Look up LHS variable in symbol table
    Symbol *sym = symbolTable.lookup(lhs_name);
    if (!sym)
    {
        fprintf(stderr, "[Type Error] Line %d: Undefined variable '%s'\n",
                line_no, lhs_name.c_str());
        type = new Type(TYPE_ERROR);
        return;
    }

    // Check type compatibility (warning for mismatches, like C)
    // Phase 1: We allow assignments between different numeric types but warn
    if (sym->type.base_type != rhs->type->base_type)
    {
        // Only warn if both are numeric (allow implicit conversions)
        if (sym->type.is_numeric() && rhs->type->is_numeric())
        {
            fprintf(stderr, "[Type Warning] Line %d: Implicit conversion in assignment from %s to %s\n",
                    line_no, rhs->type->to_string().c_str(), sym->type.to_string().c_str());
        }
        else
        {
            // Error for non-numeric type mismatches
            fprintf(stderr, "[Type Error] Line %d: Cannot assign %s to %s\n",
                    line_no, rhs->type->to_string().c_str(), sym->type.to_string().c_str());
            type = new Type(TYPE_ERROR);
            return;
        }
    }

    // Assignment type is the LHS type
    type = new Type(sym->type);

    // ========================================================================
    // TAC Generation (only if types are valid)
    // ========================================================================

    // Copy code from RHS
    code = rhs->code;

    // Create operand for LHS
    TACOperand lhs(TACOperand::OPERAND_IDENTIFIER, lhs_name);

    // Emit assignment
    tacGen.emit(TAC_ASSIGN, lhs, *rhs->result);
    code.push_back(tacGen.getCode().back());

    // Result is the LHS
    result = new TACOperand(lhs);
}

// ============================================================================
// Declaration Base Class
// ============================================================================

Declaration::~Declaration()
{
    delete decl_type;
    // Note: code vector contains pointers managed by TACGenerator
}

// ============================================================================
// ============================================================================
// VARIABLE DECLARATIONS
// Handles: type varname = initializer
// Inserts into symbol table and validates initializer type compatibility
// Phase 1: Basic declarations with int, char, double types
// ============================================================================

// ============================================================================
// STATEMENT NODES - Control Flow
// ============================================================================
// Statements control the flow of execution:
//   - IfStatement: if-else branches with labels and jumps
//   - WhileStatement: loops with labels and conditional jumps
//   - ExpressionStatement: single expression followed by semicolon
//   - CompoundStatement: block of statements in braces {}
// ============================================================================

Statement::~Statement()
{
    for (TACInstruction *instr : code)
    {
        delete instr;
    }
}

// ============================================================================
// IfStatement - if (condition) then_stmt else else_stmt
// ============================================================================
// TAC Pattern with ELSE:
//   <condition.code>
//   ifFalse condition.result goto L_else
//   <then_stmt.code>
//   goto L_end
//   L_else:
//   <else_stmt.code>
//   L_end:
//
// TAC Pattern without ELSE:
//   <condition.code>
//   ifFalse condition.result goto L_end
//   <then_stmt.code>
//   L_end:
// ============================================================================

IfStatement::IfStatement(Expression *cond, Statement *then_s, Statement *else_s)
    : condition(cond), then_stmt(then_s), else_stmt(else_s)
{
}

IfStatement::~IfStatement()
{
    delete condition;
    delete then_stmt;
    if (else_stmt)
        delete else_stmt;
}

string IfStatement::to_string() const
{
    string result = "if (" + condition->to_string() + ") " + then_stmt->to_string();
    if (else_stmt)
    {
        result += " else " + else_stmt->to_string();
    }
    return result;
}

void IfStatement::generate_tac()
{
    // ========================================================================
    // Backpatching-based if-then-else translation
    // ========================================================================
    // Grammar: if (B) M1 S1 N else M2 S2
    //
    // B.truelist = backpatch to M1.instr
    // B.falselist = backpatch to M2.instr (or after S1 if no else)
    // N generates goto and adds to S.nextlist
    // ========================================================================

    // STEP 1: Generate code for condition
    condition->generate_tac();
    code = condition->code;

    if (else_stmt)
    {
        // If-else case: if (B) M1 S1 N else M2 S2

        // M1: marker before then statement
        int M1 = tacGen.nextinstr();

        // Backpatch B.truelist to M1
        backpatch(condition->truelist, M1);

        // Generate then statement
        then_stmt->generate_tac();
        code.insert(code.end(), then_stmt->code.begin(), then_stmt->code.end());

        // N: generate goto to skip else (will be backpatched later)
        int N_goto = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
        InstructionList N_list = makelist(N_goto);

        // M2: marker before else statement
        int M2 = tacGen.nextinstr();

        // Backpatch B.falselist to M2
        backpatch(condition->falselist, M2);

        // Generate else statement
        else_stmt->generate_tac();
        code.insert(code.end(), else_stmt->code.begin(), else_stmt->code.end());

        // S.nextlist = merge(S1.nextlist, N, S2.nextlist)
        nextlist = merge(then_stmt->nextlist, N_list);
        nextlist = merge(nextlist, else_stmt->nextlist);
    }
    else
    {
        // If-only case: if (B) M S

        // M: marker before then statement
        int M = tacGen.nextinstr();

        // Backpatch B.truelist to M
        backpatch(condition->truelist, M);

        // Generate then statement
        then_stmt->generate_tac();
        code.insert(code.end(), then_stmt->code.begin(), then_stmt->code.end());

        // S.nextlist = merge(B.falselist, S1.nextlist)
        nextlist = merge(condition->falselist, then_stmt->nextlist);
    }

    printf("[AST] IfStatement: Generated TAC with backpatching\n");
}

// ============================================================================
// WhileStatement - while (condition) body
// ============================================================================
// TAC Pattern:
//   L_begin:
//   <condition.code>
//   ifFalse condition.result goto L_end
//   <body.code>
//   goto L_begin
//   L_end:
// ============================================================================

WhileStatement::WhileStatement(Expression *cond, Statement *body_stmt)
    : condition(cond), body(body_stmt)
{
}

WhileStatement::~WhileStatement()
{
    delete condition;
    delete body;
}

string WhileStatement::to_string() const
{
    return "while (" + condition->to_string() + ") " + body->to_string();
}

void WhileStatement::generate_tac()
{
    // ========================================================================
    // Backpatching-based while loop translation
    // ========================================================================
    // Grammar: while M1 (B) M2 S
    //
    // M1.instr = beginning of loop (for repeat)
    // backpatch B.truelist to M2.instr
    // backpatch S.nextlist to M1.instr
    // S.nextlist = B.falselist
    // ========================================================================

    // M1: beginning of loop
    int M1 = tacGen.nextinstr();

    // Generate code for condition
    condition->generate_tac();
    code = condition->code;

    // M2: start of body
    int M2 = tacGen.nextinstr();

    // Backpatch B.truelist to M2 (enter loop body when true)
    backpatch(condition->truelist, M2);

    // Generate code for body
    body->generate_tac();
    code.insert(code.end(), body->code.begin(), body->code.end());

    // Backpatch S.nextlist (end of body) to M1 (repeat loop)
    backpatch(body->nextlist, M1);

    // Generate goto back to beginning
    int goto_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
    tacGen.getCode()[goto_instr]->target_line = M1;

    // S.nextlist = B.falselist (exit loop when condition is false)
    nextlist = condition->falselist;

    printf("[AST] WhileStatement: Generated TAC with backpatching\n");
}

// ============================================================================
// ExpressionStatement - expression;
// ============================================================================

ExpressionStatement::ExpressionStatement(Expression *e)
    : expr(e)
{
}

ExpressionStatement::~ExpressionStatement()
{
    if (expr)
        delete expr;
}

string ExpressionStatement::to_string() const
{
    if (expr)
        return expr->to_string() + ";";
    return ";";
}

void ExpressionStatement::generate_tac()
{
    if (expr)
    {
        printf("[ExpressionStatement] Generating TAC for expression\n");
        expr->generate_tac();
        code = expr->code;
    }
    else
    {
        printf("[ExpressionStatement] Empty statement (just semicolon)\n");
    }
}

// ============================================================================
// CompoundStatement - { stmt1; stmt2; ... }
// ============================================================================

CompoundStatement::CompoundStatement()
{
}

CompoundStatement::~CompoundStatement()
{
    for (Statement *stmt : statements)
    {
        delete stmt;
    }
}

void CompoundStatement::add_statement(Statement *stmt)
{
    if (stmt)
        statements.push_back(stmt);
}

string CompoundStatement::to_string() const
{
    string result = "{\n";
    for (Statement *stmt : statements)
    {
        result += "  " + stmt->to_string() + "\n";
    }
    result += "}";
    return result;
}

void CompoundStatement::generate_tac()
{
    printf("[CompoundStatement] Generating TAC for %zu statements\n", statements.size());

    InstructionList current_nextlist;

    for (size_t i = 0; i < statements.size(); i++)
    {
        Statement *stmt = statements[i];

        // Backpatch previous statement's nextlist to current position (M)
        int M = tacGen.nextinstr();
        backpatch(current_nextlist, M);

        // Generate code for this statement
        stmt->generate_tac();
        code.insert(code.end(), stmt->code.begin(), stmt->code.end());

        // Update nextlist to this statement's nextlist
        current_nextlist = stmt->nextlist;
    }

    // The compound statement's nextlist is the last statement's nextlist
    nextlist = current_nextlist;
}

// ============================================================================
// Helper Functions - Statement Creation
// ============================================================================

IfStatement *create_if_statement(Expression *cond, Statement *then_stmt, Statement *else_stmt)
{
    return new IfStatement(cond, then_stmt, else_stmt);
}

WhileStatement *create_while_statement(Expression *cond, Statement *body)
{
    return new WhileStatement(cond, body);
}

ExpressionStatement *create_expression_statement(Expression *expr)
{
    return new ExpressionStatement(expr);
}

CompoundStatement *create_compound_statement()
{
    return new CompoundStatement();
}

// ============================================================================
// VARIABLE DECLARATIONS
// ============================================================================

VariableDeclaration::VariableDeclaration(Type *t, const string &name, Expression *init)
    : var_name(name), initializer(init)
{
    decl_type = t;
}

VariableDeclaration::~VariableDeclaration()
{
    if (initializer)
        delete initializer;
}

string VariableDeclaration::to_string() const
{
    string result = decl_type->to_string() + " " + var_name;
    if (initializer)
    {
        result += " = " + initializer->to_string();
    }
    return result;
}

void VariableDeclaration::generate_tac()
{
    cout << "[AST] Variable declaration: " << var_name
         << " (type: " << decl_type->to_string() << ")" << endl;

    // Insert into symbol table
    symbolTable.insert(var_name, *decl_type);

    // If there's an initializer, generate code for it
    if (initializer)
    {
        initializer->generate_tac();

        // ========================================================================
        // Phase 1: Type Checking for Variable Initialization
        // ========================================================================

        // Check if initializer has a type
        if (!initializer->type)
        {
            fprintf(stderr, "[Type Error] Line %d: Missing type information in initializer for '%s'\n",
                    line_no, var_name.c_str());
            return;
        }

        // Error propagation from initializer
        if (initializer->type->is_error())
        {
            return;
        }

        // Check type compatibility
        if (decl_type->base_type != initializer->type->base_type)
        {
            // Only warn if both are numeric (allow implicit conversions)
            if (decl_type->is_numeric() && initializer->type->is_numeric())
            {
                fprintf(stderr, "[Type Warning] Line %d: Implicit conversion in initialization of '%s' from %s to %s\n",
                        line_no, var_name.c_str(), initializer->type->to_string().c_str(), decl_type->to_string().c_str());
            }
            else
            {
                // Error for non-numeric type mismatches
                fprintf(stderr, "[Type Error] Line %d: Cannot initialize '%s' of type %s with value of type %s\n",
                        line_no, var_name.c_str(), decl_type->to_string().c_str(), initializer->type->to_string().c_str());
                return;
            }
        }

        // ========================================================================
        // TAC Generation (only if types are valid)
        // ========================================================================

        code = initializer->code;

        // Generate assignment
        TACOperand lhs(TACOperand::OPERAND_IDENTIFIER, var_name);
        tacGen.emit(TAC_ASSIGN, lhs, *initializer->result);
        code.push_back(tacGen.getCode().back());
    }
}

// ============================================================================
// HELPER FUNCTIONS - AST Node Creation
// ============================================================================
// These functions are called from the parser (ansic.y) to create AST nodes
// Overloaded versions handle different literal types and location tracking
// ============================================================================

PrimaryExpression *create_primary_expression(const string &name)
{
    return new PrimaryExpression(name);
}

PrimaryExpression *create_primary_expression(int value)
{
    return new PrimaryExpression(value);
}

PrimaryExpression *create_primary_expression(char value)
{
    return new PrimaryExpression(value);
}

PrimaryExpression *create_primary_expression(double value)
{
    return new PrimaryExpression(value);
}

// Overloaded versions with location info
PrimaryExpression *create_primary_expression(const string &name, int line, int col)
{
    PrimaryExpression *expr = new PrimaryExpression(name);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

PrimaryExpression *create_primary_expression(int value, int line, int col)
{
    PrimaryExpression *expr = new PrimaryExpression(value);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

PrimaryExpression *create_primary_expression(char value, int line, int col)
{
    PrimaryExpression *expr = new PrimaryExpression(value);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

PrimaryExpression *create_primary_expression(double value, int line, int col)
{
    PrimaryExpression *expr = new PrimaryExpression(value);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

PrimaryExpression *create_paren_expression(Expression *expr)
{
    return new PrimaryExpression(expr);
}

BinaryExpression *create_binary_expression(Expression *left, TACOp op, Expression *right)
{
    return new BinaryExpression(left, op, right);
}

UnaryExpression *create_unary_expression(TACOp op, Expression *expr)
{
    return new UnaryExpression(op, expr);
}

AssignmentExpression *create_assignment_expression(const string &var, Expression *rhs)
{
    return new AssignmentExpression(var, rhs);
}

// ============================================================================
// Helper Functions - Declaration Creation
// ============================================================================

VariableDeclaration *create_variable_declaration(Type *type, const string &name,
                                                 Expression *init)
{
    return new VariableDeclaration(type, name, init);
}
