#include "statement.h"
#include "declaration.h"
#include "symbol_table.h"
#include <algorithm>
#include <iostream>
#include <cstdio>
#include "diagnostics.h"

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
#define SEM_WARN(line, ...) report_semantic_warning(effective_line(line), __VA_ARGS__)

// ============================================================================
// Static variable to track loop nesting depth for break/continue validation
// ============================================================================
static int loop_depth = 0;

// ============================================================================
// Static variable to track current function return type for return validation
// ============================================================================
// Current function return type (value) is defined in ansic.y
extern Type current_function_return_type;

// ============================================================================
// Active compound scope stack for destructor tracking during codegen
// ============================================================================
static std::vector<CompoundStatement *> active_compound_stack;

// Forward decl for helper used in return/scope-exit to emit destructor calls
static void emit_destructor_for_symbol(Symbol *sym, std::vector<TACInstruction *> &out);

// Registration API used by declaration codegen when a class object is constructed
void register_constructed_local(Symbol *sym)
{
    if (!active_compound_stack.empty() && sym)
    {
        active_compound_stack.back()->constructed_locals.push_back(sym);
        if (debug)
        {
            printf("[RAII] Registered constructed local '%s' in scope %d\n", sym->name.c_str(), sym->scope);
        }
    }
}

// ============================================================================
// Statement Implementation
// ============================================================================
// This file contains implementations for all statement node types:
//   1. Statement Base Class
//   2. If Statement (if-else)
//   3. While Statement (test-first loop)
//   4. Do-While Statement (test-last loop)
//   5. Until Statement (loop until true)
//   6. For Statement (C-style for loop)
//   7. Break Statement
//   8. Continue Statement
//   9. Declaration Statement
//  10. Expression Statement
//  11. Compound Statement
//
// All loops use backpatching for control flow
// Break/continue lists propagate up to enclosing loops
// ============================================================================

// ============================================================================
// Statement Base Class
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

    // TYPE CHECK: Ensure condition is bool-compatible
    if (condition->type && !is_bool_compatible(*condition->type))
    {
        SEM_ERROR(condition->line_no,
                  "if condition must be bool-compatible (numeric, pointer, enum, or bool), got %s",
                  condition->type->to_string().c_str());
        semantic_error_count++;
        return;
    }

    // STEP 1.5: Handle non-boolean expressions in boolean context
    // If condition doesn't have truelist/falselist (e.g., simple variable or arithmetic),
    // generate implicit comparison: if (condition != 0)
    if (condition->truelist.empty() && condition->falselist.empty())
    {
        // Generate: if condition goto ___ (truelist)
        int true_jump = tacGen.emit(TAC_IF_GOTO, TACOperand(), *condition->result);
        code.push_back(tacGen.getCode().back());

        // Generate: goto ___ (falselist)
        int false_jump = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
        code.push_back(tacGen.getCode().back());

        condition->truelist = makelist(true_jump);
        condition->falselist = makelist(false_jump);
    }

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

        // Propagate break and continue lists from both branches
        breaklist = merge(then_stmt->breaklist, else_stmt->breaklist);
        continuelist = merge(then_stmt->continuelist, else_stmt->continuelist);
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

        // Propagate break and continue lists from then branch
        breaklist = then_stmt->breaklist;
        continuelist = then_stmt->continuelist;
    }

    if (debug)
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

    // TYPE CHECK: Ensure condition is bool-compatible
    if (condition->type && !is_bool_compatible(*condition->type))
    {
        SEM_ERROR(condition->line_no,
                  "while condition must be bool-compatible (numeric, pointer, enum, or bool), got %s",
                  condition->type->to_string().c_str());
        semantic_error_count++;
        return;
    }

    // M2: start of body
    int M2 = tacGen.nextinstr();

    // Backpatch B.truelist to M2 (enter loop body when true)
    backpatch(condition->truelist, M2);

    // Increment loop depth before generating body (for break/continue validation)
    loop_depth++;

    // Generate code for body
    body->generate_tac();
    code.insert(code.end(), body->code.begin(), body->code.end());

    // Decrement loop depth after body
    loop_depth--;

    // Backpatch S.nextlist (end of body) to M1 (repeat loop)
    backpatch(body->nextlist, M1);

    // Generate goto back to beginning
    int goto_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
    tacGen.getCode()[goto_instr]->target_line = M1;

    // Handle break and continue
    // - break statements should jump to loop exit (after this while statement)
    // - continue statements should jump to loop beginning (M1 - condition check)
    int loop_exit = tacGen.nextinstr();
    backpatch(body->breaklist, loop_exit);
    backpatch(body->continuelist, M1);

    // S.nextlist = B.falselist (exit loop when condition is false)
    nextlist = condition->falselist;

    if (debug)
        printf("[AST] WhileStatement: Generated TAC with backpatching\n");
}

// ============================================================================
// DoWhileStatement - do { body } while (condition);
// ============================================================================
// TAC Pattern:
//   M1:
//   <body.code>
//   M2:
//   <condition.code>
//   if condition.result goto M1
//   (exit)
// ============================================================================

DoWhileStatement::DoWhileStatement(Statement *body_stmt, Expression *cond)
    : body(body_stmt), condition(cond)
{
}

DoWhileStatement::~DoWhileStatement()
{
    delete body;
    delete condition;
}

string DoWhileStatement::to_string() const
{
    return "do " + body->to_string() + " while (" + condition->to_string() + ");";
}

