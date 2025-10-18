#include "statement.h"
#include "declaration.h"
#include "symbol_table.h"
#include <iostream>
#include <cstdio>

using namespace std;

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
        fprintf(stderr, "[Error] Line %d: 'break' statement not within loop or switch\n", line_no);
        return;
    }

    // Generate a goto that will be backpatched to loop exit
    int goto_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());

    // Add to breaklist (will be backpatched by enclosing loop to exit)
    breaklist = makelist(goto_instr);

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
        fprintf(stderr, "[Error] Line %d: 'continue' statement not within loop\n", line_no);
        return;
    }

    // Generate a goto that will be backpatched to loop beginning/post
    int goto_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());

    // Add to continuelist (will be backpatched by enclosing loop to beginning/post)
    continuelist = makelist(goto_instr);

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
    if (declaration)
    {
        declaration->generate_tac();
        code = declaration->code;
    }
    // No control flow, so nextlist stays empty
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

        // Propagate break and continue lists (they bubble up to enclosing loop)
        breaklist = merge(breaklist, stmt->breaklist);
        continuelist = merge(continuelist, stmt->continuelist);
    }

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
        fprintf(stderr, "[Type Error] Line %d: Case label must be an integer or character constant\n",
                line_no);
    }

    // Generate code for the statement following the case label
    statement->generate_tac();
    code = statement->code;

    // Propagate control flow lists
    nextlist = statement->nextlist;
    breaklist = statement->breaklist;
    continuelist = statement->continuelist;

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

    printf("[AST] DefaultLabel: Generated TAC for default label\n");
}

// ============================================================================
// SwitchStatement - switch (expr) { case ...: ... default: ... }
// ============================================================================

