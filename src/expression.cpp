#include "expression.h"
#include "symbol_table.h"
#include "diagnostics.h"
#include <iostream>
#include <sstream>
#include <string>

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

// Helper function to get operator name for error messages
const char *get_operator_name(TACOp op)
{
    switch (op)
    {
    case TAC_ADD:
        return "+";
    case TAC_SUB:
        return "-";
    case TAC_MUL:
        return "*";
    case TAC_DIV:
        return "/";
    case TAC_MOD:
        return "%";
    case TAC_BITWISE_AND:
        return "&";
    case TAC_BITWISE_OR:
        return "|";
    case TAC_BITWISE_XOR:
        return "^";
    case TAC_LEFT_SHIFT:
        return "<<";
    case TAC_RIGHT_SHIFT:
        return ">>";
    case TAC_LT:
        return "<";
    case TAC_GT:
        return ">";
    case TAC_LE:
        return "<=";
    case TAC_GE:
        return ">=";
    case TAC_EQ:
        return "==";
    case TAC_NE:
        return "!=";
    case TAC_LOGICAL_AND:
        return "&&";
    case TAC_LOGICAL_OR:
        return "||";
    default:
        return "unknown";
    }
}

// ============================================================================
// Expression Implementation
// ============================================================================
// This file contains implementations for all expression node types:
//   1. Expression Base Class
//   2. Primary Expressions (identifiers, constants)
//   3. Binary Expressions (arithmetic, bitwise, comparison)
//   4. Unary Expressions (prefix inc/dec, negation, bitwise NOT)
//   5. Assignment Expressions
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
    : prim_type(PRIM_IDENTIFIER), name(id_name), int_value(0), char_value('\0'), float_value(0.0), bool_value(false), expr(nullptr), symbol_ref(nullptr)
{
    // Look up symbol during construction (while in correct scope)
    symbol_ref = lookup_symbol(id_name);
}

PrimaryExpression::PrimaryExpression(int value)
    : prim_type(PRIM_INT_CONSTANT), int_value(value), char_value('\0'), float_value(0.0), bool_value(false), expr(nullptr), symbol_ref(nullptr)
{
}

PrimaryExpression::PrimaryExpression(char value)
    : prim_type(PRIM_CHAR_CONSTANT), int_value(0), char_value(value), float_value(0.0), bool_value(false), expr(nullptr), symbol_ref(nullptr)
{
}

PrimaryExpression::PrimaryExpression(double value)
    : prim_type(PRIM_FLOAT_CONSTANT), int_value(0), char_value('\0'), float_value(value), bool_value(false), expr(nullptr), symbol_ref(nullptr)
{
}

PrimaryExpression::PrimaryExpression(const string &str, bool is_string_literal)
    : prim_type(PRIM_STRING_LITERAL), int_value(0), char_value('\0'), float_value(0.0), bool_value(false), string_value(str), expr(nullptr), symbol_ref(nullptr)
{
}

PrimaryExpression::PrimaryExpression(Expression *e)
    : prim_type(PRIM_PAREN_EXPR), int_value(0), char_value('\0'), float_value(0.0), bool_value(false), expr(e), symbol_ref(nullptr)
{
}

PrimaryExpression::PrimaryExpression(bool value)
    : prim_type(PRIM_BOOL_CONSTANT), int_value(0), char_value('\0'), float_value(0.0), bool_value(value), expr(nullptr), symbol_ref(nullptr)
{
}