void DoWhileStatement::generate_tac()
{
    // ========================================================================
    // Backpatching-based do-while loop translation
    // ========================================================================
    // Grammar: do M1 S M2 while (B)
    //
    // M1.instr = beginning of body
    // backpatch S.nextlist to M2.instr
    // backpatch B.truelist to M1.instr (repeat)
    // S.nextlist = B.falselist (exit)
    // ========================================================================

    // M1: beginning of body
    int M1 = tacGen.nextinstr();

    // Increment loop depth before generating body
    loop_depth++;

    // Generate code for body
    body->generate_tac();
    code = body->code;

    // Decrement loop depth after body
    loop_depth--;

    // M2: after body, before condition
    int M2 = tacGen.nextinstr();

    // Backpatch S.nextlist (end of body) to M2
    backpatch(body->nextlist, M2);

    // Generate code for condition
    condition->generate_tac();
    code.insert(code.end(), condition->code.begin(), condition->code.end());

    // TYPE CHECK: Ensure condition is bool-compatible
    if (condition->type && !is_bool_compatible(*condition->type))
    {
        SEM_ERROR(condition->line_no,
                  "do-while condition must be bool-compatible (numeric, pointer, enum, or bool), got %s",
                  condition->type->to_string().c_str());
        semantic_error_count++;
        return;
    }

    // Backpatch B.truelist to M1 (loop back)
    backpatch(condition->truelist, M1);

    // Handle break and continue
    // - break statements should jump to loop exit
    // - continue statements should jump to condition check (M2)
    int loop_exit = tacGen.nextinstr();
    backpatch(body->breaklist, loop_exit);
    backpatch(body->continuelist, M2);

    // S.nextlist = B.falselist (exit when condition is false)
    nextlist = condition->falselist;

    if (debug)
        printf("[AST] DoWhileStatement: Generated TAC with backpatching\n");
}

// ============================================================================
// UntilStatement - until (condition) { body }
// ============================================================================
// TAC Pattern: Loop UNTIL condition becomes true (opposite of while)
//   M1:
//   <condition.code>
//   if condition.result goto EXIT (true = exit)
//   <body.code>
//   goto M1
//   EXIT:
// ============================================================================

UntilStatement::UntilStatement(Expression *cond, Statement *body_stmt)
    : condition(cond), body(body_stmt)
{
}

UntilStatement::~UntilStatement()
{
    delete condition;
    delete body;
}

string UntilStatement::to_string() const
{
    return "until (" + condition->to_string() + ") " + body->to_string();
}

void UntilStatement::generate_tac()
{
    // ========================================================================
    // Backpatching-based until loop translation
    // ========================================================================
    // until(B) S  means: loop while NOT B
    //
    // M1.instr = beginning of loop
    // backpatch B.truelist to EXIT (condition true = exit!)
    // backpatch B.falselist to M2 (condition false = enter body)
    // backpatch S.nextlist to M1 (repeat)
    // S.nextlist = B.truelist (exit when condition is true)
    // ========================================================================

    // M1: beginning of loop
    int M1 = tacGen.nextinstr();

    // Generate code for condition
    condition->generate_tac();
    code = condition->code;

    // M2: start of body
    int M2 = tacGen.nextinstr();

    // For until: truelist goes to EXIT, falselist enters body
    // Backpatch B.falselist to M2 (enter loop body when false)
    backpatch(condition->falselist, M2);

    // Increment loop depth before generating body
    loop_depth++;

    // Generate code for body
    body->generate_tac();
    code.insert(code.end(), body->code.begin(), body->code.end());

    // Decrement loop depth after body
    loop_depth--;

    // Backpatch S.nextlist (end of body) to M1 (repeat loop)
    backpatch(body->nextlist, M1);

    // Generate goto back to beginning
    int goto_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
    tacGen.getCode()[goto_instr]->target_line = M1;

    // Handle break and continue
    // - break statements should jump to loop exit
    // - continue statements should jump to loop beginning (M1 - condition check)
    int loop_exit = tacGen.nextinstr();
    backpatch(body->breaklist, loop_exit);
    backpatch(body->continuelist, M1);

    // S.nextlist = B.truelist (exit loop when condition is TRUE)
    nextlist = condition->truelist;

    if (debug)
        printf("[AST] UntilStatement: Generated TAC with backpatching\n");
}

// ============================================================================
// ForStatement - for (init; cond; post) body
// ============================================================================
// TAC Pattern:
//   <init.code>
//   M1:
//   <condition.code>
//   if-false condition.result goto EXIT
//   <body.code>
//   M2:
//   <post.code>
//   goto M1
//   EXIT:
// ============================================================================

ForStatement::ForStatement(Statement *init_stmt, Expression *cond, Expression *post_expr, Statement *body_stmt)
    : init(init_stmt), condition(cond), post(post_expr), body(body_stmt)
{
}

ForStatement::~ForStatement()
{
    if (init)
        delete init;
    if (condition)
        delete condition;
    if (post)
        delete post;
    delete body;
}

string ForStatement::to_string() const
{
    string result = "for (";
    if (init)
        result += init->to_string();
    result += " ";
    if (condition)
        result += condition->to_string();
    result += "; ";
    if (post)
        result += post->to_string();
    result += ") " + body->to_string();
    return result;
}

void ForStatement::generate_tac()
{
    // ========================================================================
    // Backpatching-based for loop translation
    // ========================================================================
    // for (init; B; post) S
    //
    // Generate init
    // M1 = beginning of condition check
    // backpatch B.truelist to M2 (body start)
    // backpatch S.nextlist to M_post (post expression)
    // Generate post, goto M1
    // S.nextlist = B.falselist (exit)
    // ========================================================================
    // NOTE: Scope management is handled in the parser (ansic.y)

    // Generate initialization
    if (init)
    {
        init->generate_tac();
        code = init->code;
    }

    // M1: beginning of loop (condition check)
    int M1 = tacGen.nextinstr();

    // Generate condition (if present, otherwise infinite loop)
    InstructionList exit_list;
    if (condition)
    {
        condition->generate_tac();
        code.insert(code.end(), condition->code.begin(), condition->code.end());

        // TYPE CHECK: Ensure condition is bool-compatible
        if (condition->type && !is_bool_compatible(*condition->type))
        {
            SEM_ERROR(condition->line_no,
                      "for loop condition must be bool-compatible (numeric, pointer, enum, or bool), got %s",
                      condition->type->to_string().c_str());
            semantic_error_count++;
            return;
        }

        // M2: start of body
        int M2 = tacGen.nextinstr();

        // Backpatch B.truelist to M2 (enter body when true)
        backpatch(condition->truelist, M2);

        // Exit list is B.falselist
        exit_list = condition->falselist;
    }
    // If no condition, it's an infinite loop (no exit condition)

    // Increment loop depth before generating body
    loop_depth++;

    // Generate body
    body->generate_tac();
    code.insert(code.end(), body->code.begin(), body->code.end());

    // Decrement loop depth after body
    loop_depth--;

    // M_post: after body, before post expression
    int M_post = tacGen.nextinstr();

    // Backpatch body.nextlist to M_post
    backpatch(body->nextlist, M_post);

    // Generate post expression
    if (post)
    {
        post->generate_tac();
        code.insert(code.end(), post->code.begin(), post->code.end());
    }

    // Generate goto back to M1 (condition check)
    int goto_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
    tacGen.getCode()[goto_instr]->target_line = M1;

    // Handle break and continue
    // - break statements should jump to loop exit
    // - continue statements should jump to M_post (post expression), NOT to M1
    int loop_exit = tacGen.nextinstr();
    backpatch(body->breaklist, loop_exit);
    backpatch(body->continuelist, M_post);

    // S.nextlist = exit_list
    nextlist = exit_list;

    if (debug)
        printf("[AST] ForStatement: Generated TAC with backpatching\n");
}

