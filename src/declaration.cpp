#include "declaration.h"
#include "symbol_table.h"
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

void VariableDeclaration::insert_symbol()
{
    cout << "[AST] Variable declaration: " << var_name
         << " (type: " << decl_type->to_string() << ")" << endl;

    // Insert into symbol table
    symbolTable.insert(var_name, *decl_type);
}

void VariableDeclaration::generate_tac()
{
    // Symbol table insertion is done separately via insert_symbol()
    // Here we only generate TAC for initializer if present

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

        // Look up the symbol to get its scope for mangling
        Symbol *sym = symbolTable.lookup(var_name);
        string mangled_name = var_name;
        if (sym)
        {
            mangled_name = var_name + "_" + std::to_string(sym->scope);
        }

        // Generate assignment with mangled name: varname_scope
        TACOperand lhs(TACOperand::OPERAND_IDENTIFIER, mangled_name);
        tacGen.emit(TAC_ASSIGN, lhs, *initializer->result);
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