PrimaryExpression::PrimaryExpression()
    : prim_type(PRIM_NULL_CONSTANT), int_value(0), char_value('\0'), float_value(0.0), bool_value(false), expr(nullptr), symbol_ref(nullptr)
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
    case PRIM_STRING_LITERAL:
        return string_value;
    case PRIM_BOOL_CONSTANT:
        return bool_value ? "true" : "false";
    case PRIM_NULL_CONSTANT:
        return "null";
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
        // Check if this is an enum member first
        if (is_enum_member(name))
        {
            // Replace enum member with its constant value
            int value = get_enum_member_value(name);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, std::to_string(value));
            type = new Type(TYPE_INT);
            if (debug)
            {
                cout << "[AST] Enum member: " << name << " = " << value << endl;
            }
            break;
        }

        // Check if we're inside a method and this might be a member access
        if (current_method_signature != nullptr)
        {
            // Look up the class this method belongs to
            ClassType *class_type = lookup_class_in_scope(current_method_signature->class_name);
            if (class_type)
            {
                // Check if this identifier is a class member
                bool is_member = false;
                Type member_type;
                size_t member_offset = 0;

                for (size_t i = 0; i < class_type->members.size(); i++)
                {
                    if (class_type->members[i].first == name)
                    {
                        is_member = true;
                        member_type = *(class_type->members[i].second);
                        member_offset = class_type->member_offsets[class_type->members[i].first];
                        break;
                    }
                }

                if (is_member)
                {
                    // Access member via implicit 'this' pointer (param_0)
                    // Generate: temp = *(param_0 + offset)
                    TACOperand this_ptr(TACOperand::OPERAND_IDENTIFIER, "param_0");
                    TACOperand offset_operand(TACOperand::OPERAND_CONSTANT, std::to_string(member_offset));

                    // Calculate address: this + offset
                    Type *ptr_type = new Type(member_type);
                    ptr_type->pointer_level++;
                    TACOperand addr_temp = tacGen.newTemp(ptr_type);
                    tacGen.emit(TAC_ADD, addr_temp, this_ptr, offset_operand);

                    // Dereference to get member value
                    TACOperand member_temp = tacGen.newTemp(&member_type);
                    tacGen.emit(TAC_DEREF, member_temp, addr_temp, TACOperand());

                    result = new TACOperand(member_temp);
                    type = new Type(member_type);

                    if (debug)
                    {
                        cout << "[AST] Member access via 'this': " << name
                             << " (offset " << member_offset << ")" << endl;
                    }
                    break;
                }
            }
        }

        // Otherwise, normal identifier lookup
        // Use cached symbol from construction time (correct scope)
        Symbol *sym = symbol_ref;
        if (!sym)
        {
            SEM_ERROR(line_no, "Undefined variable '%s'", name.c_str());
            result = new TACOperand(TACOperand::OPERAND_IDENTIFIER, name);
            // Propagate semantic error and mark type as error
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
        }
        else
        {
            // Use mangled name with scope: name_scope
            string mangled_name = mangle_for_tac(name, sym);

            // Handle references: automatically dereference them
            if (sym->type.is_reference)
            {
                // References are stored as pointers, so we need to dereference
                // Generate: temp = *reference_var
                TACOperand ref_ptr(TACOperand::OPERAND_IDENTIFIER, mangled_name);

                // Create the type without the reference flag
                Type *deref_type = new Type(sym->type);
                deref_type->is_reference = false;

                TACOperand deref_temp = tacGen.newTemp(deref_type);
                tacGen.emit(TAC_DEREF, deref_temp, ref_ptr, TACOperand());
                code.push_back(tacGen.getCode().back());

                result = new TACOperand(deref_temp);
                type = deref_type;

                if (debug)
                {
                    cout << "[AST] Auto-dereferencing reference: " << name << endl;
                }
            }
            else
            {
                // Normal variable access
                result = new TACOperand(TACOperand::OPERAND_IDENTIFIER, mangled_name);
                type = new Type(sym->type);
            }
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
        if (debug)
        {
            cout << "[AST] Character constant: " << char_repr
                 << " (ASCII: " << ascii_value << ")" << endl;
        }
        break;
    }

    case PRIM_FLOAT_CONSTANT:
    {
        result = new TACOperand(TACOperand::OPERAND_CONSTANT, std::to_string(float_value));
        type = new Type(TYPE_DOUBLE);
        if (debug)
        {
            cout << "[AST] Float constant: " << float_value << endl;
        }
        break;
    }

    case PRIM_STRING_LITERAL:
    {
        // String literals create a string operand
        result = new TACOperand(TACOperand::OPERAND_STRING, string_value);
        // String literals have type "pointer to char" (char*)
        type = new Type(TYPE_CHAR, 1); // base_type = char, pointer_level = 1
        if (debug)
        {
            cout << "[AST] String literal: " << string_value << endl;
        }
        break;
    }

    case PRIM_BOOL_CONSTANT:
    {
        // Boolean constants (true/false)
        result = new TACOperand(TACOperand::OPERAND_CONSTANT, bool_value ? "1" : "0");
        type = new Type(TYPE_BOOL);

        // Generate truelist/falselist for use in control flow contexts
        if (bool_value)
        {
            // true: generate unconditional jump (will go to truelist target)
            int jump_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
            code.push_back(tacGen.getCode().back());
            truelist.push_back(jump_instr);
            // falselist is empty (never take false branch)
        }
        else
        {
            // false: generate unconditional jump (will go to falselist target)
            int jump_instr = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
            code.push_back(tacGen.getCode().back());
            falselist.push_back(jump_instr);
            // truelist is empty (never take true branch)
        }

        if (debug)
        {
            cout << "[AST] Boolean constant: " << (bool_value ? "true" : "false") << endl;
        }
        break;
    }

    case PRIM_NULL_CONSTANT:
    {
        // Null pointer constants (null/nullptr)
        result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0");
        // Null constants have a special "null pointer" type - for now, use void*
        type = new Type(TYPE_VOID, 1); // void* type
        if (debug)
        {
            cout << "[AST] Null constant" << endl;
        }
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

        // Handle case where left operand doesn't have truelist/falselist
        if (left->truelist.empty() && left->falselist.empty())
        {
            // Generate: if left goto ___ (truelist)
            int true_jump = tacGen.emit(TAC_IF_GOTO, TACOperand(), *left->result);
            code.push_back(tacGen.getCode().back());

            // Generate: goto ___ (falselist)
            int false_jump = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
            code.push_back(tacGen.getCode().back());

            left->truelist = makelist(true_jump);
            left->falselist = makelist(false_jump);
        }

        // M: marker - current position before E2
        int M = tacGen.nextinstr();
        backpatch(left->truelist, M);

        right->generate_tac();
        code.insert(code.end(), right->code.begin(), right->code.end());

        // Handle case where right operand doesn't have truelist/falselist
        // (e.g., simple variable like 'a' in 'b && a')
        if (right->truelist.empty() && right->falselist.empty())
        {
            // Generate: if right goto ___ (truelist)
            int true_jump = tacGen.emit(TAC_IF_GOTO, TACOperand(), *right->result);
            code.push_back(tacGen.getCode().back());

            // Generate: goto ___ (falselist)
            int false_jump = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
            code.push_back(tacGen.getCode().back());

            right->truelist = makelist(true_jump);
            right->falselist = makelist(false_jump);
        }

        // Merge lists
        falselist = merge(left->falselist, right->falselist);
        truelist = right->truelist;

        // Set result and type for potential use in assignments
        // Note: The actual control flow is handled by truelist/falselist backpatching
        // We only generate explicit result if needed (e.g., bool b = a && c;)
        // For now, just set the result to the right operand
        result = right->result;
        type = new Type(TYPE_BOOL);

        // Type checking - logical operators accept numeric types or pointers (C semantics)
        if (!left->type || !right->type || left->type->is_error() || right->type->is_error())
        {
            type = new Type(TYPE_ERROR);
        }
        else if (!(left->type->is_numeric() || left->type->is_pointer() || left->type->base_type == TYPE_BOOL) ||
                 !(right->type->is_numeric() || right->type->is_pointer() || right->type->base_type == TYPE_BOOL))
        {
            SEM_ERROR(line_no,
                      "Logical operator '&&' requires numeric, pointer, or bool operands, got %s and %s",
                      left->type->to_string().c_str(), right->type->to_string().c_str());
            semantic_error_count++;
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

        // Handle case where left operand doesn't have truelist/falselist
        if (left->truelist.empty() && left->falselist.empty())
        {
            // Generate: if left goto ___ (truelist)
            int true_jump = tacGen.emit(TAC_IF_GOTO, TACOperand(), *left->result);
            code.push_back(tacGen.getCode().back());

            // Generate: goto ___ (falselist)
            int false_jump = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
            code.push_back(tacGen.getCode().back());

            left->truelist = makelist(true_jump);
            left->falselist = makelist(false_jump);
        }

        // M: marker - current position before E2
        int M = tacGen.nextinstr();
        backpatch(left->falselist, M);

        right->generate_tac();
        code.insert(code.end(), right->code.begin(), right->code.end());

        // Handle case where right operand doesn't have truelist/falselist
        if (right->truelist.empty() && right->falselist.empty())
        {
            // Generate: if right goto ___ (truelist)
            int true_jump = tacGen.emit(TAC_IF_GOTO, TACOperand(), *right->result);
            code.push_back(tacGen.getCode().back());

            // Generate: goto ___ (falselist)
            int false_jump = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
            code.push_back(tacGen.getCode().back());

            right->truelist = makelist(true_jump);
            right->falselist = makelist(false_jump);
        }

        // Merge lists
        truelist = merge(left->truelist, right->truelist);
        falselist = right->falselist;

        // Set result and type for potential use in assignments
        // Note: The actual control flow is handled by truelist/falselist backpatching
        result = right->result;
        type = new Type(TYPE_BOOL);

        // Type checking - logical operators accept numeric types or pointers (C semantics)
        if (!left->type || !right->type || left->type->is_error() || right->type->is_error())
        {
            type = new Type(TYPE_ERROR);
        }
        else if (!(left->type->is_numeric() || left->type->is_pointer() || left->type->base_type == TYPE_BOOL) ||
                 !(right->type->is_numeric() || right->type->is_pointer() || right->type->base_type == TYPE_BOOL))
        {
            SEM_ERROR(line_no,
                      "Logical operator '||' requires numeric, pointer, or bool operands, got %s and %s",
                      left->type->to_string().c_str(), right->type->to_string().c_str());
            semantic_error_count++;
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
        SEM_ERROR(line_no, "Missing type information in binary expression");
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
        return;
    }

    // Error propagation: if either operand is already an error, propagate it
    if (left->type->is_error() || right->type->is_error())
    {
        type = new Type(TYPE_ERROR);
        result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
        return;
    }

    // ========================================================================
    // ARRAY AND POINTER HANDLING
    // Arrays decay to pointers ONLY in valid contexts (arithmetic, comparison, assignment)
    // ========================================================================
    bool left_is_pointer = left->type->is_pointer();
    bool right_is_pointer = right->type->is_pointer();
    bool left_is_array = left->type->is_array;
    bool right_is_array = right->type->is_array;
    bool left_is_integer = left->type->is_integer();
    bool right_is_integer = right->type->is_integer();

    // Check for invalid array operations BEFORE allowing decay
    if (left_is_array || right_is_array)
    {
        // Arrays can ONLY be used in: +, -, ==, !=, <, >, <=, >=
        // All other operations (*, /, %, &, |, ^, <<, >>) are INVALID
        if (op != TAC_ADD && op != TAC_SUB &&
            op != TAC_LT && op != TAC_GT && op != TAC_LE && op != TAC_GE &&
            op != TAC_EQ && op != TAC_NE)
        {
            SEM_ERROR(line_no,
                      "Invalid operation '%s' on array type. Arrays can only be used with +, -, or comparison operators",
                      get_operator_name(op));
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0");
            return;
        }

        // Additional validation: array * array, array / array, etc. are invalid
        if ((op == TAC_MUL || op == TAC_DIV || op == TAC_MOD) && (left_is_array && right_is_array))
        {
            SEM_ERROR(line_no,
                      "Invalid operation '%s' between two arrays",
                      get_operator_name(op));
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0");
            return;
        }
    }

    // NOW we can allow array-to-pointer decay for valid operations
    bool left_is_pointer_like = left_is_pointer || left_is_array;
    bool right_is_pointer_like = right_is_pointer || right_is_array;

    // Handle pointer + integer (or array + integer)
    if (op == TAC_ADD && left_is_pointer_like && right_is_integer)
    {
        handle_pointer_plus_integer(left, right);
        return;
    }

    // Handle integer + pointer (or integer + array)
    if (op == TAC_ADD && left_is_integer && right_is_pointer_like)
    {
        handle_pointer_plus_integer(right, left);
        return;
    }

    // Handle pointer - integer (or array - integer)
    if (op == TAC_SUB && left_is_pointer_like && right_is_integer)
    {
        handle_pointer_minus_integer(left, right);
        return;
    }

    // Handle pointer - pointer (or array - pointer, or array - array)
    if (op == TAC_SUB && left_is_pointer_like && right_is_pointer_like)
    {
        handle_pointer_minus_pointer(left, right);
        return;
    }

    // ========================================================================
    // REJECT INVALID POINTER/ARRAY COMBINATIONS
    // ========================================================================

    // Addition: reject pointer + pointer, array + pointer, array + array (not handled above)
    if (op == TAC_ADD && left_is_pointer_like && right_is_pointer_like)
    {
        SEM_ERROR(line_no,
                  "Invalid addition: cannot add two pointers/arrays. Use pointer/array + integer instead. Got %s + %s",
                  left->type->to_string().c_str(), right->type->to_string().c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0");
        return;
    }

    // Subtraction: reject int - pointer/array (reverse subtraction not allowed)
    if (op == TAC_SUB && left_is_integer && right_is_pointer_like)
    {
        SEM_ERROR(line_no,
                  "Invalid subtraction: cannot subtract pointer/array from integer. Got %s - %s",
                  left->type->to_string().c_str(), right->type->to_string().c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0");
        return;
    }

    // ========================================================================
    // CONTINUE WITH NORMAL TYPE CHECKING FOR NON-POINTER OPERATIONS
    // ========================================================================

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
            SEM_ERROR(line_no,
                      "Modulo operator '%%' requires integer operands, got %s and %s",
                      left->type->to_string().c_str(), right->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
            return;
        }
    }
    // Bitwise operators require integer operands (int/char, not float/double)
    else if (op == TAC_BITWISE_AND || op == TAC_BITWISE_OR || op == TAC_BITWISE_XOR ||
             op == TAC_LEFT_SHIFT || op == TAC_RIGHT_SHIFT)
    {
        if (!left->type->is_integer() || !right->type->is_integer())
        {
            SEM_ERROR(line_no,
                      "Bitwise operator '%s' requires integer operands, got %s and %s",
                      op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
            return;
        }
    }
    // Ordering operators (<, >, <=, >=) require ONLY numeric operands (no pointers/arrays)
    else if (op == TAC_LT || op == TAC_GT || op == TAC_LE || op == TAC_GE)
    {
        if (!left->type->is_numeric() || !right->type->is_numeric())
        {
            SEM_ERROR(line_no,
                      "Ordering operator '%s' requires numeric operands only, got %s and %s",
                      op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
            return;
        }
    }
    // Equality operators (==, !=) allow numeric, pointer, and array comparisons
    else if (op == TAC_EQ || op == TAC_NE)
    {
        bool valid = false;

        // Case 1: Both operands are numeric
        if (left->type->is_numeric() && right->type->is_numeric())
        {
            valid = true;
        }
        // Case 2: Both operands are pointers/arrays of compatible types
        else if ((left->type->is_pointer() || left->type->is_array) &&
                 (right->type->is_pointer() || right->type->is_array))
        {
            // Pointer/array comparison rules:
            // 1. Same pointer/array types can be compared (arrays decay to pointers)
            // 2. void* can be compared with any pointer/array
            // 3. Any pointer/array can be compared with null constant
            if (is_type_compatible(*left->type, *right->type, true) ||
                is_type_compatible(*right->type, *left->type, true))
            {
                valid = true;
            }
        }
        // Case 3: One is a null constant and the other is a pointer/array
        // TODO: Implement null constant detection

        if (!valid)
        {
            SEM_ERROR(line_no,
                      "Equality operator '%s' requires compatible types, got %s and %s",
                      op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
            return;
        }
    }
    // Logical operators accept bool-compatible types (int, char, double, pointers, enum, bool)
    // but reject struct/class/union types
    else if (op == TAC_LOGICAL_AND || op == TAC_LOGICAL_OR)
    {
        if (!is_bool_compatible(*left->type) || !is_bool_compatible(*right->type))
        {
            SEM_ERROR(line_no,
                      "Logical operator '%s' requires bool-compatible operands (numeric, pointer, enum, or bool), got %s and %s",
                      op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
            return;
        }
    }
    // Other arithmetic operators require numeric operands
    else if (op == TAC_ADD || op == TAC_SUB || op == TAC_MUL || op == TAC_DIV)
    {
        if (!left->type->is_numeric() || !right->type->is_numeric())
        {
            SEM_ERROR(line_no,
                      "Operator '%s' requires numeric operands, got %s and %s",
                      op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
            return;
        }
    }

    // Determine result type using promotion rules
    // Phase 1: For bitwise/comparison/logical ops, result is int; for arithmetic, use type promotion
    if (op == TAC_BITWISE_AND || op == TAC_BITWISE_OR || op == TAC_BITWISE_XOR ||
        op == TAC_LEFT_SHIFT || op == TAC_RIGHT_SHIFT || op == TAC_MOD)
    {
        // Bitwise operations result in int
        type = new Type(TYPE_INT);
    }
    else if (op == TAC_LT || op == TAC_GT || op == TAC_LE || op == TAC_GE ||
             op == TAC_EQ || op == TAC_NE ||
             op == TAC_LOGICAL_AND || op == TAC_LOGICAL_OR)
    {
        // Comparison and logical operations return bool
        type = new Type(TYPE_BOOL);
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
        // For relational expressions, generate simple comparison result
        // And create truelist/falselist for backpatching in control flow contexts

        // Create new temporary for the comparison result (type is bool)
        TACOperand temp = tacGen.newTemp(type);
        result = new TACOperand(temp);

        // Emit the comparison operation (simple assignment)
        tacGen.emit(op, *result, *left->result, *right->result);
        code.push_back(tacGen.getCode().back());

        // Generate conditional jumps for backpatching
        // truelist: jump if condition is true (target to be filled later)
        int true_jump = tacGen.emit(TAC_IF_GOTO, TACOperand(), *result);
        code.push_back(tacGen.getCode().back());
        truelist.push_back(true_jump);

        // falselist: unconditional jump when condition is false (target to be filled later)
        int false_jump = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
        code.push_back(tacGen.getCode().back());
        falselist.push_back(false_jump);
    }
    else
    {
        // Arithmetic/bitwise operations: compute value normally
        // Create new temporary for result (with type)
        TACOperand temp = tacGen.newTemp(type);
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

// ============================================================================
// BinaryExpression: Pointer Arithmetic Helper Methods
// ============================================================================

void BinaryExpression::handle_pointer_plus_integer(Expression *ptr_expr, Expression *int_expr)
{
    // Get element size for scaling
    int elem_size = ptr_expr->type->get_element_size();

    // Calculate result type FIRST (before generating TAC)
    if (ptr_expr->type->is_array)
    {
        // Array decay removes the outermost dimension
        type = new Type(*ptr_expr->type);
        type->pointer_level = ptr_expr->type->pointer_level + 1;

        if (ptr_expr->type->array_dim > 1)
        {
            type->is_array = true;
            type->array_dim = ptr_expr->type->array_dim - 1;
            type->array_sizes = std::vector<int>(
                ptr_expr->type->array_sizes.begin() + 1,
                ptr_expr->type->array_sizes.end());
        }
        else
        {
            // Single dimension array decays to pointer (no array anymore)
            type->is_array = false;
            type->array_dim = 0;
            type->array_sizes.clear();
        }
    }
    else
    {
        // Pointer + int = same pointer type
        type = new Type(*ptr_expr->type);
    }

    // Combine code from both operands
    code.insert(code.end(), ptr_expr->code.begin(), ptr_expr->code.end());
    code.insert(code.end(), int_expr->code.begin(), int_expr->code.end());

    // For arrays, we need to get the base address first (array decays to pointer)
    TACOperand ptr_operand;
    if (ptr_expr->type->is_array)
    {
        // Array needs address-of to decay to pointer
        Type *ptr_type = new Type(*ptr_expr->type);
        ptr_type->is_array = false;
        ptr_type->pointer_level = 1;
        ptr_operand = tacGen.newTemp(ptr_type);
        tacGen.emit(TAC_ADDR_OF, ptr_operand, *ptr_expr->result);
        code.push_back(tacGen.getCode().back());
    }
    else
    {
        ptr_operand = *ptr_expr->result;
    }

    // Step 1: Scale the integer by element size
    // _t1 = integer * elem_size
    Type *int_type = new Type(TYPE_INT);
    TACOperand scale_temp = tacGen.newTemp(int_type);
    TACOperand size_operand(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));
    tacGen.emit(TAC_MUL, scale_temp, *int_expr->result, size_operand);
    code.push_back(tacGen.getCode().back());

    // Step 2: Add scaled offset to pointer
    // result = pointer + _t1
    TACOperand result_temp = tacGen.newTemp(type);
    tacGen.emit(TAC_ADD, result_temp, ptr_operand, scale_temp);
    code.push_back(tacGen.getCode().back());

    // Result is the computed pointer (type was calculated earlier)
    result = new TACOperand(result_temp);

    if (debug)
        printf("[AST] Pointer arithmetic: pointer + integer (scaled by %d)\n", elem_size);
}

void BinaryExpression::handle_pointer_minus_integer(Expression *ptr_expr, Expression *int_expr)
{
    // Get element size for scaling
    int elem_size = ptr_expr->type->get_element_size();

    // Calculate result type FIRST (before generating TAC)
    if (ptr_expr->type->is_array)
    {
        // Array decay removes the outermost dimension
        type = new Type(*ptr_expr->type);
        type->pointer_level = ptr_expr->type->pointer_level + 1;

        if (ptr_expr->type->array_dim > 1)
        {
            type->is_array = true;
            type->array_dim = ptr_expr->type->array_dim - 1;
            type->array_sizes = std::vector<int>(
                ptr_expr->type->array_sizes.begin() + 1,
                ptr_expr->type->array_sizes.end());
        }
        else
        {
            type->is_array = false;
            type->array_dim = 0;
            type->array_sizes.clear();
        }
    }
    else
    {
        // Pointer - int = same pointer type
        type = new Type(*ptr_expr->type);
    }

    // Combine code from both operands
    code.insert(code.end(), ptr_expr->code.begin(), ptr_expr->code.end());
    code.insert(code.end(), int_expr->code.begin(), int_expr->code.end());

    // For arrays, we need to get the base address first (array decays to pointer)
    TACOperand ptr_operand;
    if (ptr_expr->type->is_array)
    {
        // Array needs address-of to decay to pointer
        Type *ptr_type = new Type(*ptr_expr->type);
        ptr_type->is_array = false;
        ptr_type->pointer_level = 1;
        ptr_operand = tacGen.newTemp(ptr_type);
        tacGen.emit(TAC_ADDR_OF, ptr_operand, *ptr_expr->result);
        code.push_back(tacGen.getCode().back());
    }
    else
    {
        ptr_operand = *ptr_expr->result;
    }

    // Step 1: Scale the integer by element size
    // _t1 = integer * elem_size
    Type *int_type = new Type(TYPE_INT);
    TACOperand scale_temp = tacGen.newTemp(int_type);
    TACOperand size_operand(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));
    tacGen.emit(TAC_MUL, scale_temp, *int_expr->result, size_operand);
    code.push_back(tacGen.getCode().back());

    // Step 2: Subtract scaled offset from pointer
    // result = pointer - _t1
    TACOperand result_temp = tacGen.newTemp(type);
    tacGen.emit(TAC_SUB, result_temp, ptr_operand, scale_temp);
    code.push_back(tacGen.getCode().back());

    // Result is the computed pointer (type was calculated earlier)
    result = new TACOperand(result_temp);

    if (debug)
        printf("[AST] Pointer arithmetic: pointer - integer (scaled by %d)\n", elem_size);
}

void BinaryExpression::handle_pointer_minus_pointer(Expression *left_ptr, Expression *right_ptr)
{
    // For type checking, treat arrays as decayed pointers
    Type left_type_decayed = *left_ptr->type;
    Type right_type_decayed = *right_ptr->type;

    if (left_ptr->type->is_array)
    {
        left_type_decayed.is_array = false;
        left_type_decayed.pointer_level = 1;
        left_type_decayed.array_dim = 0;
        left_type_decayed.array_sizes.clear();
    }

    if (right_ptr->type->is_array)
    {
        right_type_decayed.is_array = false;
        right_type_decayed.pointer_level = 1;
        right_type_decayed.array_dim = 0;
        right_type_decayed.array_sizes.clear();
    }

    // Type check: must be compatible pointer types
    if (left_type_decayed.base_type != right_type_decayed.base_type ||
        left_type_decayed.pointer_level != right_type_decayed.pointer_level)
    {
        SEM_ERROR(line_no,
                  "Incompatible pointer types in subtraction: %s - %s",
                  left_ptr->type->to_string().c_str(), right_ptr->type->to_string().c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
        return;
    }

    int elem_size = left_ptr->type->get_element_size();

    // Combine code from both operands
    code.insert(code.end(), left_ptr->code.begin(), left_ptr->code.end());
    code.insert(code.end(), right_ptr->code.begin(), right_ptr->code.end());

    // Get pointer addresses (handle array decay)
    TACOperand left_operand;
    if (left_ptr->type->is_array)
    {
        Type *ptr_type = new Type(*left_ptr->type);
        ptr_type->is_array = false;
        ptr_type->pointer_level = 1;
        left_operand = tacGen.newTemp(ptr_type);
        tacGen.emit(TAC_ADDR_OF, left_operand, *left_ptr->result);
        code.push_back(tacGen.getCode().back());
    }
    else
    {
        left_operand = *left_ptr->result;
    }

    TACOperand right_operand;
    if (right_ptr->type->is_array)
    {
        Type *ptr_type = new Type(*right_ptr->type);
        ptr_type->is_array = false;
        ptr_type->pointer_level = 1;
        right_operand = tacGen.newTemp(ptr_type);
        tacGen.emit(TAC_ADDR_OF, right_operand, *right_ptr->result);
        code.push_back(tacGen.getCode().back());
    }
    else
    {
        right_operand = *right_ptr->result;
    }

    // Step 1: Byte-level subtraction
    // _t1 = left_ptr - right_ptr
    Type *int_type = new Type(TYPE_INT);
    TACOperand diff_temp = tacGen.newTemp(int_type);
    tacGen.emit(TAC_SUB, diff_temp, left_operand, right_operand);
    code.push_back(tacGen.getCode().back());

    // Step 2: Unscale by element size to get number of elements
    // result = _t1 / elem_size
    TACOperand result_temp = tacGen.newTemp(int_type);
    TACOperand size_operand(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));
    tacGen.emit(TAC_DIV, result_temp, diff_temp, size_operand);
    code.push_back(tacGen.getCode().back());

    // Result is integer (ptrdiff_t)
    result = new TACOperand(result_temp);
    type = new Type(TYPE_INT);

    if (debug)
        printf("[AST] Pointer arithmetic: pointer - pointer (unscaled by %d)\n", elem_size);
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
        SEM_ERROR(line_no, "Missing type information in unary expression");
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // Error propagation
    if (expr->type->is_error())
    {
        type = new Type(TYPE_ERROR);
        return;
    }

    // Address-of operator (&)
    if (op == TAC_ADDR_OF)
    {
        // Special case: &arr[i] - take address of array element
        ArrayAccessExpression *array_expr = dynamic_cast<ArrayAccessExpression *>(expr);
        if (array_expr)
        {
            // For &arr[i], we want the address that arr[i] calculates (without the final dereference)
            array_expr->array->generate_tac();
            array_expr->index->generate_tac();

            code = array_expr->array->code;
            code.insert(code.end(), array_expr->index->code.begin(), array_expr->index->code.end());

            // Calculate element size and offset
            int elem_size = array_expr->array->type->get_element_size();

            // t1 = index * elem_size
            Type *int_type = new Type(TYPE_INT);
            TACOperand t1 = tacGen.newTemp(int_type);
            TACOperand elem_size_op(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));
            tacGen.emit(TAC_MUL, t1, *array_expr->index->result, elem_size_op);
            code.push_back(tacGen.getCode().back());

            // result = array + t1 (this is the address we want!)
            TACOperand temp = tacGen.newTemp(type);
            result = new TACOperand(temp);
            tacGen.emit(TAC_ADD, *result, *array_expr->array->result, t1);
            code.push_back(tacGen.getCode().back());

            // Result type: pointer to element type
            type = new Type(*array_expr->type);
            type->pointer_level++;

            return;
        }

        // Check if operand is a member access expression (struct.member or ptr->member)
        MemberAccessExpression *member_expr = dynamic_cast<MemberAccessExpression *>(expr);
        MemberAccessPtrExpression *ptr_member_expr = dynamic_cast<MemberAccessPtrExpression *>(expr);

        if (member_expr || ptr_member_expr)
        {
            // For member access, we already computed the address in the second-to-last instruction
            // We just need to return that address instead of the loaded value
            code = expr->code;

            if (code.size() >= 2)
            {
                // Get the address from the second-to-last instruction (before the dereference)
                TACInstruction *addr_instr = code[code.size() - 2];
                result = new TACOperand(addr_instr->result);

                // Remove the last instruction (the dereference) since we want the address
                code.pop_back();

                // Result type: pointer to member type
                type = new Type(*expr->type);
                type->pointer_level++;

                return;
            }
        }

        // Check that operand is an lvalue (can take address)
        if (!expr->result || expr->result->type != TACOperand::OPERAND_IDENTIFIER)
        {
            SEM_ERROR(line_no, "Cannot take address of non-lvalue (not a variable)");
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }

        // Result type: pointer to operand's type
        type = new Type(*expr->type);
        type->pointer_level++;

        // TAC Generation
        code = expr->code;
        TACOperand temp = tacGen.newTemp(type);
        result = new TACOperand(temp);
        tacGen.emit(TAC_ADDR_OF, *result, *expr->result);
        code.push_back(tacGen.getCode().back());

        return;
    }

    // Dereference operator (*)
    if (op == TAC_DEREF)
    {
        // Check that operand is a pointer or array
        if (!expr->type->is_pointer() && !expr->type->is_array)
        {
            SEM_ERROR(line_no, "Cannot dereference non-pointer type %s",
                      expr->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }

        // Result type: decrease pointer level or reduce array dimension
        type = new Type(*expr->type);

        // For pointer to array (e.g., int (*)[4]), decrease pointer level
        // The result is still an array type
        if (type->pointer_level > 0)
        {
            type->pointer_level--;
        }
        // For pure arrays (not pointers to arrays), decrease array dimension
        else if (type->is_array)
        {
            type->array_dim--;
            if (!type->array_sizes.empty())
            {
                type->array_sizes.erase(type->array_sizes.begin());
            }
            if (type->array_dim == 0)
            {
                type->is_array = false;
            }
        }

        // TAC Generation
        code = expr->code;
        TACOperand temp = tacGen.newTemp(type);
        result = new TACOperand(temp);
        tacGen.emit(TAC_DEREF, *result, *expr->result);
        code.push_back(tacGen.getCode().back());

        return;
    }

    // Unary + and - require numeric operands
    if (op == TAC_UMINUS || op == TAC_UPLUS)
    {
        if (!expr->type->is_numeric())
        {
            const char *op_name = (op == TAC_UMINUS) ? "-" : "+";
            SEM_ERROR(line_no, "Unary '%s' requires numeric operand, got %s",
                      op_name, expr->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }
    }
    // Bitwise NOT requires integer operand
    else if (op == TAC_BITWISE_NOT)
    {
        if (!expr->type->is_integer())
        {
            SEM_ERROR(line_no, "Bitwise NOT '~' requires integer operand, got %s",
                      expr->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }
    }
    // Logical NOT requires bool-compatible operands (int, char, double, pointers, enum, bool)
    // but rejects struct/class/union types
    else if (op == TAC_LOGICAL_NOT)
    {
        if (!is_bool_compatible(*expr->type))
        {
            SEM_ERROR(line_no,
                      "Logical NOT '!' requires bool-compatible operand (numeric, pointer, enum, or bool), got %s",
                      expr->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }
    }
    // Prefix increment/decrement require numeric lvalue operands
    else if (op == TAC_PRE_INC || op == TAC_PRE_DEC)
    {
        const char *op_name = (op == TAC_PRE_INC) ? "++" : "--";

        if (!expr->type->is_numeric() && !expr->type->is_pointer())
        {
            SEM_ERROR(line_no,
                      "Prefix '%s' requires numeric or pointer operand, got %s",
                      op_name, expr->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }

        // Check that operand is an lvalue (modifiable variable)
        // expr->result should be an OPERAND_IDENTIFIER, not a constant or temp
        if (!expr->result || expr->result->type != TACOperand::OPERAND_IDENTIFIER)
        {
            SEM_ERROR(line_no, "Prefix '%s' requires an lvalue (modifiable variable)", op_name);
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }
    }

    // Result type: bitwise NOT and logical NOT have different return types
    if (op == TAC_BITWISE_NOT)
    {
        // Bitwise NOT promotes char to int but otherwise keeps the type
        type = new Type(TYPE_INT);
    }
    else if (op == TAC_LOGICAL_NOT)
    {
        // Logical NOT returns bool (true/false)
        type = new Type(TYPE_BOOL);
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

        // Check if it's a pointer - need to scale by element size
        if (expr->type->is_pointer())
        {
            // Pointer increment: p++ means p = p + sizeof(*p)
            int elem_size = expr->type->get_element_size();
            TACOperand size_operand(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));

            // Generate: expr = expr +/- elem_size
            TACOp add_or_sub = (op == TAC_PRE_INC) ? TAC_ADD : TAC_SUB;
            tacGen.emit(add_or_sub, *result, *result, size_operand);
            code.push_back(tacGen.getCode().back());
        }
        else
        {
            // Normal integer increment: x = x + 1 or x = x - 1
            tacGen.emit(op, *result, TACOperand());
            code.push_back(tacGen.getCode().back());
        }
    }
    else
    {
        // Regular unary operations: create new temporary
        TACOperand temp = tacGen.newTemp(type);
        result = new TACOperand(temp);

        // Emit the operation
        tacGen.emit(op, *result, *expr->result);
        code.push_back(tacGen.getCode().back());
    }
}

// ============================================================================
// POSTFIX EXPRESSIONS (x++ and x--)
// Handles: expr++, expr--
// Key: Returns OLD value before modification
// ============================================================================

PostfixExpression::PostfixExpression(TACOp operation, Expression *e)
    : op(operation), expr(e)
{
}

PostfixExpression::~PostfixExpression()
{
    delete expr;
}

string PostfixExpression::to_string() const
{
    string op_str = (op == TAC_POST_INC) ? "++" : "--";
    return expr->to_string() + op_str;
}

void PostfixExpression::generate_tac()
{
    // STEP 1: Generate code for the operand
    expr->generate_tac();
    code.insert(code.end(), expr->code.begin(), expr->code.end());

    // STEP 2: Type checking - must be a modifiable lvalue
    if (!expr->type || expr->type->is_error())
    {
        type = new Type(TYPE_ERROR);
        return;
    }

    if (!expr->type->is_numeric() && !expr->type->is_pointer())
    {
        SEM_ERROR(line_no,
                  "Postfix %s requires numeric or pointer type, got %s",
                  (op == TAC_POST_INC ? "++" : "--"),
                  expr->type->to_string().c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // STEP 3: Save old value to temporary (this is what makes it postfix!)
    TACOperand old_value = tacGen.newTemp(type);
    tacGen.emit(TAC_ASSIGN, old_value, *expr->result, TACOperand());
    code.push_back(tacGen.getCode().back());

    // STEP 4: Increment/decrement the variable
    if (expr->type->is_pointer())
    {
        // For pointers, scale by element size
        int elem_size = expr->type->get_element_size();
        TACOperand size_operand(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));
        TACOp add_or_sub = (op == TAC_POST_INC) ? TAC_ADD : TAC_SUB;

        TACOperand temp = tacGen.newTemp(type);
        tacGen.emit(add_or_sub, temp, *expr->result, size_operand);
        code.push_back(tacGen.getCode().back());

        tacGen.emit(TAC_ASSIGN, *expr->result, temp, TACOperand());
        code.push_back(tacGen.getCode().back());
    }
    else
    {
        // For numeric types, use simple increment/decrement
        TACOp actual_op = (op == TAC_POST_INC) ? TAC_PRE_INC : TAC_PRE_DEC;
        tacGen.emit(actual_op, *expr->result, *expr->result, TACOperand());
        code.push_back(tacGen.getCode().back());
    }

    // STEP 5: Result is the OLD value (saved temporary)
    result = new TACOperand(old_value);
    type = new Type(*expr->type);

    if (debug)
        printf("[AST] PostfixExpression: %s (result is old value)\n",
               (op == TAC_POST_INC ? "++" : "--"));
}

// ============================================================================
// ASSIGNMENT EXPRESSIONS
// Handles: variable = expression
// Type checking: Validates lvalue exists and types are compatible
// Warnings: Emits warnings for implicit type conversions
// ============================================================================

AssignmentExpression::AssignmentExpression(const string &var, Expression *rhs_expr)
    : lhs_name(var), rhs(rhs_expr), lhs_symbol(nullptr)
{
    // Look up LHS symbol during construction (while in correct scope)
    lhs_symbol = lookup_symbol(var);

    // If not found as local variable and we're inside a method, it might be a member
    if (!lhs_symbol && current_method_signature != nullptr)
    {
        // We'll handle this as member access in generate_tac
        // Don't report error here
    }
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
        SEM_ERROR(line_no, "Missing type information in assignment");
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // Error propagation from RHS
    if (rhs->type->is_error())
    {
        type = new Type(TYPE_ERROR);
        return;
    }

    // Check if we're inside a method and LHS might be a member access
    bool is_member_assignment = false;
    Type member_type;
    size_t member_offset = 0;

    if (!lhs_symbol && current_method_signature != nullptr)
    {
        // Look up the class this method belongs to
        ClassType *class_type = lookup_class_in_scope(current_method_signature->class_name);
        if (class_type)
        {
            // Check if this identifier is a class member
            for (size_t i = 0; i < class_type->members.size(); i++)
            {
                if (class_type->members[i].first == lhs_name)
                {
                    is_member_assignment = true;
                    member_type = *(class_type->members[i].second);
                    member_offset = class_type->member_offsets[class_type->members[i].first];
                    break;
                }
            }
        }
    }

    // Look up LHS variable in symbol table (use cached symbol from construction)
    Symbol *sym = lhs_symbol;
    if (!sym && !is_member_assignment)
    {
        SEM_ERROR(line_no, "Undefined variable '%s'", lhs_name.c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // For member assignment, use member_type; for normal assignment, use sym->type
    Type lhs_type = is_member_assignment ? member_type : sym->type;

    // Use unified type compatibility checking
    if (!is_type_compatible(lhs_type, *rhs->type, true))
    {
        SEM_ERROR(line_no, "Cannot assign %s to %s",
                  rhs->type->to_string().c_str(), lhs_type.to_string().c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }
    else if (should_warn_implicit_conversion(lhs_type, *rhs->type))
    {
        SEM_WARN(line_no, "Implicit conversion in assignment from %s to %s",
                 rhs->type->to_string().c_str(), lhs_type.to_string().c_str());
    }

    // Assignment type is the LHS type
    type = new Type(lhs_type);

    // ========================================================================
    // TAC Generation (only if types are valid)
    // ========================================================================

    // Copy code from RHS
    code = rhs->code;

    if (is_member_assignment)
    {
        // Generate TAC for member assignment: *(this + offset) = rhs
        // 1. Calculate member address: this + offset
        TACOperand this_ptr(TACOperand::OPERAND_IDENTIFIER, "param_0");
        TACOperand offset_operand(TACOperand::OPERAND_CONSTANT, std::to_string(member_offset));
        Type *ptr_type = new Type(TYPE_INT);
        ptr_type->pointer_level = 1;
        TACOperand addr_temp = tacGen.newTemp(ptr_type);
        tacGen.emit(TAC_ADD, addr_temp, this_ptr, offset_operand);
        code.push_back(tacGen.getCode().back());

        // 2. Store value at address: *addr = rhs
        tacGen.emit(TAC_DEREF_STORE, addr_temp, *rhs->result, TACOperand());
        code.push_back(tacGen.getCode().back());

        // Result is the address (for chained assignments)
        result = new TACOperand(addr_temp);

        if (debug)
        {
            cout << "[AST] Member assignment via 'this': " << lhs_name
                 << " (offset " << member_offset << ")" << endl;
        }
    }
    else
    {
        // Normal variable assignment
        // Create operand for LHS with mangled name: varname_scope
        string mangled_lhs = mangle_for_tac(lhs_name, sym);
        TACOperand lhs(TACOperand::OPERAND_IDENTIFIER, mangled_lhs);

        // Check if LHS is a reference - if so, we need to dereference it for assignment
        if (sym->type.is_reference)
        {
            // References: need to store through the pointer
            // Generate: *ref = rhs_value
            tacGen.emit(TAC_DEREF_STORE, lhs, *rhs->result, TACOperand());
            code.push_back(tacGen.getCode().back());

            // Result is the LHS reference itself (for chained assignments)
            result = new TACOperand(lhs);

            if (debug)
            {
                cout << "[AST] Assignment through reference: " << lhs_name << endl;
            }
            return;
        }

        // Handle boolean expressions with truelist/falselist from backpatching
        if (!rhs->truelist.empty() || !rhs->falselist.empty())
        {
            // Boolean expression in assignment context needs special handling
            // Backpatch truelist to assign 1 (true), falselist to assign 0 (false)

            TACOperand true_val(TACOperand::OPERAND_CONSTANT, "1");
            TACOperand false_val(TACOperand::OPERAND_CONSTANT, "0");

            // Backpatch truelist to location where we assign true
            int true_label = tacGen.nextinstr();
            backpatch(rhs->truelist, true_label);
            tacGen.emit(TAC_ASSIGN, lhs, true_val);
            code.push_back(tacGen.getCode().back());

            // Jump over false assignment
            int skip_false = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
            code.push_back(tacGen.getCode().back());

            // Backpatch falselist to location where we assign false
            int false_label = tacGen.nextinstr();
            backpatch(rhs->falselist, false_label);
            tacGen.emit(TAC_ASSIGN, lhs, false_val);
            code.push_back(tacGen.getCode().back());

            // Backpatch skip jump to after false assignment
            int after_label = tacGen.nextinstr();
            tacGen.getCode()[skip_false]->target_line = after_label;

            // Result is the LHS
            result = new TACOperand(lhs);
        }
        else
        {
            // Normal assignment without truelist/falselist
            tacGen.emit(TAC_ASSIGN, lhs, *rhs->result);
            code.push_back(tacGen.getCode().back());

            // Result is the LHS
            result = new TACOperand(lhs);
        }
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

PrimaryExpression *create_string_literal_expression(const string &str, int line, int col)
{
    PrimaryExpression *expr = new PrimaryExpression(str, true);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
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

PrimaryExpression *create_paren_expression(Expression *expr, int line, int col)
{
    PrimaryExpression *paren = new PrimaryExpression(expr);
    paren->line_no = line;
    paren->column_no = col;
    return paren;
}

PrimaryExpression *create_bool_constant_expression(bool value, int line, int col)
{
    // Create a boolean constant (true/false)
    PrimaryExpression *expr = new PrimaryExpression(value);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

PrimaryExpression *create_null_constant_expression(int line, int col)
{
    // Create a null pointer constant
    PrimaryExpression *expr = new PrimaryExpression(); // Uses the special null constructor
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

BinaryExpression *create_binary_expression(Expression *left, TACOp op, Expression *right, int line, int col)
{
    BinaryExpression *expr = new BinaryExpression(left, op, right);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

UnaryExpression *create_unary_expression(TACOp op, Expression *expr, int line, int col)
{
    UnaryExpression *unary = new UnaryExpression(op, expr);
    unary->line_no = line;
    unary->column_no = col;
    return unary;
}

AssignmentExpression *create_assignment_expression(const string &var, Expression *rhs, int line, int col)
{
    AssignmentExpression *expr = new AssignmentExpression(var, rhs);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

// ============================================================================
// GENERAL ASSIGNMENT EXPRESSIONS (*ptr = val, arr[i] = val, etc.)
// ============================================================================

GeneralAssignmentExpression::GeneralAssignmentExpression(Expression *lhs_expr, Expression *rhs_expr)
    : lhs(lhs_expr), rhs(rhs_expr)
{
}

GeneralAssignmentExpression::~GeneralAssignmentExpression()
{
    delete lhs;
    delete rhs;
}

string GeneralAssignmentExpression::to_string() const
{
    return lhs->to_string() + " = " + rhs->to_string();
}

void GeneralAssignmentExpression::generate_tac()
{
    // Generate code for both sides
    lhs->generate_tac();
    rhs->generate_tac();

    // Combine code
    code = lhs->code;
    code.insert(code.end(), rhs->code.begin(), rhs->code.end());

    // Type checking
    if (!lhs->type || !rhs->type)
    {
        SEM_ERROR(line_no, "Missing type information in assignment");
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    if (lhs->type->is_error() || rhs->type->is_error())
    {
        type = new Type(TYPE_ERROR);
        return;
    }

    // Check if LHS is a dereference operation (*ptr = val)
    UnaryExpression *unary_lhs = dynamic_cast<UnaryExpression *>(lhs);
    if (unary_lhs && unary_lhs->op == TAC_DEREF)
    {
        // Store through pointer: *ptr = value
        // The LHS already computed the pointer address in its result
        // We need to use TAC_DEREF_STORE

        // Use unified type compatibility checking
        if (!is_type_compatible(*lhs->type, *rhs->type, true))
        {
            SEM_ERROR(line_no, "Cannot assign %s to %s",
                      rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }
        else if (should_warn_implicit_conversion(*lhs->type, *rhs->type))
        {
            SEM_WARN(line_no, "Implicit conversion in assignment from %s to %s",
                     rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
        }

        // Generate: *ptr = rhs_value
        // We need the pointer (from the dereference's operand)
        TACOperand ptr_operand = *unary_lhs->expr->result; // The pointer itself
        tacGen.emit(TAC_DEREF_STORE, ptr_operand, *rhs->result);
        code.push_back(tacGen.getCode().back());

        // Result type is the LHS type
        type = new Type(*lhs->type);
        result = new TACOperand(*rhs->result); // Result is the assigned value
    }
    // Check if LHS is an array access (arr[i] = val)
    else if (ArrayAccessExpression *array_lhs = dynamic_cast<ArrayAccessExpression *>(lhs))
    {
        // Store to array element: arr[i] = value
        // The ArrayAccessExpression already calculated the address
        // We need to extract the address (before the final dereference)

        // Use unified type compatibility checking
        if (!is_type_compatible(*lhs->type, *rhs->type, true))
        {
            SEM_ERROR(line_no, "Cannot assign %s to %s",
                      rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }
        else if (should_warn_implicit_conversion(*lhs->type, *rhs->type))
        {
            SEM_WARN(line_no, "Implicit conversion in assignment from %s to %s",
                     rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
        }

        // The ArrayAccessExpression generated:
        // t1 = index * elem_size
        // t2 = array + t1
        // t3 = *t2
        // We need to use t2 (the address) for storing

        // Re-calculate the address (we need t2 from array access)
        // Get the last 3 instructions from array code
        if (array_lhs->code.size() >= 3)
        {
            // The second-to-last instruction has the address in its result
            TACInstruction *addr_instr = array_lhs->code[array_lhs->code.size() - 2];
            TACOperand addr = addr_instr->result;

            // Generate: *addr = rhs_value
            tacGen.emit(TAC_DEREF_STORE, addr, *rhs->result);
            code.push_back(tacGen.getCode().back());
        }

        // Result type is the LHS type
        type = new Type(*lhs->type);
        result = new TACOperand(*rhs->result); // Result is the assigned value
    }
    // Check if LHS is a member access (struct.member = val)
    else if (MemberAccessExpression *member_lhs = dynamic_cast<MemberAccessExpression *>(lhs))
    {
        // Store to struct member: struct.member = value
        // The MemberAccessExpression calculated:
        // t1 = &struct
        // t2 = t1 + offset
        // t3 = *t2 (the loaded value)
        // We need t2 (the address) for storing

        // Use unified type compatibility checking
        if (!is_type_compatible(*lhs->type, *rhs->type, true))
        {
            SEM_ERROR(line_no, "Cannot assign %s to %s",
                      rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }
        else if (should_warn_implicit_conversion(*lhs->type, *rhs->type))
        {
            SEM_WARN(line_no, "Implicit conversion in assignment from %s to %s",
                     rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
        }

        // Get the address from the second-to-last instruction
        if (member_lhs->code.size() >= 2)
        {
            TACInstruction *addr_instr = member_lhs->code[member_lhs->code.size() - 2];
            TACOperand addr = addr_instr->result;

            // Check if RHS result is valid
            if (!rhs->result)
            {
                SEM_ERROR(line_no, "RHS result is null in member assignment");
                semantic_error_count++;
            }
            else
            {
                // Generate: *addr = rhs_value
                tacGen.emit(TAC_DEREF_STORE, addr, *rhs->result);
                code.push_back(tacGen.getCode().back());
            }
        }
        else
        {
            SEM_ERROR(line_no, "Member LHS code size too small: %zu", member_lhs->code.size());
            semantic_error_count++;
        }

        // Result type is the LHS type
        type = new Type(*lhs->type);
        result = new TACOperand(*rhs->result);
    }
    // Check if LHS is a pointer member access (ptr->member = val)
    else if (MemberAccessPtrExpression *ptr_member_lhs = dynamic_cast<MemberAccessPtrExpression *>(lhs))
    {
        // Store to struct member via pointer: ptr->member = value
        // Similar to member access, we need the address before the final dereference

        // Use unified type compatibility checking
        if (!is_type_compatible(*lhs->type, *rhs->type, true))
        {
            SEM_ERROR(line_no, "Cannot assign %s to %s",
                      rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }
        else if (should_warn_implicit_conversion(*lhs->type, *rhs->type))
        {
            SEM_WARN(line_no, "Implicit conversion in assignment from %s to %s",
                     rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
        }

        // Get the address from the second-to-last instruction
        if (ptr_member_lhs->code.size() >= 2)
        {
            TACInstruction *addr_instr = ptr_member_lhs->code[ptr_member_lhs->code.size() - 2];
            TACOperand addr = addr_instr->result;

            // Generate: *addr = rhs_value
            tacGen.emit(TAC_DEREF_STORE, addr, *rhs->result);
            code.push_back(tacGen.getCode().back());
        }

        // Result type is the LHS type
        type = new Type(*lhs->type);
        result = new TACOperand(*rhs->result);
    }
    else
    {
        // Other complex lvalues not yet supported
        SEM_ERROR(line_no, "Complex assignment LHS type not yet supported");
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
    }
}

GeneralAssignmentExpression *create_general_assignment_expression(Expression *lhs, Expression *rhs, int line, int col)
{
    GeneralAssignmentExpression *expr = new GeneralAssignmentExpression(lhs, rhs);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

// ============================================================================
// ARRAY ACCESS EXPRESSIONS (arr[index])
// ============================================================================

ArrayAccessExpression::ArrayAccessExpression(Expression *arr, Expression *idx)
    : array(arr), index(idx)
{
}

ArrayAccessExpression::~ArrayAccessExpression()
{
    delete array;
    delete index;
}

string ArrayAccessExpression::to_string() const
{
    return array->to_string() + "[" + index->to_string() + "]";
}

void ArrayAccessExpression::generate_tac()
{
    // Generate code for array and index
    array->generate_tac();
    index->generate_tac();

    // Combine code
    code = array->code;
    code.insert(code.end(), index->code.begin(), index->code.end());

    // Type checking
    if (!array->type || !index->type)
    {
        SEM_ERROR(line_no, "Missing type information in array access");
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    if (array->type->is_error() || index->type->is_error())
    {
        type = new Type(TYPE_ERROR);
        return;
    }

    // Check that array is actually an array or pointer
    if (!array->type->is_array && !array->type->is_pointer())
    {
        // Prefer to report the line number of the array expression if available
        int err_line = (array->line_no != 0) ? array->line_no : line_no;

        // Try to include the identifier name if this is a primary identifier
        std::string name_hint;
        PrimaryExpression *prim = dynamic_cast<PrimaryExpression *>(array);
        if (prim && prim->prim_type == PrimaryExpression::PRIM_IDENTIFIER)
        {
            name_hint = std::string(" '") + prim->name + std::string("'");
        }

        SEM_ERROR(err_line, "Subscripted value is not an array or pointer%s (type: %s)",
                  name_hint.c_str(), array->type->to_string().c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // Check that index is an integer type
    if (!index->type->is_integer())
    {
        SEM_ERROR(line_no, "Array subscript must be an integer type");
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // Determine element size
    int elem_size = array->type->get_element_size();

    // Calculate result type (reduce array dimension or pointer level)
    type = new Type(*array->type);
    if (type->is_array)
    {
        type->array_dim--;
        if (!type->array_sizes.empty())
        {
            type->array_sizes.erase(type->array_sizes.begin());
        }
        if (type->array_dim == 0)
        {
            type->is_array = false;
        }
    }
    else if (type->pointer_level > 0)
    {
        type->pointer_level--;
        if (type->pointer_level == 0)
        {
            // No longer a pointer after dereferencing
        }
    }

    // TAC Generation: arr[i] = *(arr + i * elem_size)
    // BUT: If result is still an array, don't dereference (just return address)

    // Step 1: t1 = index * elem_size
    Type *int_type = new Type(TYPE_INT);
    TACOperand t1 = tacGen.newTemp(int_type);
    TACOperand elem_size_op(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));
    tacGen.emit(TAC_MUL, t1, *index->result, elem_size_op);
    code.push_back(tacGen.getCode().back());

    // Step 2: t2 = array + t1 (calculate address)
    // When adding to an array, it decays to a pointer (increase pointer_level)
    Type *ptr_type = new Type(*array->type);
    if (array->type->is_array)
    {
        // Array decays to pointer: increment pointer level
        ptr_type->pointer_level++;
        // Keep array info if multi-dimensional, otherwise it becomes a plain pointer
        if (ptr_type->array_dim > 1)
        {
            ptr_type->array_dim--;
            if (!ptr_type->array_sizes.empty())
            {
                ptr_type->array_sizes.erase(ptr_type->array_sizes.begin());
            }
        }
        else
        {
            ptr_type->is_array = false;
            ptr_type->array_dim = 0;
            ptr_type->array_sizes.clear();
        }
    }
    TACOperand t2 = tacGen.newTemp(ptr_type);
    tacGen.emit(TAC_ADD, t2, *array->result, t1);
    code.push_back(tacGen.getCode().back());

    // Step 3: Dereference to get the value, UNLESS result is an array
    // Arrays don't get dereferenced because they decay to pointers
    // But pointers DO get dereferenced to get the pointer value
    if (type->is_array)
    {
        // Result is an array - return address without dereferencing
        // (array will decay to pointer when used)
        result = new TACOperand(t2);
    }
    else
    {
        // Result is a scalar or pointer - dereference to get the value
        TACOperand t3 = tacGen.newTemp(type);
        result = new TACOperand(t3);
        tacGen.emit(TAC_DEREF, *result, t2);
        code.push_back(tacGen.getCode().back());
    }
}

ArrayAccessExpression *create_array_access_expression(Expression *array, Expression *index, int line, int col)
{
    ArrayAccessExpression *expr = new ArrayAccessExpression(array, index);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

// ============================================================================
// ARRAY INITIALIZER EXPRESSIONS
// ============================================================================

ArrayInitializerExpression::ArrayInitializerExpression(const std::vector<Expression *> &init_list)
    : initializers(init_list)
{
}

ArrayInitializerExpression::~ArrayInitializerExpression()
{
    for (Expression *expr : initializers)
    {
        delete expr;
    }
}

std::string ArrayInitializerExpression::to_string() const
{
    std::string result = "{";
    for (size_t i = 0; i < initializers.size(); i++)
    {
        if (i > 0)
            result += ", ";
        result += initializers[i]->to_string();
    }
    result += "}";
    return result;
}

void ArrayInitializerExpression::generate_tac()
{
    // For array initializers, we don't generate a single result operand
    // Instead, we will be used during variable declaration to generate
    // individual assignments for each array element

    // Generate TAC for all initializer expressions
    for (Expression *expr : initializers)
    {
        // For nested array initializers, don't generate TAC yet -
        // just mark them as having a placeholder type
        ArrayInitializerExpression *nested = dynamic_cast<ArrayInitializerExpression *>(expr);
        if (nested)
        {
            // This is a nested initializer - give it a placeholder type
            nested->type = new Type(TYPE_INT); // placeholder
        }
        else
        {
            expr->generate_tac();
            code.insert(code.end(), expr->code.begin(), expr->code.end());
        }
    }

    // The type will be determined by the declaration context
    // For now, we don't set a result operand since array initializers
    // are handled specially in variable declarations
}

ArrayInitializerExpression *create_array_initializer_expression(const std::vector<Expression *> &init_list, int line, int col)
{
    ArrayInitializerExpression *expr = new ArrayInitializerExpression(init_list);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

// ============================================================================
// FUNCTION CALL EXPRESSIONS
// ============================================================================

void CallExpression::generate_tac()
{
    code.clear();
    std::vector<Type> argTypes;
    argTypes.reserve(args.size());

    // Evaluate arguments left-to-right, collect code and types
    for (auto *e : args)
    {
        e->generate_tac();
        code.insert(code.end(), e->code.begin(), e->code.end());
        if (e->type)
            argTypes.push_back(*e->type);
        else
            argTypes.push_back(Type(TYPE_ERROR));
    }

    // Check if we're inside a method and this could be an implicit method call
    MethodSignature *method_match = nullptr;
    if (current_method_signature != nullptr)
    {
        // We're inside a method, check if func_name is a method of the current class
        std::string current_class_name = current_method_signature->class_name;
        // For method calls, use find_method_call_match to allow implicit conversions
        method_match = find_method_call_match(current_class_name, func_name, argTypes);

        if (method_match)
        {
            // Check method access permissions for implicit method call
            AccessLevel method_access = method_match->access_level;
            // For implicit calls, we're always inside the same class, so all access levels should be allowed
            // But let's still implement the check for consistency
            bool inside_same_class = true; // We know we're inside the same class for implicit calls

            if (method_access != ACCESS_PUBLIC && !inside_same_class)
            {
                const char *access_str = (method_access == ACCESS_PRIVATE) ? "private" : "protected";
                SEM_ERROR(line_no, "Cannot access %s method '%s' from current context",
                          access_str, func_name.c_str());
                semantic_error_count++;
                type = new Type(TYPE_ERROR);
                return;
            }

            if (debug)
            {
                printf("[CallExpression] Converting bare call '%s' to implicit method call in class '%s'\n",
                       func_name.c_str(), current_class_name.c_str());
            }

            // This is an implicit method call - emit like MethodCallExpression but with implicit 'this'

            // Create implicit 'this' parameter (address of current object)
            // The current object is always param_0 in any method
            TACOperand this_param(TACOperand::OPERAND_IDENTIFIER, "param_0");

            // Emit 'this' as first parameter
            tacGen.emit(TAC_PARAM, TACOperand(), this_param);
            code.push_back(tacGen.getCode().back());

            // Emit parameters for user arguments
            for (auto *e : args)
            {
                tacGen.emit(TAC_PARAM, TACOperand(), *e->result);
                code.push_back(tacGen.getCode().back());
            }

            // Emit call to mangled method name
            TACOperand methodOp(TACOperand::OPERAND_LABEL, method_match->mangled_name);
            TACOperand nParams(TACOperand::OPERAND_CONSTANT, std::to_string(args.size() + 1)); // +1 for 'this'

            // Set return type and emit call
            if (method_match->returnType.base_type != TYPE_VOID)
            {
                Type *ret_type = new Type(method_match->returnType);
                TACOperand temp = tacGen.newTemp(ret_type);
                result = new TACOperand(temp);
                tacGen.emit(TAC_CALL, *result, methodOp, nParams);
                code.push_back(tacGen.getCode().back());
                type = ret_type;
            }
            else
            {
                result = new TACOperand();
                tacGen.emit(TAC_CALL, TACOperand(), methodOp, nParams);
                code.push_back(tacGen.getCode().back());
                type = new Type(TYPE_VOID);
            }
            return; // Early return for method call
        }
    }

    // Not an implicit method call, try regular function call
    // For function calls, use find_function_call_match to allow implicit conversions
    int match = find_function_call_match(func_name, argTypes);
    if (match < 0)
    {
        // Check if this might be a method call made outside of a class context
        bool could_be_method = false;
        for (const auto &method_sig : method_signatures)
        {
            if (method_sig.method_name == func_name)
            {
                could_be_method = true;
                break;
            }
        }

        if (could_be_method && current_method_signature == nullptr)
        {
            SEM_ERROR(line_no, "Method '%s' can only be called from within a class method context, or use obj.%s() syntax",
                      func_name.c_str(), func_name.c_str());
        }
        else
        {
            SEM_ERROR(line_no, "No matching function '%s' for given argument types", func_name.c_str());
        }
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        // Still emit params and a call to keep TAC flow, with no result
    }

    // Emit params in reverse or forward? We'll use right-to-left is common, but here left-to-right
    for (auto *e : args)
    {
        tacGen.emit(TAC_PARAM, TACOperand(), *e->result);
        code.push_back(tacGen.getCode().back());
    }

    // Emit call - use mangled function name if we found a match
    std::string call_name = func_name; // default to original name
    if (match >= 0)
    {
        call_name = mangle_function_for_tac(func_name, function_signatures[match]);
    }
    TACOperand funcOp(TACOperand::OPERAND_LABEL, call_name);
    TACOperand nArgs(TACOperand::OPERAND_CONSTANT, std::to_string(args.size()));

    // If we have a signature, set return type
    Type retT = (match >= 0) ? function_signatures[match].returnType : Type(TYPE_ERROR);
    if (retT.base_type != TYPE_VOID)
    {
        Type *ret_type = new Type(retT);
        TACOperand temp = tacGen.newTemp(ret_type);
        result = new TACOperand(temp);
        tacGen.emit(TAC_CALL, *result, funcOp, nArgs);
        code.push_back(tacGen.getCode().back());
        type = ret_type;
    }
    else
    {
        result = new TACOperand();
        tacGen.emit(TAC_CALL, TACOperand(), funcOp, nArgs);
        code.push_back(tacGen.getCode().back());
        type = new Type(TYPE_VOID);
    }
}

CallExpression *create_call_expression(const std::string &name, const std::vector<Expression *> &args, int line, int col)
{
    CallExpression *expr = new CallExpression(name, args);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

// ============================================================================
// METHOD CALL EXPRESSIONS - object.method(args)
// ============================================================================

void MethodCallExpression::generate_tac()
{
    code.clear();

    // 1. Generate TAC for object expression to get its address/value
    object->generate_tac();
    code.insert(code.end(), object->code.begin(), object->code.end());

    // 2. Determine the class type of the object
    if (!object->type)
    {
        SEM_ERROR(line_no, "Object has no type information for method call '%s'", method_name.c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    if (object->type->is_error())
    {
        // Error already reported, propagate error type
        type = new Type(TYPE_ERROR);
        return;
    }

    if (!object->type->is_class)
    {
        SEM_ERROR(line_no, "Cannot call method '%s' on non-class type '%s'",
                  method_name.c_str(), object->type->to_string().c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    std::string class_name = object->type->class_name;

    // 3. Evaluate arguments and collect their types
    std::vector<Type> argTypes;
    argTypes.reserve(args.size());
    bool has_error_args = false;

    for (auto *e : args)
    {
        e->generate_tac();
        code.insert(code.end(), e->code.begin(), e->code.end());
        if (e->type)
        {
            argTypes.push_back(*e->type);
            if (e->type->is_error())
                has_error_args = true;
        }
        else
        {
            argTypes.push_back(Type(TYPE_ERROR));
            has_error_args = true;
        }
    }

    // If any argument has an error, propagate error (don't try to match)
    if (has_error_args)
    {
        type = new Type(TYPE_ERROR);
        return;
    }

    // 4. Find matching method using overload resolution
    // For method calls, use find_method_call_match to allow implicit conversions
    MethodSignature *method = find_method_call_match(class_name, method_name, argTypes);
    if (!method)
    {
        // Build argument type string for better error message
        std::string argTypeStr;
        for (size_t i = 0; i < argTypes.size(); ++i)
        {
            argTypeStr += argTypes[i].to_string();
            if (i < argTypes.size() - 1)
                argTypeStr += ", ";
        }
        if (argTypes.empty())
            argTypeStr = "void";

        SEM_ERROR(line_no, "No matching method '%s::%s(%s)'",
                  class_name.c_str(), method_name.c_str(), argTypeStr.c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // 4.5. Check method access permissions
    AccessLevel method_access = method->access_level;
    bool inside_same_class = (current_method_signature && current_method_signature->class_name == class_name);

    if (method_access != ACCESS_PUBLIC && !inside_same_class)
    {
        const char *access_str = (method_access == ACCESS_PRIVATE) ? "private" : "protected";
        SEM_ERROR(line_no, "Cannot access %s method '%s::%s'",
                  access_str, class_name.c_str(), method_name.c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // 5. Type checking passed - emit TAC

    // First parameter is address of object (this pointer)
    Type *ptr_type = new Type(*object->type);
    ptr_type->pointer_level++;
    TACOperand object_addr = tacGen.newTemp(ptr_type);
    tacGen.emit(TAC_ADDR_OF, object_addr, *object->result, TACOperand());
    code.push_back(tacGen.getCode().back());

    tacGen.emit(TAC_PARAM, TACOperand(), object_addr);
    code.push_back(tacGen.getCode().back());

    // Emit parameters for user arguments
    for (auto *e : args)
    {
        tacGen.emit(TAC_PARAM, TACOperand(), *e->result);
        code.push_back(tacGen.getCode().back());
    }

    // Emit call to mangled method name
    TACOperand methodOp(TACOperand::OPERAND_LABEL, method->mangled_name);
    TACOperand nParams(TACOperand::OPERAND_CONSTANT, std::to_string(args.size() + 1)); // +1 for 'this'

    // Set return type and emit call
    if (method->returnType.base_type != TYPE_VOID)
    {
        Type *ret_type = new Type(method->returnType);
        TACOperand temp = tacGen.newTemp(ret_type);
        result = new TACOperand(temp);
        tacGen.emit(TAC_CALL, *result, methodOp, nParams);
        code.push_back(tacGen.getCode().back());
        type = ret_type;
    }
    else
    {
        result = new TACOperand();
        tacGen.emit(TAC_CALL, TACOperand(), methodOp, nParams);
        code.push_back(tacGen.getCode().back());
        type = new Type(TYPE_VOID);
    }

    if (debug)
    {
        cout << "[AST] Method call: " << class_name << "::" << method_name
             << " -> " << method->mangled_name << endl;
    }
}

MethodCallExpression *create_method_call_expression(Expression *object, const char *method_name, const std::vector<Expression *> *args, int line, int col)
{
    std::vector<Expression *> arg_vec;
    if (args)
    {
        arg_vec = *args;
    }
    MethodCallExpression *expr = new MethodCallExpression(object, std::string(method_name), arg_vec);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

PostfixExpression *create_postfix_expression(TACOp op, Expression *expr, int line, int col)
{
    PostfixExpression *postfix = new PostfixExpression(op, expr);
    postfix->line_no = line;
    postfix->column_no = col;
    return postfix;
}

// ============================================================================
// MEMBER ACCESS EXPRESSIONS - struct.member and ptr->member
// ============================================================================

MemberAccessExpression::MemberAccessExpression(Expression *s_expr, const std::string &member)
    : struct_expr(s_expr), member_name(member)
{
}

MemberAccessExpression::~MemberAccessExpression()
{
    delete struct_expr;
}

std::string MemberAccessExpression::to_string() const
{
    return struct_expr->to_string() + "." + member_name;
}

void MemberAccessExpression::generate_tac()
{
    code.clear();

    // Generate code for the struct/class expression
    struct_expr->generate_tac();
    code.insert(code.end(), struct_expr->code.begin(), struct_expr->code.end());

    // Check if the expression is a struct or class type
    if (!struct_expr->type || ((!struct_expr->type->is_struct && !struct_expr->type->is_class) && struct_expr->type->pointer_level == 0))
    {
        SEM_ERROR(line_no, "Member access requires a struct or class type, got '%s'",
                  struct_expr->type ? struct_expr->type->to_string().c_str() : "unknown");
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand();
        return;
    }

    // Handle class member access
    if (struct_expr->type->is_class)
    {
        // Lookup class type - prefer using the direct pointer if available
        ClassType *ct = struct_expr->type->class_type_ptr;
        if (!ct)
        {
            // Fallback to scope-based lookup
            ct = lookup_class_in_scope(struct_expr->type->class_name);
        }
        if (!ct)
        {
            SEM_ERROR(line_no, "Class type '%s' not found",
                      struct_expr->type->class_name.c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand();
            return;
        }

        // Check if member exists
        if (!ct->has_member(member_name))
        {
            SEM_ERROR(line_no, "Class '%s' has no member named '%s'",
                      ct->name.c_str(), member_name.c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand();
            return;
        }

        // Check access permissions
        AccessLevel member_access = ct->get_member_access(member_name);
        bool inside_same_class = (current_class && current_class->name == ct->name);

        if (member_access != ACCESS_PUBLIC && !inside_same_class)
        {
            const char *access_str = (member_access == ACCESS_PRIVATE) ? "private" : "protected";
            SEM_ERROR(line_no, "Cannot access %s member '%s' of class '%s'",
                      access_str, member_name.c_str(), ct->name.c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand();
            return;
        }

        // Get member offset and type
        int offset = ct->get_member_offset(member_name);
        Type *member_type = ct->get_member_type(member_name);

        // Generate TAC: compute address of member
        TACOperand offset_op(TACOperand::OPERAND_CONSTANT, std::to_string(offset));
        Type *ptr_type = new Type(*member_type);
        ptr_type->pointer_level++;
        TACOperand addr_temp = tacGen.newTemp(ptr_type);

        // If struct_expr is a simple identifier, use address-of
        PrimaryExpression *prim = dynamic_cast<PrimaryExpression *>(struct_expr);
        if (prim && prim->prim_type == PrimaryExpression::PRIM_IDENTIFIER)
        {
            TACOperand struct_addr = tacGen.newTemp(ptr_type);
            tacGen.emit(TAC_ADDR_OF, struct_addr, *struct_expr->result);
            code.push_back(tacGen.getCode().back());

            tacGen.emit(TAC_ADD, addr_temp, struct_addr, offset_op);
            code.push_back(tacGen.getCode().back());
        }
        else
        {
            // For complex expressions, assume result is already an address
            tacGen.emit(TAC_ADD, addr_temp, *struct_expr->result, offset_op);
            code.push_back(tacGen.getCode().back());
        }

        // Load value from member address
        result = new TACOperand(tacGen.newTemp(member_type));
        tacGen.emit(TAC_DEREF, *result, addr_temp);
        code.push_back(tacGen.getCode().back());

        // Set type to member type
        type = new Type(*member_type);
        return;
    }

    // Handle struct member access (original code)
    // Lookup struct type - prefer using the direct pointer if available
    StructType *st = struct_expr->type->struct_type_ptr;
    if (!st)
    {
        // Fallback to scope-based lookup (for backwards compatibility)
        st = lookup_struct_in_scope(struct_expr->type->struct_name);
    }
    if (!st)
    {
        SEM_ERROR(line_no, "Struct type '%s' not found",
                  struct_expr->type->struct_name.c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand();
        return;
    }

    // Check if member exists
    if (!st->has_member(member_name))
    {
        SEM_ERROR(line_no, "Struct '%s' has no member named '%s'",
                  st->name.c_str(), member_name.c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand();
        return;
    }

    // Get member offset and type
    int offset = st->get_member_offset(member_name);
    Type *member_type = st->get_member_type(member_name);

    // Generate TAC: compute address of member
    // For struct variable: base_addr + offset
    TACOperand offset_op(TACOperand::OPERAND_CONSTANT, std::to_string(offset));
    Type *ptr_type = new Type(*member_type);
    ptr_type->pointer_level++;
    TACOperand addr_temp = tacGen.newTemp(ptr_type);

    // If struct_expr is a simple identifier, use address-of
    PrimaryExpression *prim = dynamic_cast<PrimaryExpression *>(struct_expr);
    if (prim && prim->prim_type == PrimaryExpression::PRIM_IDENTIFIER)
    {
        TACOperand struct_addr = tacGen.newTemp(ptr_type);
        tacGen.emit(TAC_ADDR_OF, struct_addr, *struct_expr->result);
        code.push_back(tacGen.getCode().back());

        tacGen.emit(TAC_ADD, addr_temp, struct_addr, offset_op);
        code.push_back(tacGen.getCode().back());
    }
    else
    {
        // For complex expressions, assume result is already an address
        tacGen.emit(TAC_ADD, addr_temp, *struct_expr->result, offset_op);
        code.push_back(tacGen.getCode().back());
    }

    // Load value from member address
    result = new TACOperand(tacGen.newTemp(member_type));
    tacGen.emit(TAC_DEREF, *result, addr_temp);
    code.push_back(tacGen.getCode().back());

    // Set type to member type
    type = new Type(*member_type);
}

MemberAccessPtrExpression::MemberAccessPtrExpression(Expression *p_expr, const std::string &member)
    : ptr_expr(p_expr), member_name(member)
{
}

MemberAccessPtrExpression::~MemberAccessPtrExpression()
{
    delete ptr_expr;
}

std::string MemberAccessPtrExpression::to_string() const
{
    return ptr_expr->to_string() + "->" + member_name;
}

void MemberAccessPtrExpression::generate_tac()
{
    code.clear();

    // Generate code for the pointer expression
    ptr_expr->generate_tac();
    code.insert(code.end(), ptr_expr->code.begin(), ptr_expr->code.end());

    // Check if the expression is a pointer to struct or class
    if (!ptr_expr->type || ((!ptr_expr->type->is_struct && !ptr_expr->type->is_class) || ptr_expr->type->pointer_level == 0))
    {
        SEM_ERROR(line_no, "Pointer member access requires a pointer to struct or class, got '%s'",
                  ptr_expr->type ? ptr_expr->type->to_string().c_str() : "unknown");
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand();
        return;
    }

    // Handle class pointer member access
    if (ptr_expr->type->is_class)
    {
        // Lookup class type - prefer using the direct pointer if available
        ClassType *ct = ptr_expr->type->class_type_ptr;
        if (!ct)
        {
            // Fallback to scope-based lookup
            ct = lookup_class_in_scope(ptr_expr->type->class_name);
        }
        if (!ct)
        {
            SEM_ERROR(line_no, "Class type '%s' not found",
                      ptr_expr->type->class_name.c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand();
            return;
        }

        // Check if member exists
        if (!ct->has_member(member_name))
        {
            SEM_ERROR(line_no, "Class '%s' has no member named '%s'",
                      ct->name.c_str(), member_name.c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand();
            return;
        }

        // Check access permissions
        AccessLevel member_access = ct->get_member_access(member_name);
        bool inside_same_class = (current_class && current_class->name == ct->name);

        if (member_access != ACCESS_PUBLIC && !inside_same_class)
        {
            const char *access_str = (member_access == ACCESS_PRIVATE) ? "private" : "protected";
            SEM_ERROR(line_no, "Cannot access %s member '%s' of class '%s'",
                      access_str, member_name.c_str(), ct->name.c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand();
            return;
        }

        // Get member offset and type
        int offset = ct->get_member_offset(member_name);
        Type *member_type = ct->get_member_type(member_name);

        // Generate TAC: dereference pointer, then compute member address
        Type *struct_type = new Type(*ptr_expr->type);
        struct_type->pointer_level--;
        TACOperand struct_addr = tacGen.newTemp(struct_type);
        tacGen.emit(TAC_DEREF, struct_addr, *ptr_expr->result);
        code.push_back(tacGen.getCode().back());

        // Add offset to get member address
        TACOperand offset_op(TACOperand::OPERAND_CONSTANT, std::to_string(offset));
        Type *ptr_type = new Type(*member_type);
        ptr_type->pointer_level++;
        TACOperand member_addr = tacGen.newTemp(ptr_type);
        tacGen.emit(TAC_ADD, member_addr, struct_addr, offset_op);
        code.push_back(tacGen.getCode().back());

        // Load value from member address
        result = new TACOperand(tacGen.newTemp(member_type));
        tacGen.emit(TAC_DEREF, *result, member_addr);
        code.push_back(tacGen.getCode().back());

        // Set type to member type
        type = new Type(*member_type);
        return;
    }

    // Handle struct pointer member access (original code)
    // Lookup struct type - prefer using the direct pointer if available
    StructType *st = ptr_expr->type->struct_type_ptr;
    if (!st)
    {
        // Fallback to scope-based lookup (for backwards compatibility)
        st = lookup_struct_in_scope(ptr_expr->type->struct_name);
    }
    if (!st)
    {
        SEM_ERROR(line_no, "Struct type '%s' not found",
                  ptr_expr->type->struct_name.c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand();
        return;
    }

    // Check if member exists
    if (!st->has_member(member_name))
    {
        SEM_ERROR(line_no, "Struct '%s' has no member named '%s'",
                  st->name.c_str(), member_name.c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand();
        return;
    }

    // Get member offset and type
    int offset = st->get_member_offset(member_name);
    Type *member_type = st->get_member_type(member_name);

    // Generate TAC: dereference pointer, then compute member address
    // ptr->member is equivalent to (*ptr).member
    Type *struct_type = new Type(*ptr_expr->type);
    struct_type->pointer_level--;
    TACOperand struct_addr = tacGen.newTemp(struct_type);
    tacGen.emit(TAC_DEREF, struct_addr, *ptr_expr->result);
    code.push_back(tacGen.getCode().back());

    // Add offset to get member address
    TACOperand offset_op(TACOperand::OPERAND_CONSTANT, std::to_string(offset));
    Type *ptr_type = new Type(*member_type);
    ptr_type->pointer_level++;
    TACOperand member_addr = tacGen.newTemp(ptr_type);
    tacGen.emit(TAC_ADD, member_addr, struct_addr, offset_op);
    code.push_back(tacGen.getCode().back());

    // Load value from member address
    result = new TACOperand(tacGen.newTemp(member_type));
    tacGen.emit(TAC_DEREF, *result, member_addr);
    code.push_back(tacGen.getCode().back());

    // Set type to member type
    type = new Type(*member_type);
}

// Helper functions for creating member access expressions
MemberAccessExpression *create_member_access_expression(Expression *struct_expr, const std::string &member, int line, int col)
{
    MemberAccessExpression *expr = new MemberAccessExpression(struct_expr, member);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}

MemberAccessPtrExpression *create_member_access_ptr_expression(Expression *ptr_expr, const std::string &member, int line, int col)
{
    MemberAccessPtrExpression *expr = new MemberAccessPtrExpression(ptr_expr, member);
    expr->line_no = line;
    expr->column_no = col;
    return expr;
}