// ============================================================================
// BreakStatement - break;
// ============================================================================

BreakStatement::BreakStatement()
{
}

BreakStatement::~BreakStatement()
{
}

string BreakStatement::to_string() const
{
    return "break;";
}

void BreakStatement::generate_tac()
{
    // Validate that break is inside a loop or switch
    if (loop_depth == 0)
    {
        SEM_ERROR(line_no, "'break' statement not within loop or switch");
        semantic_error_count++;
        return;
    }

    // Generate a goto that will be backpatched to loop exit
    int goto_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());

    // Add to breaklist (will be backpatched by enclosing loop to exit)
    breaklist = makelist(goto_instr);

    if (debug)
        printf("[AST] BreakStatement: Generated goto for break\n");
}

// ============================================================================
// ContinueStatement - continue;
// ============================================================================

ContinueStatement::ContinueStatement()
{
}

ContinueStatement::~ContinueStatement()
{
}

string ContinueStatement::to_string() const
{
    return "continue;";
}

void ContinueStatement::generate_tac()
{
    // Validate that continue is inside a loop
    if (loop_depth == 0)
    {
        SEM_ERROR(line_no, "'continue' statement not within loop");
        semantic_error_count++;
        return;
    }

    // Generate a goto that will be backpatched to loop beginning/post
    int goto_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());

    // Add to continuelist (will be backpatched by enclosing loop to beginning/post)
    continuelist = makelist(goto_instr);

    if (debug)
        printf("[AST] ContinueStatement: Generated goto for continue\n");
}

// ============================================================================
// DeclarationStatement - wraps Declaration for use as Statement
// ============================================================================

DeclarationStatement::DeclarationStatement(Declaration *decl)
    : declaration(decl)
{
}

DeclarationStatement::~DeclarationStatement()
{
    if (declaration)
        delete declaration;
}

string DeclarationStatement::to_string() const
{
    if (declaration)
        return declaration->to_string();
    return "";
}

void DeclarationStatement::generate_tac()
{
    // First, generate TAC for any pending declarators from comma-separated lists
    // (these were deferred during parsing)
    extern std::vector<Declaration *> pending_declarator_tac;
    for (Declaration *decl : pending_declarator_tac)
    {
        if (decl)
        {
            decl->generate_tac();
            // Append code from pending declarator
            code.insert(code.end(), decl->code.begin(), decl->code.end());
        }
    }
    pending_declarator_tac.clear(); // Clear the pending list

    // Now generate TAC for the main declaration (the last one in the list)
    if (declaration)
    {
        declaration->generate_tac();
        code.insert(code.end(), declaration->code.begin(), declaration->code.end());
    }
    // No control flow, so nextlist stays empty
    if (debug)
        printf("[AST] DeclarationStatement: Generated TAC for wrapped declaration\n");
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
    // First, generate TAC for any pending comma expressions
    // (left-hand sides of comma operators that were deferred during parsing)
    extern std::vector<Expression *> pending_comma_expr_tac;
    for (Expression *e : pending_comma_expr_tac)
    {
        if (e)
        {
            e->generate_tac();
            // Append code from pending expression
            code.insert(code.end(), e->code.begin(), e->code.end());
        }
    }
    pending_comma_expr_tac.clear(); // Clear the pending list

    // Now generate TAC for the main expression (the rightmost in comma chain)
    if (expr)
    {
        if (debug)
            printf("[ExpressionStatement] Generating TAC for expression\n");
        expr->generate_tac();
        code.insert(code.end(), expr->code.begin(), expr->code.end());
    }
    else
    {
        if (debug)
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
    if (debug)
        printf("[CompoundStatement] Generating TAC for %zu statements\n", statements.size());

    // Push this compound on active stack for destructor tracking
    active_compound_stack.push_back(this);

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

        // Propagate break and continue lists (they bubble up to enclosing loop)
        breaklist = merge(breaklist, stmt->breaklist);
        continuelist = merge(continuelist, stmt->continuelist);
    }

    // On scope exit: emit destructors for constructed locals in reverse order
    if (!constructed_locals.empty())
    {
        if (debug)
            printf("[RAII] Emitting %zu destructor(s) at scope exit\n", constructed_locals.size());
        for (auto it = constructed_locals.rbegin(); it != constructed_locals.rend(); ++it)
        {
            emit_destructor_for_symbol(*it, code);
        }
    }

    // Pop this scope
    active_compound_stack.pop_back();

    // The compound statement's nextlist is the last statement's nextlist
    nextlist = current_nextlist;
}

// ============================================================================
// CaseLabel - case CONSTANT: statement
// ============================================================================

CaseLabel::CaseLabel(Expression *value, Statement *stmt)
    : case_value(value), statement(stmt)
{
}

CaseLabel::~CaseLabel()
{
    delete case_value;
    delete statement;
}

string CaseLabel::to_string() const
{
    return "case " + case_value->to_string() + ": " + statement->to_string();
}

