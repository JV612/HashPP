#include "declaration.h"
#include "symbol_table.h"
#include "statement.h" // for register_constructed_local
#include <iostream>
#include <cstdio>

using namespace std;

// ============================================================================
// Declaration Implementation
// ============================================================================
// This file contains implementations for declaration node types:
//   1. Declaration Base Class
//   2. Variable Declaration
//
// Each declaration inserts into symbol table and generates initialization code
// ============================================================================

// ============================================================================
// Declaration Base Class
// ============================================================================

Declaration::~Declaration()
{
    delete decl_type;
    // Note: code vector contains pointers managed by TACGenerator
}

// ============================================================================
// VARIABLE DECLARATIONS
// ============================================================================

VariableDeclaration::VariableDeclaration(Type *t, const string &name, Expression *init, bool static_var)
    : var_name(name), initializer(init), is_static(static_var)
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

void VariableDeclaration::insert_symbol()
{
    if (debug)
    {
        cout << "[AST] Variable declaration: " << var_name
             << " (type: " << decl_type->to_string()
             << ", static: " << (is_static ? "yes" : "no") << ")" << endl;
    }

    // Check if this is a static variable
    if (is_static)
    {
        // Static variables - global if at global scope, function-level if inside function
        if (!current_scope())
        {
            // Global static variable - store in global symbol table
            inserted_symbol = insert_global_symbol(var_name, *decl_type);
            if (debug)
            {
                cout << "[AST] Inserted global static variable: " << var_name << endl;
            }
        }
        else
        {
            // Function-local static variable - store in function's static table
            SymbolTable *st = current_scope();
            inserted_symbol = insert_function_static_symbol(var_name, *decl_type, st->Scopelevel);
            if (debug)
            {
                cout << "[AST] Inserted function static variable: " << var_name << endl;
            }
        }
    }
    else
    {
        // Non-static variables
        if (!current_scope())
        {
            // Global non-static variable - store in global symbol table
            inserted_symbol = insert_global_symbol(var_name, *decl_type);
            if (debug)
            {
                cout << "[AST] Inserted global variable: " << var_name << endl;
            }
        }
        else
        {
            // Local variable - store in current local scope
            SymbolTable *st = current_scope();
            inserted_symbol = st->insert(var_name, *decl_type);
            if (debug)
            {
                cout << "[AST] Inserted local variable: " << var_name << " in scope " << st->Scopelevel << endl;
            }
        }
    }
}

