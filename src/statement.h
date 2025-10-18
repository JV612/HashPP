#ifndef STATEMENT_H
#define STATEMENT_H

#include "ast_base.h"
#include "expression.h"
#include "tac.h"
#include <vector>

// Forward declaration for Declaration
class Declaration;

// ============================================================================
// Statement Nodes - Control Flow
// ============================================================================

/**
 * Base class for all statements
 *
 * Every statement has:
 * - code: list of TAC instructions generated
 * - nextlist: list of goto instructions to be backpatched to next statement
 * - breaklist: list of break gotos (backpatched by enclosing loop to loop exit)
 * - continuelist: list of continue gotos (backpatched by enclosing loop to loop beginning/post)
 *
 * The break/continue lists allow these statements to work from deeply nested positions
 * They "bubble up" through compound statements and if statements until reaching a loop
 */
class Statement : public ASTNode
{
public:
    std::vector<TACInstruction *> code; // TAC generated for this statement
    InstructionList nextlist;           // List of gotos to be backpatched to next statement
    InstructionList breaklist;          // List of break statement gotos (backpatched by enclosing loop)
    InstructionList continuelist;       // List of continue statement gotos (backpatched by enclosing loop)

    Statement() {}
    virtual ~Statement();

    /**
     * Generate Three-Address Code for this statement
     * Sets: code, nextlist, and potentially breaklist/continuelist
     */
    virtual void generate_tac() = 0;
};

/**
 * Marker - Captures current instruction position (for M non-terminal)
 *
 * Used in backpatching to mark positions where jumps should target
 * Records the instruction number at the point where it's created
 */
class Marker
{
public:
    int instr; // Instruction number where marker was placed

    Marker() : instr(tacGen.nextinstr()) {}
};

/**
 * If Statement - Conditional branching
 *
 * Syntax: if (condition) then_stmt [else else_stmt]
 *
 * TAC Pattern with ELSE:
 *   <condition.code>
 *   ifFalse condition.result goto L_else
 *   <then_stmt.code>
 *   goto L_end
 *   L_else:
 *   <else_stmt.code>
 *   L_end:
 *
 * TAC Pattern without ELSE:
 *   <condition.code>
 *   ifFalse condition.result goto L_end
 *   <then_stmt.code>
 *   L_end:
 *
 * generate_tac():
 * - Generates code for condition with markers
 * - Backpatches condition's truelist to then statement start
 * - Backpatches condition's falselist to else statement start (or after if-only)
 * - Propagates break/continue lists from both branches
 */
class IfStatement : public Statement
{
public:
    Expression *condition; // Condition expression
    Statement *then_stmt;  // Statement to execute if true
    Statement *else_stmt;  // Statement to execute if false (nullptr if no else)

    IfStatement(Expression *cond, Statement *then_s, Statement *else_s = nullptr);
    virtual ~IfStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * While Statement - Conditional loop (test-first)
 *
 * Syntax: while (condition) body
 *
 * TAC Pattern:
 *   M1:                     // Loop beginning
 *   <condition.code>
 *   ifFalse condition.result goto EXIT
 *   M2:
 *   <body.code>
 *   goto M1
 *   EXIT:
 *
 * generate_tac():
 * - M1 marks loop beginning (for repeat and continue)
 * - Condition's truelist backpatched to body start (M2)
 * - Body's nextlist backpatched to M1 (repeat)
 * - Condition's falselist becomes statement's nextlist (exit)
 * - Body's breaklist backpatched to EXIT
 * - Body's continuelist backpatched to M1
 */
class WhileStatement : public Statement
{
public:
    Expression *condition; // Loop condition
    Statement *body;       // Loop body

    WhileStatement(Expression *cond, Statement *body_stmt);
    virtual ~WhileStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Do-While Statement - Conditional loop (test-last)
 *
 * Syntax: do body while (condition);
 *
 * TAC Pattern:
 *   M1:                     // Body beginning
 *   <body.code>
 *   M2:                     // Condition beginning
 *   <condition.code>
 *   if condition.result goto M1
 *   EXIT:
 *
 * generate_tac():
 * - M1 marks body beginning (for repeat)
 * - M2 marks condition check (for continue)
 * - Body's nextlist backpatched to M2
 * - Condition's truelist backpatched to M1 (repeat)
 * - Condition's falselist becomes statement's nextlist (exit)
 * - Body's breaklist backpatched to EXIT
 * - Body's continuelist backpatched to M2
 */
class DoWhileStatement : public Statement
{
public:
    Statement *body;       // Loop body
    Expression *condition; // Loop condition (checked after body)