void CaseLabel::generate_tac()
{
    // Generate TAC for case value (should be constant, evaluated at compile time)
    case_value->generate_tac();

    // Type checking: case value must be a constant integer or character
    if (case_value->type && !case_value->type->is_integer())
    {
        SEM_ERROR(line_no, "Case label must be an integer or character constant");
        semantic_error_count++;
    }

    // Generate code for the statement following the case label
    statement->generate_tac();
    code = statement->code;

    // Propagate control flow lists
    nextlist = statement->nextlist;
    breaklist = statement->breaklist;
    continuelist = statement->continuelist;

    if (debug)
        printf("[AST] CaseLabel: Generated TAC for case label\n");
}

// ============================================================================
// DefaultLabel - default: statement
// ============================================================================

DefaultLabel::DefaultLabel(Statement *stmt)
    : statement(stmt)
{
}

DefaultLabel::~DefaultLabel()
{
    delete statement;
}

string DefaultLabel::to_string() const
{
    return "default: " + statement->to_string();
}

void DefaultLabel::generate_tac()
{
    // Generate code for the statement following the default label
    statement->generate_tac();
    code = statement->code;

    // Propagate control flow lists
    nextlist = statement->nextlist;
    breaklist = statement->breaklist;
    continuelist = statement->continuelist;

    if (debug)
        printf("[AST] DefaultLabel: Generated TAC for default label\n");
}

// ============================================================================
// SwitchStatement - switch (expr) { case ...: ... default: ... }
// ============================================================================

SwitchStatement::SwitchStatement(Expression *expr, Statement *body_stmt)
    : switch_expr(expr), body(body_stmt), default_label(-2), use_jump_table(false)
{
    // default_label values:
    // -2 = no default found yet (initial state)
    // -1 = default found but position not set yet
    // >=0 = default found and position set
}

SwitchStatement::~SwitchStatement()
{
    delete switch_expr;
    delete body;
}

string SwitchStatement::to_string() const
{
    return "switch (" + switch_expr->to_string() + ") " + body->to_string();
}

void SwitchStatement::collect_labels(Statement *stmt)
{
    // Recursively collect case and default labels from the body

    if (CaseLabel *case_label = dynamic_cast<CaseLabel *>(stmt))
    {
        // Found a case label - extract constant value once and cache it
        int case_value = extract_constant_value(case_label->case_value);

        // Check if this case value already exists (using cached values)
        for (const auto &existing_case : case_labels)
        {
            if (existing_case.constant_value == case_value)
            {
                SEM_ERROR(case_label->line_no,
                          "Duplicate case value %d in switch statement", case_value);
                semantic_error_count++;
                return; // Don't add duplicate case
            }
        }

        // Add new case label with cached constant value
        CaseLabelInfo info;
        info.expr = case_label->case_value;
        info.constant_value = case_value;
        info.label_position = -1; // Will be updated during generate_tac
        case_labels.push_back(info);
    }
    else if (DefaultLabel *default_label_stmt = dynamic_cast<DefaultLabel *>(stmt))
    {
        // Found default label - check if we already have one
        if (default_label != -2) // -2 means no default found yet
        {
            SEM_ERROR(default_label_stmt->line_no,
                      "Multiple default labels in switch statement");
            semantic_error_count++;
            return; // Don't add duplicate default
        }

        default_label = -1; // Mark as found, position will be updated during generate_tac
    }
    else if (CompoundStatement *compound = dynamic_cast<CompoundStatement *>(stmt))
    {
        // Recursively scan compound statement
        for (Statement *s : compound->statements)
        {
            collect_labels(s);
        }
    }
}

// ============================================================================
// SwitchStatement Helper Functions - Jump Table Optimization
// ============================================================================

int SwitchStatement::extract_constant_value(Expression *expr)
{
    // Extract constant integer value from case expression
    // Supports: literals, character constants, and simple constant expressions (e.g., -100, 3+5)

    if (!expr->result)
    {
        // Expression hasn't been evaluated yet
        expr->generate_tac();
    }

    // Try to get value from result if it's a constant
    if (expr->result && expr->result->type == TACOperand::OPERAND_CONSTANT)
    {
        std::string value_str = expr->result->name;

        // Handle character literals: 'A', '\n', etc.
        if (value_str.length() >= 3 && value_str[0] == '\'' && value_str[value_str.length() - 1] == '\'')
        {
            // Extract character from 'X' format
            if (value_str.length() == 3)
            {
                // Simple char: 'A'
                return (int)value_str[1];
            }
            else if (value_str.length() == 4 && value_str[1] == '\\')
            {
                // Escape sequence: '\n', '\t', etc.
                char escape_char = value_str[2];
                switch (escape_char)
                {
                case 'n':
                    return (int)'\n';
                case 't':
                    return (int)'\t';
                case 'r':
                    return (int)'\r';
                case 'b':
                    return (int)'\b';
                case 'f':
                    return (int)'\f';
                case 'v':
                    return (int)'\v';
                case '0':
                    return (int)'\0';
                case '\\':
                    return (int)'\\';
                case '\'':
                    return (int)'\'';
                case '\"':
                    return (int)'\"';
                default:
                    return (int)escape_char;
                }
            }
        }

        // Handle integer literals (positive and negative)
        try
        {
            return std::stoi(value_str);
        }
        catch (...)
        {
            SEM_ERROR(line_no, "Case value must be a constant integer or character");
            semantic_error_count++;
            return 0;
        }
    }

    // Handle simple constant expressions: -N, +N
    // Check if this is a UnaryExpression with constant operand
    UnaryExpression *unary_expr = dynamic_cast<UnaryExpression *>(expr);
    if (unary_expr && unary_expr->expr)
    {
        // Try to get the operand's constant value
        int operand_value = extract_constant_value(unary_expr->expr);

        // Apply unary operator
        if (unary_expr->op == TAC_UMINUS)
        {
            return -operand_value;
        }
        else if (unary_expr->op == TAC_UPLUS)
        {
            return operand_value;
        }
        else if (unary_expr->op == TAC_BITWISE_NOT)
        {
            return ~operand_value;
        }
        // For other unary ops, fall through to error
    }

    // Handle simple constant expressions: N + M, N - M, etc.
    // This enables expressions like (1 + 2) in case labels
    BinaryExpression *binary_expr = dynamic_cast<BinaryExpression *>(expr);
    if (binary_expr && binary_expr->left && binary_expr->right)
    {
        // Try to get both operands' constant values
        int left_value = extract_constant_value(binary_expr->left);
        int right_value = extract_constant_value(binary_expr->right);

        // Apply binary operator
        if (binary_expr->op == TAC_ADD)
            return left_value + right_value;
        else if (binary_expr->op == TAC_SUB)
            return left_value - right_value;
        else if (binary_expr->op == TAC_MUL)
            return left_value * right_value;
        else if (binary_expr->op == TAC_DIV)
            return left_value / right_value;
        else if (binary_expr->op == TAC_MOD)
            return left_value % right_value;
        else if (binary_expr->op == TAC_BITWISE_AND)
            return left_value & right_value;
        else if (binary_expr->op == TAC_BITWISE_OR)
            return left_value | right_value;
        else if (binary_expr->op == TAC_BITWISE_XOR)
            return left_value ^ right_value;
        else if (binary_expr->op == TAC_LEFT_SHIFT)
            return left_value << right_value;
        else if (binary_expr->op == TAC_RIGHT_SHIFT)
            return left_value >> right_value;
        // For other binary ops, fall through to error
    }

    SEM_ERROR(line_no, "Case value must be a constant expression");
    semantic_error_count++;
    return 0;
}