void VariableDeclaration::generate_tac()
{
    // Symbol table insertion is done separately via insert_symbol()
    // Here we only generate TAC for initializer if present

    // If there's an initializer, generate code for it
    if (initializer)
    {
        // Special case: constructor-style initialization for class objects.
        // Detect pattern: initializer is a method call whose method name == class name
        // In that case, we only need to emit the call (which uses 'this' = &var),
        // and we do NOT perform assignment/type checking against decl_type.
        if (decl_type && decl_type->is_class)
        {
            if (MethodCallExpression *mce = dynamic_cast<MethodCallExpression *>(initializer))
            {
                if (mce->method_name == decl_type->class_name)
                {
                    // Emit TAC for the constructor call and return
                    initializer->generate_tac();
                    code = initializer->code;
                    // Register this local as constructed for destructor emission
                    Symbol *sym = inserted_symbol ? inserted_symbol : lookup_symbol(var_name);
                    if (sym && sym->type.is_class && sym->type.pointer_level == 0)
                    {
                        register_constructed_local(sym);
                    }
                    return;
                }
            }
        }

        // Check if this is an array initializer
        ArrayInitializerExpression *array_init = dynamic_cast<ArrayInitializerExpression *>(initializer);
        if (array_init && decl_type->is_array)
        {
            // Handle array initialization specially
            handle_array_initialization(array_init);
            return;
        }

        initializer->generate_tac();

        // ========================================================================
        // Phase 1: Type Checking for Variable Initialization
        // ========================================================================

        // Check if initializer has a type
        if (!initializer->type)
        {
            fprintf(stderr, "[Type Error] Line %d: Missing type information in initializer for '%s'\n",
                    line_no, var_name.c_str());
            semantic_error_count++;
            return;
        }

        // Error propagation from initializer
        if (initializer->type->is_error())
        {
            return;
        }

        // Check type compatibility with full pointer/array checking
        bool compatible = false;

        // First check: exact type match (including pointer levels and array status)
        if (decl_type->base_type == initializer->type->base_type &&
            decl_type->pointer_level == initializer->type->pointer_level &&
            decl_type->is_array == initializer->type->is_array)
        {
            // For struct types, also check struct names match
            if (decl_type->is_struct && initializer->type->is_struct)
            {
                if (decl_type->struct_name == initializer->type->struct_name && decl_type->is_union == initializer->type->is_union)
                {
                    compatible = true;
                }
            }
            else if (!decl_type->is_struct && !initializer->type->is_struct)
            {
                compatible = true;
            }
            // else: one is struct, one is not -> incompatible
        }
        // Array decay check: array T[N] can initialize pointer T*
        else if (decl_type->pointer_level == 1 && !decl_type->is_array &&
                 initializer->type->is_array &&
                 decl_type->base_type == initializer->type->base_type)
        {
            // Array decays to pointer in initialization: char[N] -> char*
            compatible = true;
        }
        // Null constant check: null can be assigned to any pointer type
        else if (decl_type->pointer_level > 0 &&
                 initializer->type->base_type == TYPE_VOID && initializer->type->pointer_level == 1)
        {
            // Allow null constants to be assigned to any pointer type
            compatible = true;
        }
        // Second check: numeric type conversions (only for non-pointer types)
        else if (decl_type->pointer_level == 0 && initializer->type->pointer_level == 0 &&
                 !decl_type->is_array && !initializer->type->is_array &&
                 decl_type->is_numeric() && initializer->type->is_numeric())
        {
            // Allow implicit numeric conversions but warn
            compatible = true;
            fprintf(stderr, "[Type Warning] Line %d: Implicit conversion in initialization of '%s' from %s to %s\n",
                    line_no, var_name.c_str(), initializer->type->to_string().c_str(), decl_type->to_string().c_str());
        }

        // If not compatible, it's an error
        if (!compatible)
        {
            fprintf(stderr, "[Type Error] Line %d: Cannot initialize '%s' of type %s with value of type %s\n",
                    line_no, var_name.c_str(), decl_type->to_string().c_str(), initializer->type->to_string().c_str());
            semantic_error_count++;
            return;
        }

        // ========================================================================
        // TAC Generation (only if types are valid)
        // ========================================================================

        code = initializer->code;

        // Look up the symbol to get its scope for mangling
        // Prefer the symbol captured at insert time to ensure correct scope id
        Symbol *sym = inserted_symbol ? inserted_symbol : lookup_symbol(var_name);
        string mangled_name = sym ? mangle_for_tac(var_name, sym) : var_name;

        // Generate assignment with mangled name: varname_scope
        TACOperand lhs(TACOperand::OPERAND_IDENTIFIER, mangled_name);
        tacGen.emit(TAC_ASSIGN, lhs, *initializer->result);
        code.push_back(tacGen.getCode().back());
    }

    // Even without an explicit initializer, register non-pointer class locals
    // so that destructors are emitted on returns and scope exits.
    // Do not register globals or statics.
    if (!initializer && decl_type && decl_type->is_class && decl_type->pointer_level == 0 && !is_static)
    {
        Symbol *sym = inserted_symbol ? inserted_symbol : lookup_symbol(var_name);
        if (sym && sym->scope > 0) // local scopes have positive scope ids
        {
            register_constructed_local(sym);
            if (debug)
            {
                printf("[RAII] Registered (no-init) class local '%s' for destructor\n", var_name.c_str());
            }
        }
    }
}

