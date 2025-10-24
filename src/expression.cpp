#include "expression.h"
#include "symbol_table.h"
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

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

        // Otherwise, normal identifier lookup
        // Use cached symbol from construction time (correct scope)
        Symbol *sym = symbol_ref;
        if (!sym)
        {
            cerr << "Error: Undefined variable '" << name << "'" << endl;
            result = new TACOperand(TACOperand::OPERAND_IDENTIFIER, name);
            // Propagate semantic error and mark type as error
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
        }
        else
        {
            // Use mangled name with scope: name_scope
            string mangled_name = mangle_for_tac(name, sym);
            result = new TACOperand(TACOperand::OPERAND_IDENTIFIER, mangled_name);
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

    case PRIM_STRING_LITERAL:
    {
        // String literals create a string operand
        result = new TACOperand(TACOperand::OPERAND_STRING, string_value);
        // String literals have type "pointer to char" (char*)
        type = new Type(TYPE_CHAR, 1); // base_type = char, pointer_level = 1
        cout << "[AST] String literal: " << string_value << endl;
        break;
    }

    case PRIM_BOOL_CONSTANT:
    {
        // Boolean constants (true/false)
        result = new TACOperand(TACOperand::OPERAND_CONSTANT, bool_value ? "1" : "0");
        type = new Type(TYPE_BOOL);
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

        // M: marker - current position before E2
        int M = tacGen.nextinstr();
        backpatch(left->truelist, M);

        right->generate_tac();
        code.insert(code.end(), right->code.begin(), right->code.end());

        // Merge lists
        falselist = merge(left->falselist, right->falselist);
        truelist = right->truelist;

        // For assignment contexts, create a result operand and compute the logical AND
        TACOperand temp = tacGen.newTemp();
        result = new TACOperand(temp);
        type = new Type(TYPE_BOOL);

        // Generate TAC for logical AND: temp = left_result && right_result
        // This will emit the actual logical AND operation
        tacGen.emit(TAC_LOGICAL_AND, *result, *left->result, *right->result);
        code.push_back(tacGen.getCode().back());

        // Type checking - logical operators accept numeric types or pointers (C semantics)
        if (!left->type || !right->type || left->type->is_error() || right->type->is_error())
        {
            type = new Type(TYPE_ERROR);
        }
        else if (!(left->type->is_numeric() || left->type->is_pointer() || left->type->base_type == TYPE_BOOL) ||
                 !(right->type->is_numeric() || right->type->is_pointer() || right->type->base_type == TYPE_BOOL))
        {
            fprintf(stderr, "[Type Error] Line %d: Logical operator '&&' requires numeric, pointer, or bool operands, got %s and %s\n",
                    line_no, left->type->to_string().c_str(), right->type->to_string().c_str());
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

        // M: marker - current position before E2
        int M = tacGen.nextinstr();
        backpatch(left->falselist, M);

        right->generate_tac();
        code.insert(code.end(), right->code.begin(), right->code.end());

        // Merge lists
        truelist = merge(left->truelist, right->truelist);
        falselist = right->falselist;

        // For assignment contexts, create a result operand and compute the logical OR
        TACOperand temp = tacGen.newTemp();
        result = new TACOperand(temp);
        type = new Type(TYPE_BOOL);

        // Generate TAC for logical OR: temp = left_result || right_result
        // This will emit the actual logical OR operation
        tacGen.emit(TAC_LOGICAL_OR, *result, *left->result, *right->result);
        code.push_back(tacGen.getCode().back());

        // Type checking - logical operators accept numeric types or pointers (C semantics)
        if (!left->type || !right->type || left->type->is_error() || right->type->is_error())
        {
            type = new Type(TYPE_ERROR);
        }
        else if (!(left->type->is_numeric() || left->type->is_pointer() || left->type->base_type == TYPE_BOOL) ||
                 !(right->type->is_numeric() || right->type->is_pointer() || right->type->base_type == TYPE_BOOL))
        {
            fprintf(stderr, "[Type Error] Line %d: Logical operator '||' requires numeric, pointer, or bool operands, got %s and %s\n",
                    line_no, left->type->to_string().c_str(), right->type->to_string().c_str());
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
        fprintf(stderr, "[Type Error] Line %d: Missing type information in binary expression\n",
                line_no);
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
    // POINTER ARITHMETIC HANDLING
    // Arrays decay to pointers in arithmetic contexts
    // ========================================================================
    bool left_is_pointer = left->type->is_pointer() || left->type->is_array;
    bool right_is_pointer = right->type->is_pointer() || right->type->is_array;
    bool left_is_integer = left->type->is_integer();
    bool right_is_integer = right->type->is_integer();

    // Handle pointer + integer (or array + integer)
    if (op == TAC_ADD && left_is_pointer && right_is_integer)
    {
        handle_pointer_plus_integer(left, right);
        return;
    }

    // Handle integer + pointer (or integer + array)
    if (op == TAC_ADD && left_is_integer && right_is_pointer)
    {
        handle_pointer_plus_integer(right, left);
        return;
    }

    // Handle pointer - integer (or array - integer)
    if (op == TAC_SUB && left_is_pointer && right_is_integer)
    {
        handle_pointer_minus_integer(left, right);
        return;
    }

    // Handle pointer - pointer (or array - pointer, or array - array)
    if (op == TAC_SUB && left_is_pointer && right_is_pointer)
    {
        handle_pointer_minus_pointer(left, right);
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
            fprintf(stderr, "[Type Error] Line %d: Modulo operator '%%' requires integer operands, got %s and %s\n",
                    line_no, left->type->to_string().c_str(), right->type->to_string().c_str());
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
            fprintf(stderr, "[Type Error] Line %d: Bitwise operator '%s' requires integer operands, got %s and %s\n",
                    line_no, op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
            return;
        }
    }
    // Ordering operators (<, >, <=, >=) require numeric operands only
    else if (op == TAC_LT || op == TAC_GT || op == TAC_LE || op == TAC_GE)
    {
        if (!left->type->is_numeric() || !right->type->is_numeric())
        {
            fprintf(stderr, "[Type Error] Line %d: Ordering operator '%s' requires numeric operands, got %s and %s\n",
                    line_no, op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
            return;
        }
    }
    // Equality operators (==, !=) allow both numeric and pointer comparisons
    else if (op == TAC_EQ || op == TAC_NE)
    {
        bool valid = false;

        // Case 1: Both operands are numeric
        if (left->type->is_numeric() && right->type->is_numeric())
        {
            valid = true;
        }
        // Case 2: Both operands are pointers of compatible types
        else if (left->type->is_pointer() && right->type->is_pointer())
        {
            // For now, allow any pointer-to-pointer comparison
            // TODO: Add stricter type compatibility checking
            valid = true;
        }
        // Case 3: One is a null constant and the other is a pointer
        // TODO: Implement null constant detection

        if (!valid)
        {
            fprintf(stderr, "[Type Error] Line %d: Equality operator '%s' requires compatible types, got %s and %s\n",
                    line_no, op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            result = new TACOperand(TACOperand::OPERAND_CONSTANT, "0"); // Dummy result to prevent segfault
            return;
        }
    }
    // Logical operators accept numeric types, pointers, or bool (C semantics: any scalar type is "truthy")
    else if (op == TAC_LOGICAL_AND || op == TAC_LOGICAL_OR)
    {
        if (!(left->type->is_numeric() || left->type->is_pointer() || left->type->base_type == TYPE_BOOL) ||
            !(right->type->is_numeric() || right->type->is_pointer() || right->type->base_type == TYPE_BOOL))
        {
            fprintf(stderr, "[Type Error] Line %d: Logical operator '%s' requires numeric, pointer, or bool operands, got %s and %s\n",
                    line_no, op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
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
            fprintf(stderr, "[Type Error] Line %d: Operator '%s' requires numeric operands, got %s and %s\n",
                    line_no, op_name, left->type->to_string().c_str(), right->type->to_string().c_str());
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
        // Backpatching with truelist/falselist is only needed in control flow contexts

        // Create new temporary for the comparison result
        TACOperand temp = tacGen.newTemp();
        result = new TACOperand(temp);

        // Emit the comparison operation (simple assignment)
        tacGen.emit(op, *result, *left->result, *right->result);
        code.push_back(tacGen.getCode().back());

        // Note: truelist/falselist remain empty for simple assignment contexts
        // They will be populated by control flow structures if needed
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

// ============================================================================
// BinaryExpression: Pointer Arithmetic Helper Methods
// ============================================================================

void BinaryExpression::handle_pointer_plus_integer(Expression *ptr_expr, Expression *int_expr)
{
    // Get element size for scaling
    int elem_size = ptr_expr->type->get_element_size();

    // Combine code from both operands
    code.insert(code.end(), ptr_expr->code.begin(), ptr_expr->code.end());
    code.insert(code.end(), int_expr->code.begin(), int_expr->code.end());

    // For arrays, we need to get the base address first (array decays to pointer)
    TACOperand ptr_operand;
    if (ptr_expr->type->is_array)
    {
        // Array needs address-of to decay to pointer
        ptr_operand = tacGen.newTemp();
        tacGen.emit(TAC_ADDR_OF, ptr_operand, *ptr_expr->result);
        code.push_back(tacGen.getCode().back());
    }
    else
    {
        ptr_operand = *ptr_expr->result;
    }

    // Step 1: Scale the integer by element size
    // _t1 = integer * elem_size
    TACOperand scale_temp = tacGen.newTemp();
    TACOperand size_operand(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));
    tacGen.emit(TAC_MUL, scale_temp, *int_expr->result, size_operand);
    code.push_back(tacGen.getCode().back());

    // Step 2: Add scaled offset to pointer
    // result = pointer + _t1
    TACOperand result_temp = tacGen.newTemp();
    tacGen.emit(TAC_ADD, result_temp, ptr_operand, scale_temp);
    code.push_back(tacGen.getCode().back());

    // Result type: decay array to pointer
    result = new TACOperand(result_temp);
    if (ptr_expr->type->is_array)
    {
        // Array decay removes the outermost dimension
        // int arr[5] + 1 -> int* (pointer to int)
        // int arr[3][4] + 1 -> int (*)[4] (pointer to array of 4 ints)
        // int arr[2][3][4] + 1 -> int (*)[3][4] (pointer to array of [3][4] ints)
        type = new Type(*ptr_expr->type);
        type->pointer_level = 1;

        if (ptr_expr->type->array_dim > 1)
        {
            // Multi-dimensional: remove first dimension, keep rest as array
            type->is_array = true;
            type->array_dim = ptr_expr->type->array_dim - 1;
            // Remove the first size from array_sizes
            type->array_sizes = std::vector<int>(
                ptr_expr->type->array_sizes.begin() + 1,
                ptr_expr->type->array_sizes.end());
        }
        else
        {
            // Single dimension: becomes simple pointer
            type->is_array = false;
            type->array_dim = 0;
            type->array_sizes.clear();
        }
    }
    else
    {
        // Pointer type stays the same
        type = new Type(*ptr_expr->type);
    }

    if (debug)
        printf("[AST] Pointer arithmetic: pointer + integer (scaled by %d)\n", elem_size);
}

void BinaryExpression::handle_pointer_minus_integer(Expression *ptr_expr, Expression *int_expr)
{
    // Get element size for scaling
    int elem_size = ptr_expr->type->get_element_size();

    // Combine code from both operands
    code.insert(code.end(), ptr_expr->code.begin(), ptr_expr->code.end());
    code.insert(code.end(), int_expr->code.begin(), int_expr->code.end());

    // For arrays, we need to get the base address first (array decays to pointer)
    TACOperand ptr_operand;
    if (ptr_expr->type->is_array)
    {
        // Array needs address-of to decay to pointer
        ptr_operand = tacGen.newTemp();
        tacGen.emit(TAC_ADDR_OF, ptr_operand, *ptr_expr->result);
        code.push_back(tacGen.getCode().back());
    }
    else
    {
        ptr_operand = *ptr_expr->result;
    }

    // Step 1: Scale the integer by element size
    // _t1 = integer * elem_size
    TACOperand scale_temp = tacGen.newTemp();
    TACOperand size_operand(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));
    tacGen.emit(TAC_MUL, scale_temp, *int_expr->result, size_operand);
    code.push_back(tacGen.getCode().back());

    // Step 2: Subtract scaled offset from pointer
    // result = pointer - _t1
    TACOperand result_temp = tacGen.newTemp();
    tacGen.emit(TAC_SUB, result_temp, ptr_operand, scale_temp);
    code.push_back(tacGen.getCode().back());

    // Result type: decay array to pointer
    result = new TACOperand(result_temp);
    if (ptr_expr->type->is_array)
    {
        // Array decay removes the outermost dimension
        // int arr[5] - 1 -> int* (pointer to int)
        // int arr[3][4] - 1 -> int (*)[4] (pointer to array of 4 ints)
        type = new Type(*ptr_expr->type);
        type->pointer_level = 1;

        if (ptr_expr->type->array_dim > 1)
        {
            // Multi-dimensional: remove first dimension, keep rest as array
            type->is_array = true;
            type->array_dim = ptr_expr->type->array_dim - 1;
            // Remove the first size from array_sizes
            type->array_sizes = std::vector<int>(
                ptr_expr->type->array_sizes.begin() + 1,
                ptr_expr->type->array_sizes.end());
        }
        else
        {
            // Single dimension: becomes simple pointer
            type->is_array = false;
            type->array_dim = 0;
            type->array_sizes.clear();
        }
    }
    else
    {
        // Pointer type stays the same
        type = new Type(*ptr_expr->type);
    }

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
        fprintf(stderr, "[Type Error] Line %d: Incompatible pointer types in subtraction: %s - %s\n",
                line_no, left_ptr->type->to_string().c_str(), right_ptr->type->to_string().c_str());
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
        left_operand = tacGen.newTemp();
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
        right_operand = tacGen.newTemp();
        tacGen.emit(TAC_ADDR_OF, right_operand, *right_ptr->result);
        code.push_back(tacGen.getCode().back());
    }
    else
    {
        right_operand = *right_ptr->result;
    }

    // Step 1: Byte-level subtraction
    // _t1 = left_ptr - right_ptr
    TACOperand diff_temp = tacGen.newTemp();
    tacGen.emit(TAC_SUB, diff_temp, left_operand, right_operand);
    code.push_back(tacGen.getCode().back());

    // Step 2: Unscale by element size to get number of elements
    // result = _t1 / elem_size
    TACOperand result_temp = tacGen.newTemp();
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
        fprintf(stderr, "[Type Error] Line %d: Missing type information in unary expression\n",
                line_no);
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
            TACOperand t1 = tacGen.newTemp();
            TACOperand elem_size_op(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));
            tacGen.emit(TAC_MUL, t1, *array_expr->index->result, elem_size_op);
            code.push_back(tacGen.getCode().back());

            // result = array + t1 (this is the address we want!)
            TACOperand temp = tacGen.newTemp();
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
            fprintf(stderr, "[Type Error] Line %d: Cannot take address of non-lvalue (not a variable)\n",
                    line_no);
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }

        // Result type: pointer to operand's type
        type = new Type(*expr->type);
        type->pointer_level++;

        // TAC Generation
        code = expr->code;
        TACOperand temp = tacGen.newTemp();
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
            fprintf(stderr, "[Type Error] Line %d: Cannot dereference non-pointer type %s\n",
                    line_no, expr->type->to_string().c_str());
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
        TACOperand temp = tacGen.newTemp();
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
            fprintf(stderr, "[Type Error] Line %d: Unary '%s' requires numeric operand, got %s\n",
                    line_no, op_name, expr->type->to_string().c_str());
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
            fprintf(stderr, "[Type Error] Line %d: Bitwise NOT '~' requires integer operand, got %s\n",
                    line_no, expr->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }
    }
    // Logical NOT requires numeric, pointer, or bool operand (C semantics: any scalar type is "truthy")
    else if (op == TAC_LOGICAL_NOT)
    {
        if (!expr->type->is_numeric() && !expr->type->is_pointer() && expr->type->base_type != TYPE_BOOL)
        {
            fprintf(stderr, "[Type Error] Line %d: Logical NOT '!' requires numeric, pointer, or bool operand, got %s\n",
                    line_no, expr->type->to_string().c_str());
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
            fprintf(stderr, "[Type Error] Line %d: Prefix '%s' requires numeric or pointer operand, got %s\n",
                    line_no, op_name, expr->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }

        // Check that operand is an lvalue (modifiable variable)
        // expr->result should be an OPERAND_IDENTIFIER, not a constant or temp
        if (!expr->result || expr->result->type != TACOperand::OPERAND_IDENTIFIER)
        {
            fprintf(stderr, "[Type Error] Line %d: Prefix '%s' requires an lvalue (modifiable variable)\n",
                    line_no, op_name);
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
        TACOperand temp = tacGen.newTemp();
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
        fprintf(stderr, "[Type Error] Line %d: Postfix %s requires numeric or pointer type, got %s\n",
                line_no,
                (op == TAC_POST_INC ? "++" : "--"),
                expr->type->to_string().c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // STEP 3: Save old value to temporary (this is what makes it postfix!)
    TACOperand old_value = tacGen.newTemp();
    tacGen.emit(TAC_ASSIGN, old_value, *expr->result, TACOperand());
    code.push_back(tacGen.getCode().back());

    // STEP 4: Increment/decrement the variable
    if (expr->type->is_pointer())
    {
        // For pointers, scale by element size
        int elem_size = expr->type->get_element_size();
        TACOperand size_operand(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));
        TACOp add_or_sub = (op == TAC_POST_INC) ? TAC_ADD : TAC_SUB;

        TACOperand temp = tacGen.newTemp();
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

    // Look up LHS variable in symbol table (use cached symbol from construction)
    Symbol *sym = lhs_symbol;
    if (!sym)
    {
        fprintf(stderr, "[Type Error] Line %d: Undefined variable '%s'\n",
                line_no, lhs_name.c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // Check type compatibility with full pointer/array checking
    bool compatible = false;

    // First check: exact type match (including pointer levels and array status)
    if (sym->type.base_type == rhs->type->base_type &&
        sym->type.pointer_level == rhs->type->pointer_level &&
        sym->type.is_array == rhs->type->is_array)
    {
        // For struct types, also check that struct names match
        if (sym->type.is_struct && rhs->type->is_struct)
        {
            if (sym->type.struct_name == rhs->type->struct_name)
            {
                compatible = true;
            }
        }
        else if (!sym->type.is_struct && !rhs->type->is_struct)
        {
            // Non-struct types match
            compatible = true;
        }
        // else: one is struct, one is not -> incompatible
    }
    // Second check: array-to-pointer decay
    // An array T[] can be assigned to a pointer T*
    else if (rhs->type->is_array &&
             sym->type.pointer_level == 1 &&
             !sym->type.is_array &&
             sym->type.base_type == rhs->type->base_type)
    {
        compatible = true;
        // Array automatically decays to pointer to first element
    }
    // Null constant check: null can be assigned to any pointer type
    else if (sym->type.pointer_level > 0 &&
             rhs->type->base_type == TYPE_VOID && rhs->type->pointer_level == 1)
    {
        // Allow null constants to be assigned to any pointer type
        compatible = true;
    }
    // Third check: numeric type conversions (only for non-pointer types)
    else if (sym->type.pointer_level == 0 && rhs->type->pointer_level == 0 &&
             !sym->type.is_array && !rhs->type->is_array &&
             sym->type.is_numeric() && rhs->type->is_numeric())
    {
        // Allow implicit numeric conversions but warn
        compatible = true;
        fprintf(stderr, "[Type Warning] Line %d: Implicit conversion in assignment from %s to %s\n",
                line_no, rhs->type->to_string().c_str(), sym->type.to_string().c_str());
    }

    // If not compatible, it's an error
    if (!compatible)
    {
        fprintf(stderr, "[Type Error] Line %d: Cannot assign %s to %s\n",
                line_no, rhs->type->to_string().c_str(), sym->type.to_string().c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // Assignment type is the LHS type
    type = new Type(sym->type);

    // ========================================================================
    // TAC Generation (only if types are valid)
    // ========================================================================

    // Copy code from RHS
    code = rhs->code;

    // Create operand for LHS with mangled name: varname_scope
    string mangled_lhs = mangle_for_tac(lhs_name, sym);
    TACOperand lhs(TACOperand::OPERAND_IDENTIFIER, mangled_lhs);

    // Emit assignment
    tacGen.emit(TAC_ASSIGN, lhs, *rhs->result);
    code.push_back(tacGen.getCode().back());

    // Result is the LHS
    result = new TACOperand(lhs);
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

PrimaryExpression *create_string_literal_expression(const string &str)
{
    return new PrimaryExpression(str, true);
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

PrimaryExpression *create_bool_constant_expression(bool value)
{
    // Create a boolean constant (true/false)
    return new PrimaryExpression(value);
}

PrimaryExpression *create_null_constant_expression()
{
    // Create a null pointer constant
    return new PrimaryExpression(); // Uses the special null constructor
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
        fprintf(stderr, "[Type Error] Line %d: Missing type information in assignment\n", line_no);
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

        // Type compatibility check
        bool compatible = false;

        // Check for exact type match
        if (lhs->type->base_type == rhs->type->base_type &&
            lhs->type->pointer_level == rhs->type->pointer_level &&
            lhs->type->is_array == rhs->type->is_array)
        {
            // For struct types, also check struct names match
            if (lhs->type->is_struct && rhs->type->is_struct)
            {
                if (lhs->type->struct_name == rhs->type->struct_name)
                {
                    compatible = true;
                }
            }
            else if (!lhs->type->is_struct && !rhs->type->is_struct)
            {
                compatible = true;
            }
        }
        // Array-to-pointer decay
        else if (rhs->type->is_array &&
                 lhs->type->pointer_level == 1 &&
                 !lhs->type->is_array &&
                 lhs->type->base_type == rhs->type->base_type)
        {
            compatible = true;
        }
        // Null constant to pointer
        else if (lhs->type->pointer_level > 0 &&
                 rhs->type->base_type == TYPE_VOID && rhs->type->pointer_level == 1)
        {
            compatible = true;
        }
        // Numeric conversions (only for non-pointer types)
        else if (lhs->type->pointer_level == 0 && rhs->type->pointer_level == 0 &&
                 !lhs->type->is_array && !rhs->type->is_array &&
                 lhs->type->is_numeric() && rhs->type->is_numeric())
        {
            compatible = true;
            fprintf(stderr, "[Type Warning] Line %d: Implicit conversion in assignment from %s to %s\n",
                    line_no, rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
        }

        if (!compatible)
        {
            fprintf(stderr, "[Type Error] Line %d: Cannot assign %s to %s\n",
                    line_no, rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
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

        // Type compatibility check
        bool compatible = false;

        // Check for exact type match
        if (lhs->type->base_type == rhs->type->base_type &&
            lhs->type->pointer_level == rhs->type->pointer_level &&
            lhs->type->is_array == rhs->type->is_array)
        {
            // For struct types, also check struct names match
            if (lhs->type->is_struct && rhs->type->is_struct)
            {
                if (lhs->type->struct_name == rhs->type->struct_name)
                {
                    compatible = true;
                }
            }
            else if (!lhs->type->is_struct && !rhs->type->is_struct)
            {
                compatible = true;
            }
        }
        // Array-to-pointer decay
        else if (rhs->type->is_array &&
                 lhs->type->pointer_level == 1 &&
                 !lhs->type->is_array &&
                 lhs->type->base_type == rhs->type->base_type)
        {
            compatible = true;
        }
        // Null constant to pointer
        else if (lhs->type->pointer_level > 0 &&
                 rhs->type->base_type == TYPE_VOID && rhs->type->pointer_level == 1)
        {
            compatible = true;
        }
        // Numeric conversions (only for non-pointer types)
        else if (lhs->type->pointer_level == 0 && rhs->type->pointer_level == 0 &&
                 !lhs->type->is_array && !rhs->type->is_array &&
                 lhs->type->is_numeric() && rhs->type->is_numeric())
        {
            compatible = true;
            fprintf(stderr, "[Type Warning] Line %d: Implicit conversion in assignment from %s to %s\n",
                    line_no, rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
        }

        if (!compatible)
        {
            fprintf(stderr, "[Type Error] Line %d: Cannot assign %s to %s\n",
                    line_no, rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
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

        // Type compatibility check
        bool compatible = false;

        // Check for exact type match
        if (lhs->type->base_type == rhs->type->base_type &&
            lhs->type->pointer_level == rhs->type->pointer_level &&
            lhs->type->is_array == rhs->type->is_array)
        {
            // For struct types, also check struct names match
            if (lhs->type->is_struct && rhs->type->is_struct)
            {
                if (lhs->type->struct_name == rhs->type->struct_name)
                {
                    compatible = true;
                }
            }
            else if (!lhs->type->is_struct && !rhs->type->is_struct)
            {
                compatible = true;
            }
        }
        // Array-to-pointer decay
        else if (rhs->type->is_array &&
                 lhs->type->pointer_level == 1 &&
                 !lhs->type->is_array &&
                 lhs->type->base_type == rhs->type->base_type)
        {
            compatible = true;
        }
        // Null constant to pointer
        else if (lhs->type->pointer_level > 0 &&
                 rhs->type->base_type == TYPE_VOID && rhs->type->pointer_level == 1)
        {
            compatible = true;
        }
        // Numeric conversions (only for non-pointer types)
        else if (lhs->type->pointer_level == 0 && rhs->type->pointer_level == 0 &&
                 !lhs->type->is_array && !rhs->type->is_array &&
                 lhs->type->is_numeric() && rhs->type->is_numeric())
        {
            compatible = true;
            fprintf(stderr, "[Type Warning] Line %d: Implicit conversion in assignment from %s to %s\n",
                    line_no, rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
        }

        if (!compatible)
        {
            fprintf(stderr, "[Type Error] Line %d: Cannot assign %s to %s\n",
                    line_no, rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
        }

        // Get the address from the second-to-last instruction
        if (member_lhs->code.size() >= 2)
        {
            TACInstruction *addr_instr = member_lhs->code[member_lhs->code.size() - 2];
            TACOperand addr = addr_instr->result;

            // Check if RHS result is valid
            if (!rhs->result)
            {
                fprintf(stderr, "[Error] Line %d: RHS result is null in member assignment\n", line_no);
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
            fprintf(stderr, "[Error] Line %d: Member LHS code size too small: %zu\n", line_no, member_lhs->code.size());
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

        // Type compatibility check
        bool compatible = false;

        // Check for exact type match
        if (lhs->type->base_type == rhs->type->base_type &&
            lhs->type->pointer_level == rhs->type->pointer_level &&
            lhs->type->is_array == rhs->type->is_array)
        {
            // For struct types, also check struct names match
            if (lhs->type->is_struct && rhs->type->is_struct)
            {
                if (lhs->type->struct_name == rhs->type->struct_name)
                {
                    compatible = true;
                }
            }
            else if (!lhs->type->is_struct && !rhs->type->is_struct)
            {
                compatible = true;
            }
        }
        // Array-to-pointer decay
        else if (rhs->type->is_array &&
                 lhs->type->pointer_level == 1 &&
                 !lhs->type->is_array &&
                 lhs->type->base_type == rhs->type->base_type)
        {
            compatible = true;
        }
        // Null constant to pointer
        else if (lhs->type->pointer_level > 0 &&
                 rhs->type->base_type == TYPE_VOID && rhs->type->pointer_level == 1)
        {
            compatible = true;
        }
        // Numeric conversions (only for non-pointer types)
        else if (lhs->type->pointer_level == 0 && rhs->type->pointer_level == 0 &&
                 !lhs->type->is_array && !rhs->type->is_array &&
                 lhs->type->is_numeric() && rhs->type->is_numeric())
        {
            compatible = true;
            fprintf(stderr, "[Type Warning] Line %d: Implicit conversion in assignment from %s to %s\n",
                    line_no, rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
        }

        if (!compatible)
        {
            fprintf(stderr, "[Type Error] Line %d: Cannot assign %s to %s\n",
                    line_no, rhs->type->to_string().c_str(), lhs->type->to_string().c_str());
            semantic_error_count++;
            type = new Type(TYPE_ERROR);
            return;
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
        fprintf(stderr, "[Error] Line %d: Complex assignment LHS type not yet supported\n", line_no);
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
    }
}

GeneralAssignmentExpression *create_general_assignment_expression(Expression *lhs, Expression *rhs)
{
    return new GeneralAssignmentExpression(lhs, rhs);
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
        fprintf(stderr, "[Type Error] Line %d: Missing type information in array access\n", line_no);
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

        fprintf(stderr, "[Type Error] Line %d: Subscripted value is not an array or pointer%s (type: %s)\n",
                err_line, name_hint.c_str(), array->type->to_string().c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        return;
    }

    // Check that index is an integer type
    if (!index->type->is_integer())
    {
        fprintf(stderr, "[Type Error] Line %d: Array subscript must be an integer type\n", line_no);
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
    TACOperand t1 = tacGen.newTemp();
    TACOperand elem_size_op(TACOperand::OPERAND_CONSTANT, std::to_string(elem_size));
    tacGen.emit(TAC_MUL, t1, *index->result, elem_size_op);
    code.push_back(tacGen.getCode().back());

    // Step 2: t2 = array + t1 (calculate address)
    TACOperand t2 = tacGen.newTemp();
    tacGen.emit(TAC_ADD, t2, *array->result, t1);
    code.push_back(tacGen.getCode().back());

    // Step 3: Dereference ONLY if result is a scalar (not an array)
    if (type->is_array || type->pointer_level > 0)
    {
        // Result is still an array or pointer - just return the address
        result = new TACOperand(t2);
    }
    else
    {
        // Result is a scalar - dereference to get the value
        TACOperand t3 = tacGen.newTemp();
        result = new TACOperand(t3);
        tacGen.emit(TAC_DEREF, *result, t2);
        code.push_back(tacGen.getCode().back());
    }
}

ArrayAccessExpression *create_array_access_expression(Expression *array, Expression *index)
{
    return new ArrayAccessExpression(array, index);
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

ArrayInitializerExpression *create_array_initializer_expression(const std::vector<Expression *> &init_list)
{
    return new ArrayInitializerExpression(init_list);
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

    int match = find_function_match(func_name, argTypes);
    if (match < 0)
    {
        fprintf(stderr, "[Type Error] Line %d: No matching function '%s' for given argument types\n", line_no, func_name.c_str());
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
        TACOperand temp = tacGen.newTemp();
        result = new TACOperand(temp);
        tacGen.emit(TAC_CALL, *result, funcOp, nArgs);
        code.push_back(tacGen.getCode().back());
        type = new Type(retT);
    }
    else
    {
        result = new TACOperand();
        tacGen.emit(TAC_CALL, TACOperand(), funcOp, nArgs);
        code.push_back(tacGen.getCode().back());
        type = new Type(TYPE_VOID);
    }
}

CallExpression *create_call_expression(const std::string &name, const std::vector<Expression *> &args)
{
    return new CallExpression(name, args);
}

PostfixExpression *create_postfix_expression(TACOp op, Expression *expr)
{
    return new PostfixExpression(op, expr);
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

    // Generate code for the struct expression
    struct_expr->generate_tac();
    code.insert(code.end(), struct_expr->code.begin(), struct_expr->code.end());

    // Check if the expression is a struct type
    if (!struct_expr->type || (!struct_expr->type->is_struct && struct_expr->type->pointer_level == 0))
    {
        fprintf(stderr, "[Type Error] Line %d: Member access requires a struct type, got '%s'\n",
                line_no, struct_expr->type ? struct_expr->type->to_string().c_str() : "unknown");
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand();
        return;
    }

    // Lookup struct type - prefer using the direct pointer if available
    StructType *st = struct_expr->type->struct_type_ptr;
    if (!st)
    {
        // Fallback to scope-based lookup (for backwards compatibility)
        st = lookup_struct_in_scope(struct_expr->type->struct_name);
    }
    if (!st)
    {
        fprintf(stderr, "[Type Error] Line %d: Struct type '%s' not found\n",
                line_no, struct_expr->type->struct_name.c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand();
        return;
    }

    // Check if member exists
    if (!st->has_member(member_name))
    {
        fprintf(stderr, "[Type Error] Line %d: Struct '%s' has no member named '%s'\n",
                line_no, st->name.c_str(), member_name.c_str());
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
    TACOperand addr_temp = tacGen.newTemp();

    // If struct_expr is a simple identifier, use address-of
    PrimaryExpression *prim = dynamic_cast<PrimaryExpression *>(struct_expr);
    if (prim && prim->prim_type == PrimaryExpression::PRIM_IDENTIFIER)
    {
        TACOperand struct_addr = tacGen.newTemp();
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
    result = new TACOperand(tacGen.newTemp());
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

    // Check if the expression is a pointer to struct
    if (!ptr_expr->type || !ptr_expr->type->is_struct || ptr_expr->type->pointer_level == 0)
    {
        fprintf(stderr, "[Type Error] Line %d: Pointer member access requires a pointer to struct, got '%s'\n",
                line_no, ptr_expr->type ? ptr_expr->type->to_string().c_str() : "unknown");
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand();
        return;
    }

    // Lookup struct type - prefer using the direct pointer if available
    StructType *st = ptr_expr->type->struct_type_ptr;
    if (!st)
    {
        // Fallback to scope-based lookup (for backwards compatibility)
        st = lookup_struct_in_scope(ptr_expr->type->struct_name);
    }
    if (!st)
    {
        fprintf(stderr, "[Type Error] Line %d: Struct type '%s' not found\n",
                line_no, ptr_expr->type->struct_name.c_str());
        semantic_error_count++;
        type = new Type(TYPE_ERROR);
        result = new TACOperand();
        return;
    }

    // Check if member exists
    if (!st->has_member(member_name))
    {
        fprintf(stderr, "[Type Error] Line %d: Struct '%s' has no member named '%s'\n",
                line_no, st->name.c_str(), member_name.c_str());
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
    TACOperand struct_addr = tacGen.newTemp();
    tacGen.emit(TAC_DEREF, struct_addr, *ptr_expr->result);
    code.push_back(tacGen.getCode().back());

    // Add offset to get member address
    TACOperand offset_op(TACOperand::OPERAND_CONSTANT, std::to_string(offset));
    TACOperand member_addr = tacGen.newTemp();
    tacGen.emit(TAC_ADD, member_addr, struct_addr, offset_op);
    code.push_back(tacGen.getCode().back());

    // Load value from member address
    result = new TACOperand(tacGen.newTemp());
    tacGen.emit(TAC_DEREF, *result, member_addr);
    code.push_back(tacGen.getCode().back());

    // Set type to member type
    type = new Type(*member_type);
}

// Helper functions for creating member access expressions
MemberAccessExpression *create_member_access_expression(Expression *struct_expr, const std::string &member)
{
    return new MemberAccessExpression(struct_expr, member);
}

MemberAccessPtrExpression *create_member_access_ptr_expression(Expression *ptr_expr, const std::string &member)
{
    return new MemberAccessPtrExpression(ptr_expr, member);
}