bool SwitchStatement::should_use_jump_table(const std::vector<int> &case_values)
{
    // Decide whether to use jump table optimization based on:
    // 1. Number of cases (need at least 4-5 cases)
    // 2. Density of case values (at least 40% filled)
    // 3. Range size (not too large to avoid memory waste)

    if (case_values.empty())
        return false;

    size_t num_cases = case_values.size();

    // Need at least 4 cases to justify jump table overhead
    if (num_cases < 4)
        return false;

    // Find min and max case values
    int min_val = *std::min_element(case_values.begin(), case_values.end());
    int max_val = *std::max_element(case_values.begin(), case_values.end());
    int range = max_val - min_val + 1;

    // Check if range is reasonable (not too large)
    if (range > 256)
    {
        if (debug)
            printf("[Optimization] Switch: Range too large (%d), using sequential dispatch\n", range);
        return false;
    }

    // Check density: at least 40% of the range should be filled with cases
    float density = (float)num_cases / (float)range;
    if (density < 0.4f)
    {
        if (debug)
            printf("[Optimization] Switch: Density too low (%.2f), using sequential dispatch\n", density);
        return false;
    }

    // Good candidate for jump table!
    if (debug)
        printf("[Optimization] Switch: Using jump table (cases=%zu, range=%d, density=%.2f)\n",
               num_cases, range, density);

    return true;
}

void SwitchStatement::generate_jump_table_dispatch(TACOperand switch_result,
                                                   const std::vector<int> &case_values)
{
    // Generate jump table dispatch code:
    // 1. Bounds check (if x < min or x > max, goto default)
    // 2. Normalize index (index = x - min)
    // 3. Emit jump table instruction

    int min_val = *std::min_element(case_values.begin(), case_values.end());
    int max_val = *std::max_element(case_values.begin(), case_values.end());
    int range = max_val - min_val + 1;

    // Initialize jump table structure
    jump_table_info.min_value = min_val;
    jump_table_info.max_value = max_val;
    jump_table_info.table.resize(range, -1); // -1 means no case at this position
    jump_table_info.default_label = -1;
    jump_table_info.case_positions.clear();

    // Map case values to their indices
    for (size_t i = 0; i < case_values.size(); i++)
    {
        int index = case_values[i] - min_val;
        jump_table_info.table[index] = i; // Maps to case index
    }

    // STEP 1: Bounds check - if (x < min_val) goto default
    TACOperand min_op(TACOperand::OPERAND_CONSTANT, std::to_string(min_val));
    Type *bool_type = new Type(TYPE_BOOL);
    TACOperand bounds_temp1 = tacGen.newTemp(bool_type);
    tacGen.emit(TAC_LT, bounds_temp1, switch_result, min_op);
    int bounds_check1 = tacGen.emit(TAC_IF_GOTO, TACOperand(), bounds_temp1);
    code.push_back(tacGen.getCode().back());
    dispatch_instructions.push_back(bounds_check1);

    // STEP 2: Bounds check - if (x > max_val) goto default
    TACOperand max_op(TACOperand::OPERAND_CONSTANT, std::to_string(max_val));
    TACOperand bounds_temp2 = tacGen.newTemp(bool_type);
    tacGen.emit(TAC_GT, bounds_temp2, switch_result, max_op);
    int bounds_check2 = tacGen.emit(TAC_IF_GOTO, TACOperand(), bounds_temp2);
    code.push_back(tacGen.getCode().back());
    dispatch_instructions.push_back(bounds_check2);

    // STEP 3: Normalize to 0-based index: index = x - min_val
    Type *int_type = new Type(TYPE_INT);
    TACOperand index_temp = tacGen.newTemp(int_type);
    tacGen.emit(TAC_SUB, index_temp, switch_result, min_op);
    code.push_back(tacGen.getCode().back());

    // STEP 4: Jump table instruction - goto jump_table[index]
    // We'll emit this as a special instruction and backpatch with actual labels
    // Note: For jump table, we use arg1 to hold the index (result is empty)
    int table_jump = tacGen.emit(TAC_JUMP_TABLE, TACOperand(), index_temp);
    code.push_back(tacGen.getCode().back());
    dispatch_instructions.push_back(table_jump);

    if (debug)
        printf("[AST] SwitchStatement: Generated jump table dispatch (range %d-%d)\n",
               min_val, max_val);
}