SwitchStatement::SwitchStatement(Expression *expr, Statement *body_stmt)
    : switch_expr(expr), body(body_stmt), default_label(-1)
{
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
        // Found a case label - record it (we'll get the position during generation)
        // For now, just mark it as -1, we'll update during generate_tac
        case_labels.push_back({case_label->case_value, -1});
    }
    else if (DefaultLabel *default_label_stmt = dynamic_cast<DefaultLabel *>(stmt))
    {
        // Found default label
        default_label = -1; // Will be updated during generate_tac
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

void SwitchStatement::generate_tac()
{
    // ========================================================================
    // Switch Statement TAC Generation
    // ========================================================================
    //
    // Pattern:
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
    // ========================================================================

    printf("[AST] SwitchStatement: Generating TAC\n");

    // STEP 1: Type check - switch expression must be integer type
    switch_expr->generate_tac();

    if (!switch_expr->type || !switch_expr->type->is_integer())
    {
        fprintf(stderr, "[Type Error] Line %d: Switch expression must be an integer type\n",
                line_no);
        return;
    }

    // STEP 2: Collect case and default labels by scanning body structurally (without generating code)
    case_labels.clear();
    default_label = -1;
    collect_labels(body);

    printf("[AST] SwitchStatement: Found %zu case labels\n", case_labels.size());

    // STEP 3: Start building our code - add switch expression evaluation
    code.insert(code.end(), switch_expr->code.begin(), switch_expr->code.end());

    // Store switch result for comparison
    TACOperand switch_result = *switch_expr->result;

    // STEP 4: Generate dispatch code (comparisons and conditional jumps)
    // These will be backpatched with actual case positions once we know them
    std::vector<int> case_jump_instructions;

    for (size_t i = 0; i < case_labels.size(); i++)
    {
        Expression *case_expr = case_labels[i].first;

        // Evaluate case expression (should be constant)
        case_expr->generate_tac();
        code.insert(code.end(), case_expr->code.begin(), case_expr->code.end());

        // Generate comparison: _t = switch_result == case_value
        TACOperand temp = tacGen.newTemp();
        tacGen.emit(TAC_EQ, temp, switch_result, *case_expr->result);
        code.push_back(tacGen.getCode().back());

        // Generate conditional jump: if _t goto case_label
        int jump_instr = tacGen.emit(TAC_IF_GOTO, TACOperand(), temp);
        code.push_back(tacGen.getCode().back());

        // Remember this instruction for backpatching
        case_jump_instructions.push_back(jump_instr);
    }

    // If no cases match, goto default (or EXIT if no default)
    int default_goto_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
    code.push_back(tacGen.getCode().back());

    // STEP 5: Now generate the body code
    // Track where each case label starts
    std::vector<int> case_positions;
    int default_position = -1;

    // Accumulate all break statements from the body
    std::vector<int> accumulated_breaklist;

    if (CompoundStatement *compound = dynamic_cast<CompoundStatement *>(body))
    {
        for (Statement *stmt : compound->statements)
        {
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

    // STEP 6: Backpatch case jumps to their actual positions
    for (size_t i = 0; i < case_jump_instructions.size() && i < case_positions.size(); i++)
    {
        tacGen.getCode()[case_jump_instructions[i]]->target_line = case_positions[i];
    }

    // STEP 7: Backpatch the default goto
    int exit_label = tacGen.nextinstr();

    if (default_position != -1)
    {
        tacGen.getCode()[default_goto_instr]->target_line = default_position;
    }
    else
    {
        // No default - goto EXIT
        tacGen.getCode()[default_goto_instr]->target_line = exit_label;
    }

    // STEP 8: Backpatch all break statements to EXIT
    backpatch(accumulated_breaklist, exit_label);

    // Switch statement's nextlist is empty
    nextlist.clear();

    printf("[AST] SwitchStatement: Generated TAC with %zu cases, EXIT at line %d\n",
           case_labels.size(), exit_label);
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

DoWhileStatement *create_dowhile_statement(Statement *body, Expression *cond)
{
    return new DoWhileStatement(body, cond);
}

UntilStatement *create_until_statement(Expression *cond, Statement *body)
{
    return new UntilStatement(cond, body);
}

ForStatement *create_for_statement(Statement *init, Expression *cond, Expression *post, Statement *body)
{
    return new ForStatement(init, cond, post, body);
}

BreakStatement *create_break_statement()
{
    return new BreakStatement();
}

ContinueStatement *create_continue_statement()
{
    return new ContinueStatement();
}

DeclarationStatement *create_declaration_statement(Declaration *decl)
{
    return new DeclarationStatement(decl);
}

ExpressionStatement *create_expression_statement(Expression *expr)
{
    return new ExpressionStatement(expr);
}

CompoundStatement *create_compound_statement()
{
    return new CompoundStatement();
}

CaseLabel *create_case_label(Expression *value, Statement *stmt)
{
    return new CaseLabel(value, stmt);
}

DefaultLabel *create_default_label(Statement *stmt)
{
    return new DefaultLabel(stmt);
}

SwitchStatement *create_switch_statement(Expression *expr, Statement *body)
{
    return new SwitchStatement(expr, body);
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
    // Check if we're inside a function (current_function_return_type should be set)
    if (current_function_return_type.base_type == TYPE_ERROR)
    {
        fprintf(stderr, "[Error] Line %d: Return statement outside of function\n", line_no);
        return;
    }

    // Case 1: void function with return expression
    if (current_function_return_type.base_type == TYPE_VOID && expr != nullptr)
    {
        fprintf(stderr, "[Error] Line %d: void function cannot return a value\n", line_no);
        return;
    }

    // Case 2: non-void function without return expression
    if (current_function_return_type.base_type != TYPE_VOID && expr == nullptr)
    {
        fprintf(stderr, "[Error] Line %d: non-void function must return a value\n", line_no);
        return;
    }

    if (expr)
    {
        // Generate TAC for expression first so expr->type is set
        expr->generate_tac();
        code = expr->code;

        // Type check: compare expr->type against current_function_return_type (value)
        if (expr->type)
        {
            // Check for allowed promotions manually (Phase 1 rules)
            bool compatible = false;

            // Exact match
            if (expr->type->base_type == current_function_return_type.base_type)
            {
                compatible = true;
            }
            // Char to int promotion
            else if (expr->type->base_type == TYPE_CHAR && current_function_return_type.base_type == TYPE_INT)
            {
                compatible = true;
            }
            // Int to float promotion
            else if (expr->type->is_integer() && current_function_return_type.base_type == TYPE_FLOAT)
            {
                compatible = true;
            }
            // Integer types to each other (int, char)
            else if (expr->type->is_integer() && current_function_return_type.is_integer())
            {
                compatible = true;
            }

            if (!compatible)
            {
                fprintf(stderr, "[Type Error] Line %d: Cannot return type '%s' from function expecting '%s'\n",
                        line_no, expr->type->to_string().c_str(), current_function_return_type.to_string().c_str());
            }
        }

        tacGen.emit(TAC_RETURN, TACOperand(), *expr->result);
        code.push_back(tacGen.getCode().back());
    }
    else
    {
        tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
        code.push_back(tacGen.getCode().back());
    }

    printf("[AST] ReturnStatement: Generated TAC for return statement\n");
}

ReturnStatement *create_return_statement(Expression *expr)
{
    return new ReturnStatement(expr);
}