void VariableDeclaration::handle_array_initialization(ArrayInitializerExpression *array_init)
{
    if (debug)
    {
        cout << "[AST] Handling array initialization for " << var_name
             << " with " << array_init->initializers.size() << " elements" << endl;
    }

    // Get the array size from the type
    int array_size = 0;
    if (!decl_type->array_sizes.empty())
    {
        array_size = decl_type->array_sizes[0];
    }

    // Check that we don't have too many initializers
    if (array_init->initializers.size() > static_cast<size_t>(array_size))
    {
        fprintf(stderr, "[Error] Line %d: Too many initializers for array '%s' (expected %d, got %zu)\n",
                line_no, var_name.c_str(), array_size, array_init->initializers.size());
        semantic_error_count++;
        return;
    }

    // Generate TAC for all initializer expressions first
    for (Expression *expr : array_init->initializers)
    {
        expr->generate_tac();
        code.insert(code.end(), expr->code.begin(), expr->code.end());
    }

    // Get the symbol for the array
    Symbol *sym = inserted_symbol ? inserted_symbol : lookup_symbol(var_name);
    string base_name = sym ? mangle_for_tac(var_name, sym) : var_name;

    // Generate individual assignments for each element: arr[0] = val1, arr[1] = val2, etc.
    for (size_t i = 0; i < array_init->initializers.size(); i++)
    {
        Expression *init_expr = array_init->initializers[i];

        // Type check the initializer
        if (!init_expr->type)
        {
            fprintf(stderr, "[Type Error] Line %d: Missing type in array initializer element %zu\n",
                    line_no, i);
            semantic_error_count++;
            continue;
        }

        // Check type compatibility
        bool elem_compatible = false;

        if (decl_type->base_type == init_expr->type->base_type &&
            decl_type->pointer_level == init_expr->type->pointer_level)
        {
            // For struct types, also check struct names match
            if (decl_type->is_struct && init_expr->type->is_struct)
            {
                if (decl_type->struct_name == init_expr->type->struct_name && decl_type->is_union == init_expr->type->is_union)
                {
                    elem_compatible = true;
                }
            }
            else if (!decl_type->is_struct && !init_expr->type->is_struct)
            {
                elem_compatible = true;
            }
        }
        // Allow some numeric conversions
        else if (decl_type->pointer_level == 0 && init_expr->type->pointer_level == 0 &&
                 decl_type->is_numeric() && init_expr->type->is_numeric())
        {
            elem_compatible = true;
        }

        if (!elem_compatible)
        {
            fprintf(stderr, "[Type Error] Line %d: Incompatible type in array initializer element %zu\n",
                    line_no, i);
            semantic_error_count++;
            continue;
        }

        // Calculate array element address: base + i * sizeof(element)
        TACOperand index_op(TACOperand::OPERAND_CONSTANT, std::to_string(i));
        TACOperand element_size_op(TACOperand::OPERAND_CONSTANT, std::to_string(decl_type->get_size()));

        // offset = i * sizeof(element)
        TACOperand offset_temp = tacGen.newTemp();
        tacGen.emit(TAC_MUL, offset_temp, index_op, element_size_op);
        code.push_back(tacGen.getCode().back());

        // addr = base + offset
        TACOperand base_op(TACOperand::OPERAND_IDENTIFIER, base_name);
        TACOperand addr_temp = tacGen.newTemp();
        tacGen.emit(TAC_ADD, addr_temp, base_op, offset_temp);
        code.push_back(tacGen.getCode().back());

        // *addr = value
        tacGen.emit(TAC_DEREF_STORE, addr_temp, *init_expr->result);
        code.push_back(tacGen.getCode().back());
    }
}

// ============================================================================
// Helper Functions - Declaration Creation
// ============================================================================

VariableDeclaration *create_variable_declaration(Type *type, const string &name,
                                                 Expression *init)
{
    return new VariableDeclaration(type, name, init);
}