void SwitchStatement::generate_sequential_dispatch(TACOperand switch_result,
                                                   const std::vector<int> &case_values)
{
    // Generate sequential comparison dispatch (original implementation)
    // For each case: compare and conditionally jump

    dispatch_instructions.clear();

    for (size_t i = 0; i < case_values.size(); i++)
    {
        // Use the cached constant value directly instead of re-evaluating expression
        int case_value = case_labels[i].constant_value;
        TACOperand case_operand(TACOperand::OPERAND_CONSTANT, std::to_string(case_value));

        // Generate comparison: _t = switch_result == case_value
        Type *bool_type = new Type(TYPE_BOOL);
        TACOperand temp = tacGen.newTemp(bool_type);
        tacGen.emit(TAC_EQ, temp, switch_result, case_operand);
        code.push_back(tacGen.getCode().back());

        // Generate conditional jump: if _t goto case_label
        int jump_instr = tacGen.emit(TAC_IF_GOTO, TACOperand(), temp);
        code.push_back(tacGen.getCode().back());

        // Remember this instruction for backpatching
        dispatch_instructions.push_back(jump_instr);
    }

    // If no cases match, goto default (or EXIT if no default)
    int default_goto_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
    code.push_back(tacGen.getCode().back());
    dispatch_instructions.push_back(default_goto_instr);

    if (debug)
        printf("[AST] SwitchStatement: Generated sequential dispatch (%zu cases)\n",
               case_values.size());
}

void SwitchStatement::generate_tac()
{
    // ========================================================================
    // Switch Statement TAC Generation with Jump Table Optimization
    // ========================================================================
    //
    // Sequential Pattern (for sparse/few cases):
    //   <switch_expr.code>              // Evaluate switch expression once
    //   _t0 = switch_expr == case1_value    // Compare with each case
    //   if _t0 goto L_case1
    //   _t1 = switch_expr == case2_value
    //   if _t1 goto L_case2
    //   goto L_default (or EXIT if no default)
    //   L_case1:
    //     <case1_body>                  // Falls through to case2 unless break
    //   L_case2:
    //     <case2_body>
    //     goto EXIT                     // break statement
    //   L_default:
    //     <default_body>
    //   EXIT:
    //
    // Jump Table Pattern (for dense/many cases):
    //   <switch_expr.code>
    //   if x < min goto L_default       // Bounds check
    //   if x > max goto L_default
    //   index = x - min                 // Normalize
    //   goto jump_table[index]          // O(1) dispatch
    //   L_case1: ...
    //   L_case2: ...
    //   EXIT:
    // ========================================================================

    if (debug)
        printf("[AST] SwitchStatement: Generating TAC\n");

    // STEP 1: Type check - switch expression must be integer type
    switch_expr->generate_tac();

    if (!switch_expr->type || !switch_expr->type->is_integer())
    {
        SEM_ERROR(line_no, "Switch expression must be an integer type");
        semantic_error_count++;
        return;
    }

    // STEP 2: Collect case and default labels by scanning body structurally (without generating code)
    case_labels.clear();
    default_label = -2; // Reset to "no default found yet"
    collect_labels(body);

    if (debug)
        printf("[AST] SwitchStatement: Found %zu case labels\n", case_labels.size());

    // STEP 3: Start building our code - add switch expression evaluation
    code.insert(code.end(), switch_expr->code.begin(), switch_expr->code.end());

    // Store switch result for comparison
    TACOperand switch_result = *switch_expr->result;

    // STEP 4: Extract constant values from all cases and decide optimization strategy
    // Use cached constant values instead of recalculating
    std::vector<int> case_values;
    for (const auto &case_info : case_labels)
    {
        case_values.push_back(case_info.constant_value);
    }

    // Decide whether to use jump table or sequential dispatch
    use_jump_table = should_use_jump_table(case_values);

    // STEP 5: Generate dispatch code based on optimization decision
    dispatch_instructions.clear();

    if (use_jump_table)
    {
        // Use jump table for O(1) dispatch
        generate_jump_table_dispatch(switch_result, case_values);
    }
    else
    {
        // Use sequential comparison for sparse/few cases
        generate_sequential_dispatch(switch_result, case_values);
    }

    // STEP 6: Now generate the body code
    // Track where each case label starts
    std::vector<int> case_positions;
    int default_position = -1;

    // Accumulate all break statements from the body
    std::vector<int> accumulated_breaklist;

    // Increment loop_depth so break statements know they're in a switch
    loop_depth++;

    if (CompoundStatement *compound = dynamic_cast<CompoundStatement *>(body))
    {
        for (size_t i = 0; i < compound->statements.size(); i++)
        {
            Statement *stmt = compound->statements[i];

            // Mark position before generating this statement
            int stmt_start = tacGen.nextinstr();

            if (dynamic_cast<CaseLabel *>(stmt))
            {
                // This is a case label - record its position
                case_positions.push_back(stmt_start);
            }
            else if (dynamic_cast<DefaultLabel *>(stmt))
            {
                // This is the default label - record its position
                default_position = stmt_start;
            }

            // Generate code for this statement
            stmt->generate_tac();
            code.insert(code.end(), stmt->code.begin(), stmt->code.end());

            // Backpatch this statement's nextlist to the next statement
            // (or will be backpatched to exit later if this is the last statement)
            int next_target = tacGen.nextinstr();
            backpatch(stmt->nextlist, next_target);

            // Accumulate break statements from this statement
            accumulated_breaklist.insert(accumulated_breaklist.end(),
                                         stmt->breaklist.begin(), stmt->breaklist.end());
        }
    }
    else
    {
        // Body is not a compound statement - just generate it
        int body_start = tacGen.nextinstr();
        body->generate_tac();
        code.insert(code.end(), body->code.begin(), body->code.end());

        // If body itself is a case or default, record it
        if (dynamic_cast<CaseLabel *>(body))
        {
            case_positions.push_back(body_start);
        }
        else if (dynamic_cast<DefaultLabel *>(body))
        {
            default_position = body_start;
        }

        // Accumulate break statements from the body
        accumulated_breaklist = body->breaklist;
    }

    // Decrement loop_depth now that we're done with the switch body
    loop_depth--;

    // STEP 7: Backpatch dispatch instructions based on optimization strategy
    int exit_label = tacGen.nextinstr();

    if (use_jump_table)
    {
        // Jump table backpatching
        // dispatch_instructions[0] = bounds check 1 (goto default if x < min)
        // dispatch_instructions[1] = bounds check 2 (goto default if x > max)
        // dispatch_instructions[2] = jump table instruction

        // Backpatch bounds checks to default or exit
        int default_target = (default_position != -1) ? default_position : exit_label;

        if (dispatch_instructions.size() >= 3)
        {
            tacGen.getCode()[dispatch_instructions[0]]->target_line = default_target;
            tacGen.getCode()[dispatch_instructions[1]]->target_line = default_target;

            // For the jump table instruction, store the case positions
            // The jump table maps normalized indices to case positions

            // Store jump table metadata in the instruction's comment or as special data
            // For now, we'll generate a comment showing the table mapping
            std::string table_comment = "Jump table: ";
            for (size_t i = 0; i < jump_table_info.table.size(); i++)
            {
                int case_idx = jump_table_info.table[i];
                if (case_idx >= 0 && (size_t)case_idx < case_positions.size())
                {
                    table_comment += "[" + std::to_string(i) + "]=>" +
                                     std::to_string(case_positions[case_idx]) + " ";
                }
                else
                {
                    table_comment += "[" + std::to_string(i) + "]=>" +
                                     std::to_string(default_target) + " ";
                }
            }

            // Store the actual case positions in the jump table
            jump_table_info.case_positions = case_positions;
            jump_table_info.default_label = default_target;

            if (debug)
                printf("[AST] SwitchStatement: Backpatched jump table - %s\n",
                       table_comment.c_str());
        }
    }
    else
    {
        // Sequential dispatch backpatching
        // dispatch_instructions[0..n-1] = case comparisons
        // dispatch_instructions[n] = default goto

        // Backpatch case jumps to their actual positions
        size_t num_case_jumps = dispatch_instructions.size() - 1; // Last one is default goto
        for (size_t i = 0; i < num_case_jumps && i < case_positions.size(); i++)
        {
            tacGen.getCode()[dispatch_instructions[i]]->target_line = case_positions[i];
        }

        // Backpatch the default goto
        int default_goto_idx = dispatch_instructions.back();
        if (default_position != -1)
        {
            tacGen.getCode()[default_goto_idx]->target_line = default_position;
        }
        else
        {
            // No default - goto EXIT
            tacGen.getCode()[default_goto_idx]->target_line = exit_label;
        }
    }

    // STEP 8: Backpatch all break statements to EXIT
    backpatch(accumulated_breaklist, exit_label);

    // Switch statement's nextlist is empty
    nextlist.clear();

    if (debug)
        printf("[AST] SwitchStatement: Generated TAC with %zu cases, EXIT at line %d\n",
               case_labels.size(), exit_label);
}