    DoWhileStatement(Statement *body_stmt, Expression *cond);
    virtual ~DoWhileStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Until Statement - Loop until condition becomes true
 *
 * Syntax: until (condition) body
 *
 * Semantics: Loop UNTIL condition is true (opposite of while)
 * Same as: while (!condition) body
 *
 * TAC Pattern:
 *   M1:                     // Loop beginning
 *   <condition.code>
 *   if condition.result goto EXIT    // TRUE = exit!
 *   M2:
 *   <body.code>
 *   goto M1
 *   EXIT:
 *
 * generate_tac():
 * - M1 marks loop beginning (for repeat and continue)
 * - Condition's FALSE list backpatched to body start (M2)
 * - Condition's TRUE list becomes statement's nextlist (exit when true!)
 * - Body's nextlist backpatched to M1 (repeat)
 * - Body's breaklist backpatched to EXIT
 * - Body's continuelist backpatched to M1
 */
class UntilStatement : public Statement
{
public:
    Expression *condition; // Loop condition (exits when TRUE)
    Statement *body;       // Loop body

    UntilStatement(Expression *cond, Statement *body_stmt);
    virtual ~UntilStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * For Statement - C-style for loop
 *
 * Syntax: for (init; condition; post) body
 *
 * TAC Pattern:
 *   <init.code>             // Initialization (once)
 *   M1:                     // Condition check
 *   <condition.code>
 *   ifFalse condition.result goto EXIT
 *   M2:
 *   <body.code>
 *   M_post:                 // Post-iteration
 *   <post.code>
 *   goto M1
 *   EXIT:
 *
 * generate_tac():
 * - Init can be ExpressionStatement or DeclarationStatement (int i = 0)
 * - M1 marks condition check (for repeat)
 * - M_post marks post-expression (for continue - important!)
 * - Condition's truelist backpatched to body start (M2)
 * - Body's nextlist backpatched to M_post
 * - Condition's falselist becomes statement's nextlist (exit)
 * - Body's breaklist backpatched to EXIT
 * - Body's continuelist backpatched to M_post (NOT M1!)
 */
class ForStatement : public Statement
{
public:
    Statement *init;       // Initialization (can be ExpressionStatement or DeclarationStatement)
    Expression *condition; // Loop condition (nullptr means infinite loop)
    Expression *post;      // Post-iteration expression
    Statement *body;       // Loop body

    ForStatement(Statement *init_stmt, Expression *cond, Expression *post_expr, Statement *body_stmt);
    virtual ~ForStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Break Statement - Exit enclosing loop
 *
 * Syntax: break;
 *
 * generate_tac():
 * - Generates a goto instruction (target TBD)
 * - Adds it to breaklist
 * - Enclosing loop will backpatch to loop exit
 */
class BreakStatement : public Statement
{
public:
    BreakStatement();
    virtual ~BreakStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Continue Statement - Jump to next iteration of enclosing loop
 *
 * Syntax: continue;
 *
 * generate_tac():
 * - Generates a goto instruction (target TBD)
 * - Adds it to continuelist
 * - Enclosing loop will backpatch to:
 *   * while/do-while/until: loop beginning (condition check)
 *   * for: post-expression (then condition check)
 */
class ContinueStatement : public Statement
{
public:
    ContinueStatement();
    virtual ~ContinueStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Return Statement - Return from function
 * 
 * Syntax: return expression; or return;
 * Semantics: Type of expression must match function return type
 * 
 * generate_tac():
 * - If expression present, generate its code
 * - Emit TAC_RETURN instruction with expression result (or empty for void) 
 */
class ReturnStatement : public Statement
{
    public:
    Expression *expr;

    ReturnStatement(Expression *e = nullptr);
    virtual ~ReturnStatement();

    std::string to_string() const override;
    void generate_tac() override;

};

/**
 * Declaration Statement - Wraps a Declaration for use as a Statement
 *
 * Used primarily in for-loop initialization:
 * for (int i = 0; ...) where "int i = 0" needs to be a statement
 *
 * generate_tac():
 * - Delegates to wrapped declaration's generate_tac()
 * - No control flow, so nextlist stays empty
 */
class DeclarationStatement : public Statement
{
public:
    Declaration *declaration; // Wrapped declaration

    DeclarationStatement(Declaration *decl);
    virtual ~DeclarationStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Expression Statement - Expression followed by semicolon
 *
 * Syntax: expr;
 * Examples: x = 5; printf("hi"); func();
 *
 * Can also represent empty statement (just semicolon) when expr is nullptr
 *
 * generate_tac():
 * - Generates code for expression
 * - No control flow, so nextlist stays empty
 */
class ExpressionStatement : public Statement
{
public:
    Expression *expr; // Expression (nullptr for empty statement)

