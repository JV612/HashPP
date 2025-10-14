#pragma once

#include "ast.h"
#include "diagnostics.h"
#include "symbol_table.h" 
#include "type.h"
#include "ir_generator.h"
#include <set>

class SemanticAnalyzer {
public:
    SemanticAnalyzer();

    void analyze(const AstNodePtr &root, SymbolTable &symbols);

    const DiagnosticReporter& diagnostics() const { return reporter; }
    DiagnosticReporter& diagnostics() { return reporter; }
    
    // Get the generated IR
    const IRGenerator& get_ir_generator() const { return ir_gen; }

private:
    DiagnosticReporter reporter;
    IRGenerator ir_gen;
    SymbolTable *current_symbols = nullptr;
    std::vector<AstNodePtr> saved_params_for_body; // Temporary storage for function parameters
    int max_scope_for_lookup = -1; // Restrict symbol lookup to this scope level or lower (-1 = no restriction)
    
    // Context tracking for control flow validation
    bool in_loop_context = false;
    bool in_switch_context = false;
    TypePtr current_function_return_type = nullptr;
    
    // Label tracking for goto validation
    std::set<std::string> defined_labels;
    std::set<std::string> referenced_labels;

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
    void analyze_if_stmt(const AstNodePtr &node);
    void analyze_while_stmt(const AstNodePtr &node);
    void analyze_for_stmt(const AstNodePtr &node);
    void analyze_return_stmt(const AstNodePtr &node);
    void analyze_break_stmt(const AstNodePtr &node);
    void analyze_continue_stmt(const AstNodePtr &node);
    void analyze_goto_stmt(const AstNodePtr &node);
    void analyze_label_stmt(const AstNodePtr &node);
    
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
    
    // Type validation helpers
    bool validate_type_specifier(const std::string& type_name);
    TypePtr infer_member_access_expr(const AstNodePtr &expr);

    TypePtr resolve_type_from_specifier(const AstNodePtr &type_node, SymbolTable &symbols);
    TypePtr resolve_builtin_type(const TypeSpecifierNodeData &spec);
    
    bool types_compatible(const TypePtr &lhs, const TypePtr &rhs);
    TypePtr perform_usual_arithmetic_conversions(const TypePtr &lhs, const TypePtr &rhs);
    bool is_assignment_compatible(const TypePtr &lhs, const TypePtr &rhs);
    
    // IR generation methods
    std::string generate_ir_for_expression(const AstNodePtr &expr);
    std::string generate_ir_for_binary_expr(const AstNodePtr &expr);
    std::string generate_ir_for_unary_expr(const AstNodePtr &expr); 
    std::string generate_ir_for_assignment_expr(const AstNodePtr &expr);
    std::string generate_ir_for_identifier_expr(const AstNodePtr &expr);
    std::string generate_ir_for_literal_expr(const AstNodePtr &expr);
    
    // Simple expression string generation (no temporaries)
    std::string generate_simple_expression_string(const AstNodePtr &expr);
    
    // Scope handling for IR
    std::string get_scoped_variable_name(const std::string& name, const SymbolWeakPtr& symbol);
};
