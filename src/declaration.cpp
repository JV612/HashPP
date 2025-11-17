#include "declaration.h"
#include "symbol_table.h"
#include "statement.h" // for register_constructed_local
#include "diagnostics.h"
#include <iostream>

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
            SEM_ERROR(line_no,
                      "Missing type information in initializer for '%s'",
                      var_name.c_str());
            semantic_error_count++;
            return;
        }

        // Error propagation from initializer
        if (initializer->type->is_error())
        {
            return;
        }

        // ========================================================================
        // Reference initialization - special handling
        // ========================================================================
        if (decl_type->is_reference)
        {
            // References must be bound to lvalues (addressable objects)
            // The initializer must be an lvalue (variable, array element, dereference, etc.)
            // For now, we check if the initializer has an address we can take

            // Type compatibility check (ignoring reference)
            Type ref_base = *decl_type;
            ref_base.is_reference = false;

            if (!is_type_compatible(ref_base, *initializer->type, true))
            {
                SEM_ERROR(line_no,
                          "Cannot bind reference '%s' of type %s to value of type %s",
                          var_name.c_str(), decl_type->to_string().c_str(),
                          initializer->type->to_string().c_str());
                semantic_error_count++;
                return;
            }

            // Generate TAC: reference is stored as a pointer to the referenced object
            code = initializer->code;

            Symbol *sym = inserted_symbol ? inserted_symbol : lookup_symbol(var_name);
            string mangled_name = sym ? mangle_for_tac(var_name, sym) : var_name;

            // For references, we store the ADDRESS of the initializer
            // References are implemented as constant pointers under the hood
            TACOperand lhs(TACOperand::OPERAND_IDENTIFIER, mangled_name);

            // Take address of initializer (if it's an identifier, take its address)
            if (initializer->result->type == TACOperand::OPERAND_IDENTIFIER)
            {
                // Store address of the identifier
                TACOperand addr_temp = tacGen.newTemp();
                tacGen.emit(TAC_ADDR_OF, addr_temp, *initializer->result);
                code.push_back(tacGen.getCode().back());

                tacGen.emit(TAC_ASSIGN, lhs, addr_temp);
                code.push_back(tacGen.getCode().back());
            }
            else if (initializer->result->type == TACOperand::OPERAND_TEMP)
            {
                // For temporary values, we can't create references
                // This is a semantic error, but we'll allow it for now with a warning
                SEM_WARN(line_no,
                         "Binding reference '%s' to temporary value (may result in undefined behavior)",
                         var_name.c_str());

                tacGen.emit(TAC_ASSIGN, lhs, *initializer->result);
                code.push_back(tacGen.getCode().back());
            }
            else
            {
                // For other operand types, just assign directly
                tacGen.emit(TAC_ASSIGN, lhs, *initializer->result);
                code.push_back(tacGen.getCode().back());
            }

            return;
        }

        // Use unified type compatibility checking (for non-references)
        if (!is_type_compatible(*decl_type, *initializer->type, true))
        {
            SEM_ERROR(line_no,
                      "Cannot initialize '%s' of type %s with value of type %s",
                      var_name.c_str(), decl_type->to_string().c_str(),
                      initializer->type->to_string().c_str());
            semantic_error_count++;
            return;
        }
        else if (should_warn_implicit_conversion(*decl_type, *initializer->type))
        {
            report_semantic_warning(line_no,
                                    "Implicit conversion in initialization of '%s' from %s to %s",
                                    var_name.c_str(), initializer->type->to_string().c_str(),
                                    decl_type->to_string().c_str());
        }

        // ========================================================================
        // TAC Generation (only if types are valid)
        // ========================================================================

        code = initializer->code;

        // Look up the symbol to get its scope for mangling
        // Prefer the symbol captured at insert time to ensure correct scope id
        Symbol *sym = inserted_symbol ? inserted_symbol : lookup_symbol(var_name);
        string mangled_name = sym ? mangle_for_tac(var_name, sym) : var_name;

        // Handle boolean expressions with truelist/falselist from backpatching
        if (!initializer->truelist.empty() || !initializer->falselist.empty())
        {
            // Boolean expression in assignment context needs special handling
            // Backpatch truelist to assign 1 (true), falselist to assign 0 (false)

            TACOperand lhs(TACOperand::OPERAND_IDENTIFIER, mangled_name);
            TACOperand true_val(TACOperand::OPERAND_CONSTANT, "1");
            TACOperand false_val(TACOperand::OPERAND_CONSTANT, "0");

            // Backpatch truelist to location where we assign true
            int true_label = tacGen.nextinstr();
            backpatch(initializer->truelist, true_label);
            tacGen.emit(TAC_ASSIGN, lhs, true_val);
            code.push_back(tacGen.getCode().back());

            // Jump over false assignment
            int skip_false = tacGen.emit(TAC_GOTO, TACOperand(), TACOperand());
            code.push_back(tacGen.getCode().back());

            // Backpatch falselist to location where we assign false
            int false_label = tacGen.nextinstr();
            backpatch(initializer->falselist, false_label);
            tacGen.emit(TAC_ASSIGN, lhs, false_val);
            code.push_back(tacGen.getCode().back());

            // Backpatch skip jump to after false assignment
            int after_label = tacGen.nextinstr();
            tacGen.getCode()[skip_false]->target_line = after_label;
        }
        else
        {
            // Normal assignment without truelist/falselist
            TACOperand lhs(TACOperand::OPERAND_IDENTIFIER, mangled_name);
            tacGen.emit(TAC_ASSIGN, lhs, *initializer->result);
            code.push_back(tacGen.getCode().back());
        }
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
        SEM_ERROR(line_no,
                  "Too many initializers for array '%s' (expected %d, got %zu)",
                  var_name.c_str(), array_size, array_init->initializers.size());
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
            SEM_ERROR(line_no,
                      "Missing type in array initializer element %zu",
                      i);
            semantic_error_count++;
            continue;
        }

        // Use unified type compatibility checking
        if (!is_type_compatible(*decl_type, *init_expr->type, true))
        {
            SEM_ERROR(line_no,
                      "Incompatible type in array initializer element %zu",
                      i);
            semantic_error_count++;
            continue;
        }

        // Calculate array element address: base + i * sizeof(element)
        TACOperand index_op(TACOperand::OPERAND_CONSTANT, std::to_string(i));
        TACOperand element_size_op(TACOperand::OPERAND_CONSTANT, std::to_string(decl_type->get_size()));

        // offset = i * sizeof(element)
        Type *int_type = new Type(TYPE_INT);
        TACOperand offset_temp = tacGen.newTemp(int_type);
        tacGen.emit(TAC_MUL, offset_temp, index_op, element_size_op);
        code.push_back(tacGen.getCode().back());

        // addr = base + offset
        TACOperand base_op(TACOperand::OPERAND_IDENTIFIER, base_name);
        Type *ptr_type = new Type(*decl_type);
        ptr_type->pointer_level++;
        TACOperand addr_temp = tacGen.newTemp(ptr_type);
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
                                                 Expression *init, int line, int col)
{
    VariableDeclaration *decl = new VariableDeclaration(type, name, init);
    decl->line_no = line;
    decl->column_no = col;
    return decl;
}