    ExpressionStatement(Expression *e = nullptr);
    virtual ~ExpressionStatement();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Compound Statement - Block of statements in braces
 *
 * Syntax: { stmt1; stmt2; ...; stmtN; }
 *
 * generate_tac():
 * - Processes statements in sequence
 * - Backpatches each statement's nextlist to next statement's start (marker M)
 * - Propagates break/continue lists up (they bubble through to enclosing loop)
 * - Final statement's nextlist becomes this statement's nextlist
 */
class CompoundStatement : public Statement
{
public:
    std::vector<Statement *> statements; // List of statements in block

    CompoundStatement();
    virtual ~CompoundStatement();

    void add_statement(Statement *stmt);
    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Case Label - Marks a case within a switch statement
 *
 * Syntax: case CONSTANT: statement
 *
 * This is a wrapper that:
 * - Validates the case value is a constant integer/char
 * - Records the case value and the position in TAC
 * - Executes the statement following the label
 *
 * Note: Unlike a statement, this doesn't generate control flow itself.
 * It's collected by the enclosing SwitchStatement for dispatch generation.
 */
class CaseLabel : public Statement
{
public:
    Expression *case_value; // Must be constant integer/char expression
    Statement *statement;   // Statement to execute for this case

    CaseLabel(Expression *value, Statement *stmt);
    virtual ~CaseLabel();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Default Label - Marks the default case within a switch statement
 *
 * Syntax: default: statement
 *
 * This is a wrapper that:
 * - Records the position in TAC for default execution
 * - Executes the statement following the label
 *
 * If no case matches in switch, control transfers here.
 */
class DefaultLabel : public Statement
{
public:
    Statement *statement; // Statement to execute for default case

    DefaultLabel(Statement *stmt);
    virtual ~DefaultLabel();

    std::string to_string() const override;
    void generate_tac() override;
};

/**
 * Switch Statement - Multi-way conditional branch
 *
 * Syntax: switch (expression) { case val1: ... case val2: ... default: ... }
 *
 * TAC Pattern:
 *   <switch_expr.code>              // Evaluate switch expression once
 *   _result = switch_expr
 *   _t0 = _result == case1_value    // Compare with each case
 *   if _t0 goto L_case1
 *   _t1 = _result == case2_value
 *   if _t1 goto L_case2
 *   goto L_default                   // If no match, goto default (or exit if no default)
 *   L_case1:
 *     <case1_body>                   // Falls through to case2 unless break
 *   L_case2:
 *     <case2_body>
 *     goto EXIT                      // break statement
 *   L_default:
 *     <default_body>
 *   EXIT:
 *
 * generate_tac():
 * - Evaluates switch expression once
 * - Scans body to collect all CaseLabel and DefaultLabel positions
 * - Generates comparison dispatch code at the beginning
 * - Generates body code with fall-through behavior
 * - Backpatches all break statements to EXIT
 *
 * Features:
 * - Fall-through: execution continues to next case unless break
 * - Type checking: switch expr and case values must be integer types
 * - Constant checking: case values must be compile-time constants
 * - Default is optional
 */
class SwitchStatement : public Statement
{
public:
    Expression *switch_expr; // The switch(expr) - must be integer type
    Statement *body;         // Body statement (usually CompoundStatement)

    // Collected during body generation:
    std::vector<std::pair<Expression *, int>> case_labels; // {case_value, label_position}
    int default_label;                                     // Position of default label (-1 if no default)

    SwitchStatement(Expression *expr, Statement *body_stmt);
    virtual ~SwitchStatement();

    std::string to_string() const override;
    void generate_tac() override;

    // Helper to collect case/default labels from body
    void collect_labels(Statement *stmt);
};

// ============================================================================
// Helper Functions - Statement Node Creation
// ============================================================================

IfStatement *create_if_statement(Expression *cond, Statement *then_stmt, Statement *else_stmt = nullptr);
WhileStatement *create_while_statement(Expression *cond, Statement *body);
DoWhileStatement *create_dowhile_statement(Statement *body, Expression *cond);
UntilStatement *create_until_statement(Expression *cond, Statement *body);
ForStatement *create_for_statement(Statement *init, Expression *cond, Expression *post, Statement *body);
BreakStatement *create_break_statement();
ContinueStatement *create_continue_statement();
DeclarationStatement *create_declaration_statement(Declaration *decl);
ExpressionStatement *create_expression_statement(Expression *expr = nullptr);
CompoundStatement *create_compound_statement();
CaseLabel *create_case_label(Expression *value, Statement *stmt);
DefaultLabel *create_default_label(Statement *stmt);
SwitchStatement *create_switch_statement(Expression *expr, Statement *body);
ReturnStatement *create_return_statement(Expression *expr = nullptr);

#endif // STATEMENT_H