// ============================================================================
// Helper Functions - Statement Creation
// ============================================================================

IfStatement *create_if_statement(Expression *cond, Statement *then_stmt, Statement *else_stmt, int line, int col)
{
    IfStatement *stmt = new IfStatement(cond, then_stmt, else_stmt);
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

WhileStatement *create_while_statement(Expression *cond, Statement *body, int line, int col)
{
    WhileStatement *stmt = new WhileStatement(cond, body);
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

DoWhileStatement *create_dowhile_statement(Statement *body, Expression *cond, int line, int col)
{
    DoWhileStatement *stmt = new DoWhileStatement(body, cond);
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

UntilStatement *create_until_statement(Expression *cond, Statement *body, int line, int col)
{
    UntilStatement *stmt = new UntilStatement(cond, body);
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

ForStatement *create_for_statement(Statement *init, Expression *cond, Expression *post, Statement *body, int line, int col)
{
    ForStatement *stmt = new ForStatement(init, cond, post, body);
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

BreakStatement *create_break_statement(int line, int col)
{
    BreakStatement *stmt = new BreakStatement();
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

ContinueStatement *create_continue_statement(int line, int col)
{
    ContinueStatement *stmt = new ContinueStatement();
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

DeclarationStatement *create_declaration_statement(Declaration *decl, int line, int col)
{
    DeclarationStatement *stmt = new DeclarationStatement(decl);
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

ExpressionStatement *create_expression_statement(Expression *expr, int line, int col)
{
    ExpressionStatement *stmt = new ExpressionStatement(expr);
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

CompoundStatement *create_compound_statement(int line, int col)
{
    CompoundStatement *stmt = new CompoundStatement();
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

CaseLabel *create_case_label(Expression *value, Statement *stmt, int line, int col)
{
    CaseLabel *label = new CaseLabel(value, stmt);
    label->line_no = line;
    label->column_no = col;
    return label;
}

DefaultLabel *create_default_label(Statement *stmt, int line, int col)
{
    DefaultLabel *label = new DefaultLabel(stmt);
    label->line_no = line;
    label->column_no = col;
    return label;
}

SwitchStatement *create_switch_statement(Expression *expr, Statement *body, int line, int col)
{
    SwitchStatement *stmt = new SwitchStatement(expr, body);
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

// ============================================================================
// ReturnStatement - return [expr];
// ============================================================================

ReturnStatement::ReturnStatement(Expression *e)
    : expr(e)
{
}

ReturnStatement::~ReturnStatement()
{
    if (expr)
        delete expr;
}

string ReturnStatement::to_string() const
{
    if (expr)
        return "return " + expr->to_string() + ";";
    return "return;";
}

void ReturnStatement::generate_tac()
{
    // Mark that current function has a return statement
    current_function_has_return = true;

    // Check if we're inside a function (current_function_return_type should be set)
    if (current_function_return_type.is_error())
    {
        SEM_ERROR(line_no, "Return statement outside of function");
        semantic_error_count++;
        return;
    }

    // Case 1: void function with return expression
    if (current_function_return_type.base_type == TYPE_VOID && expr != nullptr)
    {
        SEM_ERROR(line_no, "void function cannot return a value");
        semantic_error_count++;
        return;
    }

    // Case 2: non-void function without return expression
    if (current_function_return_type.base_type != TYPE_VOID && expr == nullptr)
    {
        SEM_ERROR(line_no, "non-void function must return a value");
        semantic_error_count++;
        return;
    }

    if (expr)
    {
        // Generate TAC for expression first so expr->type is set
        expr->generate_tac();
        code = expr->code;

        // Emit destructors for all active scopes (innermost-first, per-scope reverse order)
        if (!active_compound_stack.empty())
        {
            for (auto scope_it = active_compound_stack.rbegin(); scope_it != active_compound_stack.rend(); ++scope_it)
            {
                CompoundStatement *scope = *scope_it;
                for (auto sym_it = scope->constructed_locals.rbegin(); sym_it != scope->constructed_locals.rend(); ++sym_it)
                {
                    emit_destructor_for_symbol(*sym_it, code);
                }
            }
        }

        // Type check: compare expr->type against current_function_return_type (value)
        if (expr->type)
        {
            const Type &retT = current_function_return_type;
            const Type &exprT = *expr->type;

            // Use unified type compatibility checking
            if (!is_type_compatible(retT, exprT, true))
            {
                SEM_ERROR(line_no, "Cannot return type '%s' from function expecting '%s'",
                          exprT.to_string().c_str(), retT.to_string().c_str());
                semantic_error_count++;
            }
            else if (should_warn_implicit_conversion(retT, exprT))
            {
                SEM_WARN(line_no, "Implicit conversion in return from '%s' to '%s'",
                         exprT.to_string().c_str(), retT.to_string().c_str());
            }
        }
        else
        {
            // Expression failed to produce a type (likely undefined identifier)
            SEM_ERROR(line_no, "return expression has no type (undefined or invalid)");
            semantic_error_count++;
        }
        // Emit TAC only if we have a valid result operand
        if (expr->result)
        {
            tacGen.emit(TAC_RETURN, TACOperand(), *expr->result);
            code.push_back(tacGen.getCode().back());
        }
        else
        {
            // Fallback: emit a bare return to avoid dangling control flow
            tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
            code.push_back(tacGen.getCode().back());
        }
    }
    else
    {
        // No return value: still ensure destructors run before returning
        if (!active_compound_stack.empty())
        {
            for (auto scope_it = active_compound_stack.rbegin(); scope_it != active_compound_stack.rend(); ++scope_it)
            {
                CompoundStatement *scope = *scope_it;
                for (auto sym_it = scope->constructed_locals.rbegin(); sym_it != scope->constructed_locals.rend(); ++sym_it)
                {
                    emit_destructor_for_symbol(*sym_it, code);
                }
            }
        }
        tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
        code.push_back(tacGen.getCode().back());
    }

    if (debug)
        printf("[AST] ReturnStatement: Generated TAC for return statement\n");
}

ReturnStatement *create_return_statement(Expression *expr, int line, int col)
{
    ReturnStatement *stmt = new ReturnStatement(expr);
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

// ============================================================================
// GotoStatement Implementation
// ============================================================================

GotoStatement::GotoStatement(const std::string &label)
    : label_name(label)
{
}

GotoStatement::~GotoStatement()
{
}

string GotoStatement::to_string() const
{
    return "goto " + label_name + ";";
}

void GotoStatement::generate_tac()
{
    // Emit a goto instruction to the target label
    int goto_instr = tacGen.emit(TAC_GOTO, TACOperand(TACOperand::OPERAND_LABEL, label_name), TACOperand());
    code.push_back(tacGen.getCode().back());

    // Register this goto for backpatching when the label is found
    tacGen.emit_goto(label_name, goto_instr);

    if (debug)
        printf("[AST] GotoStatement: Generated goto to label '%s'\n", label_name.c_str());
}

// ============================================================================
// LabelStatement Implementation
// ============================================================================

LabelStatement::LabelStatement(const std::string &label, Statement *stmt)
    : label_name(label), statement(stmt)
{
    // Do NOT emit TAC or register the label here.
    // Label will be emitted and registered in generate_tac().
}

LabelStatement::~LabelStatement()
{
    if (statement)
        delete statement;
}

string LabelStatement::to_string() const
{
    if (statement)
        return label_name + ": " + statement->to_string();
    return label_name + ":";
}

void LabelStatement::generate_tac()
{
    // Register the label at the current position without emitting a TAC instruction
    // This allows gotos to jump to the next instruction that will be emitted
    tacGen.register_label_at_current_position(label_name);

    // Generate code for the statement following the label
    if (statement)
    {
        statement->generate_tac();
        // Append the statement's code
        code.insert(code.end(), statement->code.begin(), statement->code.end());

        // Propagate control flow lists
        nextlist = statement->nextlist;
        breaklist = statement->breaklist;
        continuelist = statement->continuelist;
    }

    if (debug)
        printf("[AST] LabelStatement: Registered label '%s' at current position\n", label_name.c_str());
}

// ============================================================================
// Helper Functions for Goto and Label Statements
// ============================================================================

GotoStatement *create_goto_statement(const std::string &label, int line, int col)
{
    GotoStatement *stmt = new GotoStatement(label);
    stmt->line_no = line;
    stmt->column_no = col;
    return stmt;
}

LabelStatement *create_label_statement(const std::string &label, Statement *stmt, int line, int col)
{
    LabelStatement *label_stmt = new LabelStatement(label, stmt);
    label_stmt->line_no = line;
    label_stmt->column_no = col;
    return label_stmt;
}

// ============================================================================
// Helper: emit destructor call for a constructed class object symbol
// ============================================================================
static void emit_destructor_for_symbol(Symbol *sym, std::vector<TACInstruction *> &out)
{
    if (!sym)
        return;

    // Only for class objects (non-pointer direct objects)
    if (!sym->type.is_class || sym->type.pointer_level > 0)
        return;

    const std::string &class_name = sym->type.class_name;
    // Check if destructor exists: ~ClassName with no parameters
    std::vector<Type> no_params;
    // For destructor calls, use find_method_call_match to allow implicit conversions
    MethodSignature *dtor = find_method_call_match(class_name, "~" + class_name, no_params);
    if (!dtor)
    {
        // No user-declared destructor; skip silently
        return;
    }

    // Build object expression referring to the exact symbol (ensure correct mangling)
    PrimaryExpression *obj = create_primary_expression(sym->name);
    obj->symbol_ref = sym; // force correct symbol for mangling

    std::vector<Expression *> empty_args;
    MethodCallExpression *call = create_method_call_expression(obj, ("~" + class_name).c_str(), &empty_args);
    call->generate_tac();
    // Append generated TAC
    out.insert(out.end(), call->code.begin(), call->code.end());
    delete call; // cleans up obj too
}
