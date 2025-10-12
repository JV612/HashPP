#pragma once

#include "ast.h"
#include "diagnostics.h"
#include "symbol_table.h"
#include "type.h"

class SemanticAnalyzer {
public:
    SemanticAnalyzer();

    void analyze(const AstNodePtr &root, SymbolTable &symbols);

    const DiagnosticReporter& diagnostics() const { return reporter; }
    DiagnosticReporter& diagnostics() { return reporter; }

private:
    DiagnosticReporter reporter;
    SymbolTable *current_symbols = nullptr;
    std::vector<AstNodePtr> saved_params_for_body; // Temporary storage for function parameters

    void analyze_translation_unit(const AstNodePtr &node, SymbolTable &symbols);
    void analyze_declaration(const AstNodePtr &node, SymbolTable &symbols);
    void analyze_variable_decl(const AstNodePtr &node, SymbolTable &symbols);
    void analyze_function_decl(const AstNodePtr &node, SymbolTable &symbols);
    void analyze_struct_decl(const AstNodePtr &node, SymbolTable &symbols);
    void analyze_union_decl(const AstNodePtr &node, SymbolTable &symbols);
    void analyze_enum_decl(const AstNodePtr &node, SymbolTable &symbols);
    void analyze_statement(const AstNodePtr &node);
    void analyze_compound_stmt(const AstNodePtr &node);
    void analyze_expression_stmt(const AstNodePtr &node);
    
    TypePtr infer_expression_type(const AstNodePtr &expr);
    TypePtr infer_binary_expr(const AstNodePtr &expr);
    TypePtr infer_unary_expr(const AstNodePtr &expr);
    TypePtr infer_assignment_expr(const AstNodePtr &expr);
    TypePtr infer_call_expr(const AstNodePtr &expr);
    TypePtr infer_identifier_expr(const AstNodePtr &expr);
    TypePtr infer_literal_expr(const AstNodePtr &expr);
    TypePtr infer_subscript_expr(const AstNodePtr &expr);
    
    // Function overload resolution helpers
    std::string generate_call_signature(const std::string& func_name, const std::vector<TypePtr>& arg_types);
    SymbolPtr find_best_overload(const std::string& func_name, const std::vector<TypePtr>& arg_types);
    TypePtr infer_member_access_expr(const AstNodePtr &expr);

    TypePtr resolve_type_from_specifier(const AstNodePtr &type_node, SymbolTable &symbols);
    TypePtr resolve_builtin_type(const TypeSpecifierNodeData &spec);
    
    bool types_compatible(const TypePtr &lhs, const TypePtr &rhs);
    TypePtr perform_usual_arithmetic_conversions(const TypePtr &lhs, const TypePtr &rhs);
    bool is_assignment_compatible(const TypePtr &lhs, const TypePtr &rhs);
};
