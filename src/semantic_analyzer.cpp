#include "semantic_analyzer.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace {
std::vector<std::string> tokenize_type_name(const std::string &name) {
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : name) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}
}

SemanticAnalyzer::SemanticAnalyzer() = default;

void SemanticAnalyzer::analyze(const AstNodePtr &root, SymbolTable &symbols) {
    reporter.clear();
    current_symbols = &symbols;
    if (!root) return;
    analyze_translation_unit(root, symbols);
    current_symbols = nullptr;
}

void SemanticAnalyzer::analyze_translation_unit(const AstNodePtr &node, SymbolTable &symbols) {
    if (!node || node->kind != AstNodeKind::TranslationUnit) return;
    const auto &tu = node->as<TranslationUnitNodeData>();
    for (const auto &decl : tu.declarations) {
        analyze_declaration(decl, symbols);
    }
}

void SemanticAnalyzer::analyze_declaration(const AstNodePtr &node, SymbolTable &symbols) {
    if (!node) return;
    switch (node->kind) {
        case AstNodeKind::VariableDecl:
            analyze_variable_decl(node, symbols);
            break;
        case AstNodeKind::FunctionDecl:
            analyze_function_decl(node, symbols);
            break;
        case AstNodeKind::StructDecl:
            analyze_struct_decl(node, symbols);
            break;
        case AstNodeKind::UnionDecl:
            analyze_union_decl(node, symbols);
            break;
        case AstNodeKind::EnumDecl:
            analyze_enum_decl(node, symbols);
            break;
        default:
            break;
    }
}

void SemanticAnalyzer::analyze_variable_decl(const AstNodePtr &node, SymbolTable &symbols) {
    auto &data = node->as<VariableDeclNodeData>();
    
    if (!data.type_expr) {
        reporter.report(DiagnosticSeverity::Error, "Missing type for variable '" + data.name + "'", node->range);
        return;
    }

    TypePtr type = resolve_type_from_specifier(data.type_expr, symbols);
    if (!type) {
        reporter.report(DiagnosticSeverity::Error, "Unable to resolve type for variable '" + data.name + "'", node->range);
        return;
    }

    // Apply pointer levels first (they bind tighter)
    for (int i = 0; i < data.pointer_levels; ++i) {
        type = make_pointer_type(type);
    }
    
    // Then apply array dimensions (from right to left: innermost to outermost)
    // For int matrix[3][4], dimensions are [3, 4], and we want:
    // array of 3, where each element is (array of 4 ints)
    // So we build: int -> int[4] -> int[4][3]
    if (data.is_array && !data.array_dimensions.empty()) {
        // Validate array dimensions first
        for (size_t i = 0; i < data.array_dimensions.size(); ++i) {
            int dim = data.array_dimensions[i];
            if (dim < 0) {
                reporter.report(DiagnosticSeverity::Error, 
                    "Array size must be a positive integer constant", node->range);
            } else if (dim == 0 && i != 0) { // Allow first dimension to be 0 for incomplete arrays
                reporter.report(DiagnosticSeverity::Error, 
                    "Only the first array dimension can be unspecified", node->range);
            }
        }
        
        // Build from rightmost (innermost) dimension
        for (auto it = data.array_dimensions.rbegin(); it != data.array_dimensions.rend(); ++it) {
            int dim = *it;
            type = make_array_type(type, dim > 0 ? std::optional<size_t>(dim) : std::nullopt);
        }
    }

    node->inferred_type = type;
    if (auto sym = data.symbol.lock()) {
        sym->type = type;
    }

    // Validate const variables must be initialized
    if (type && type->qualifiers.is_const && !data.initializer) {
        reporter.report(DiagnosticSeverity::Error, 
            "Const variable '" + data.name + "' must be initialized at declaration", 
            node->range);
    }

    // Validate initializer if present
    if (data.initializer) {
        // Check for array initializer validation
        if (data.is_array && data.initializer->kind == AstNodeKind::InitializerList) {
            auto &init_data = data.initializer->as<InitializerListNodeData>();
            
            // Validate array initializer count against array size
            if (!data.array_dimensions.empty()) {
                int expected_size = data.array_dimensions[0]; // First dimension
                int actual_size = init_data.elements.size();
                
                if (expected_size > 0 && actual_size > expected_size) {
                    reporter.report(DiagnosticSeverity::Error, 
                        "Too many initializers for array '" + data.name + "': expected at most " + 
                        std::to_string(expected_size) + " but got " + std::to_string(actual_size), 
                        node->range);
                }
            }
        }
        
        TypePtr init_type = infer_expression_type(data.initializer);
        if (init_type && !is_assignment_compatible(type, init_type)) {
            reporter.report(DiagnosticSeverity::Error, 
                "Incompatible initializer for variable '" + data.name + "': expected " + 
                type_to_string(type) + " but got " + type_to_string(init_type), 
                node->range);
        }
        
        // Emit IR for variable initializer (simple case: non-array)
        // For arrays, skip for now (will need to handle element-wise initialization)
        if (!data.is_array && data.initializer) {
            // Get scoped variable name
            std::string scoped_name = get_scoped_variable_name(data.name, data.symbol);
            
            // For simple literals, emit direct assignment
            if (data.initializer->kind == AstNodeKind::LiteralExpr) {
                auto &lit_data = data.initializer->as<LiteralExprNodeData>();
                ir_gen.emit(IROpcode::ASSIGN, scoped_name, lit_data.lexeme, "", "", node->range.begin.line);
            } else {
                // For complex expressions, generate proper TAC and assign the final result
                std::string rhs_result = generate_ir_for_expression(data.initializer);
                if (!rhs_result.empty()) {
                    ir_gen.emit(IROpcode::ASSIGN, scoped_name, rhs_result, "", "", node->range.begin.line);
                }
            }
        }
    }
}

TypePtr SemanticAnalyzer::resolve_type_from_specifier(const AstNodePtr &type_node, SymbolTable &symbols) {
    if (!type_node || type_node->kind != AstNodeKind::TypeSpecifier) return nullptr;
    const auto &spec = type_node->as<TypeSpecifierNodeData>();
    switch (spec.kind) {
        case TypeSpecifierKind::Builtin: {
            // First validate the type specifier for invalid combinations
            if (!validate_type_specifier(spec.name)) {
                return nullptr; // Error already reported by validate_type_specifier
            }
            
            TypePtr builtin_type = resolve_builtin_type(spec);
            if (builtin_type) {
                return builtin_type;
            }
            // If it's not a recognized builtin type, try to look it up as a user-defined type
            SymbolPtr sym = symbols.lookup_tag(spec.name);
            if (!sym) {
                sym = symbols.lookup_ident(spec.name);
            }
            if (sym && sym->kind == SymbolKind::StructTag) {
                // Check the symbol's type to determine if it's struct, union, or enum
                if (sym->type && sym->type->category == TypeCategory::Struct) {
                    return sym->type; // Return the existing type
                } else if (sym->type && sym->type->category == TypeCategory::Union) {
                    return sym->type; // Return the existing type  
                } else if (sym->type && sym->type->category == TypeCategory::Enum) {
                    return sym->type; // Return the existing type
                } else {
                    // Fallback: create struct type (for backwards compatibility)
                    return make_struct_type(spec.name);
                }
            }
            return nullptr;
        }
        case TypeSpecifierKind::Identifier: {
            // Look up the identifier in the symbol table - first try as a tag (struct/class/union/enum)
            SymbolPtr sym = symbols.lookup_tag(spec.name);
            if (!sym) {
                // If not found as a tag, try as a regular identifier (typedef)
                sym = symbols.lookup_ident(spec.name);
            }
            if (!sym) return nullptr;
            
            // Check what kind of symbol it is
            if (sym->kind == SymbolKind::StructTag) {

                // Check the symbol's type to determine if it's struct, union, or enum
                if (sym->type && sym->type->category == TypeCategory::Struct) {
                    return sym->type; // Return the existing type
                } else if (sym->type && sym->type->category == TypeCategory::Union) {
                    return sym->type; // Return the existing type  
                } else if (sym->type && sym->type->category == TypeCategory::Enum) {
                    return sym->type; // Return the existing type
                } else {
                    // Fallback: create struct type (for backwards compatibility)
                    return make_struct_type(spec.name);
                }
            } else if (sym->kind == SymbolKind::TypedefName) {
                // TODO: resolve typedef to its underlying type
                return nullptr;
            }
            return nullptr;
        }
        case TypeSpecifierKind::Struct:
            return make_struct_type(spec.name);
        case TypeSpecifierKind::Union:
            return make_union_type(spec.name);
        case TypeSpecifierKind::Enum:
            return make_enum_type(spec.name);
        default:
            return nullptr;
    }
}

TypePtr SemanticAnalyzer::resolve_builtin_type(const TypeSpecifierNodeData &spec) {
    const auto tokens = tokenize_type_name(spec.name);
    if (tokens.empty()) return nullptr;

    std::unordered_set<std::string> qualifiers;
    bool is_signed = false;
    bool is_unsigned = false;
    bool is_short = false;
    int long_count = 0;
    std::string base_name;
    bool has_base_type = false;

    for (const auto &tok : tokens) {
        if (tok == "const" || tok == "volatile" || tok == "restrict" || tok == "_Atomic") {
            qualifiers.insert(tok);
            continue;
        }
        if (tok == "signed") { is_signed = true; continue; }
        if (tok == "unsigned") { is_unsigned = true; continue; }
        if (tok == "short") { is_short = true; continue; }
        if (tok == "long") { ++long_count; continue; }
        
        // Check if this is a base type name
        if (tok == "int" || tok == "char" || tok == "bool" || tok == "void" || 
            tok == "double") {
            if (has_base_type) {
                // Multiple base types detected - this is an error
                return nullptr; // Will be handled by caller
            }
            base_name = tok;
            has_base_type = true;
            continue;
        }
        
        // If we reach here, it's an unknown token - might be a user-defined type
        // Don't treat it as error here, let the caller handle it
        return nullptr;
    }

    TypeQualifierSet qualifier_set;
    qualifier_set.is_const = qualifiers.count("const") > 0;
    qualifier_set.is_volatile = qualifiers.count("volatile") > 0;
    qualifier_set.is_restrict = qualifiers.count("restrict") > 0;
    qualifier_set.is_atomic = qualifiers.count("_Atomic") > 0;

    try {
        BuiltinTypeKind builtin = builtin_from_specifiers(is_signed, is_unsigned, long_count, is_short, base_name);
        return make_builtin_type(builtin, qualifier_set);
    } catch (const std::exception &) {
        return nullptr;
    }
}

// Helper function to validate type specifier for invalid combinations
bool SemanticAnalyzer::validate_type_specifier(const std::string& type_name) {
    const auto tokens = tokenize_type_name(type_name);
    if (tokens.empty()) return false;

    std::vector<std::string> base_types;
    
    for (const auto &tok : tokens) {
        // Skip qualifiers and modifiers
        if (tok == "const" || tok == "volatile" || tok == "restrict" || tok == "_Atomic" ||
            tok == "signed" || tok == "unsigned" || tok == "short" || tok == "long") {
            continue;
        }
        
        // Check if this is a base type name
        if (tok == "int" || tok == "char" || tok == "bool" || tok == "void" || 
            tok == "double") {
            base_types.push_back(tok);
        }
    }
    
    // If we have multiple base types, it's invalid
    if (base_types.size() > 1) {
        std::string error_msg = "Invalid type specifier: multiple base types '";
        for (size_t i = 0; i < base_types.size(); ++i) {
            if (i > 0) error_msg += "' and '";
            error_msg += base_types[i];
        }
        error_msg += "'";
        // Report error with dummy range - ideally we'd have proper source location
        reporter.report(DiagnosticSeverity::Error, error_msg, SourceRange{});
        return false;
    }
    
    return true;
}

void SemanticAnalyzer::analyze_function_decl(const AstNodePtr &node, SymbolTable &symbols) {
    auto &data = node->as<FunctionDeclNodeData>();
    
    // Resolve return type
    TypePtr return_type = resolve_type_from_specifier(data.return_type, symbols);
    if (!return_type) {
        return_type = make_builtin_type(BuiltinTypeKind::Int); // default to int
    }
    
    // Build parameter types
    std::vector<TypePtr> param_types;
    for (const auto &param_node : data.parameters) {
        if (!param_node || param_node->kind != AstNodeKind::ParameterDecl) continue;
        
        auto &param_data = param_node->as<ParameterDeclNodeData>();
        TypePtr param_type = resolve_type_from_specifier(param_data.type_expr, symbols);
        if (!param_type) {
            reporter.report(DiagnosticSeverity::Error, 
                "Unable to resolve type for parameter '" + param_data.name + "'", param_node->range);
            continue;
        }
        
        // Apply pointer levels
        for (int i = 0; i < param_data.pointer_levels; ++i) {
            param_type = make_pointer_type(param_type);
        }
        
        // Apply array dimensions (arrays decay to pointers in parameter lists)
        if (param_data.is_array && !param_data.array_dimensions.empty()) {
            // In function parameters, arrays decay to pointers
            // e.g., void foo(int arr[10]) is the same as void foo(int *arr)
            // But multi-dimensional arrays preserve inner dimensions:
            // void foo(int arr[3][4]) -> void foo(int (*arr)[4])
            
            if (param_data.array_dimensions.size() == 1) {
                // Single dimension: just convert to pointer
                param_type = make_pointer_type(param_type);
            } else {
                // Multi-dimensional: build nested arrays from right to left, skip first
                for (auto it = param_data.array_dimensions.rbegin(); 
                     it != param_data.array_dimensions.rend() - 1; ++it) {
                    int dim = *it;
                    param_type = make_array_type(param_type, dim > 0 ? std::optional<size_t>(dim) : std::nullopt);
                }
                // Then make it a pointer
                param_type = make_pointer_type(param_type);
            }
        }
        
        param_node->inferred_type = param_type;
        if (auto sym = param_data.symbol.lock()) {
            sym->type = param_type;
        }
        
        param_types.push_back(param_type);
    }
    
    // Build function type
    TypePtr func_type = make_function_type(return_type, param_types, data.is_variadic);
    
    node->inferred_type = func_type;
    if (auto sym = data.symbol.lock()) {
        sym->type = func_type;
    }

    // Analyze function body if present
    if (data.body) {
        // Set function return type context for return statement validation
        TypePtr saved_return_type = current_function_return_type;
        current_function_return_type = return_type;
        
        // The function body (compound statement) will enter its own scope
        // We need to add parameters to that scope before analyzing statements
        // Store parameters temporarily so compound_stmt can access them
        saved_params_for_body = data.parameters;
        
        analyze_statement(data.body);
        // Note: saved_params_for_body is cleared by analyze_compound_stmt
        
        // Restore previous return type context
        current_function_return_type = saved_return_type;
    }
}

void SemanticAnalyzer::analyze_struct_decl(const AstNodePtr &node, SymbolTable &symbols) {
    auto &data = node->as<StructDeclNodeData>();
    
    // If this is a struct definition (has members), process the members
    if (data.is_definition && !data.members.empty()) {
        // TODO: Process struct members to build member list
        // For now, just create the struct type
    }
    
    // Create and register the struct type
    TypePtr struct_type = make_struct_type(data.name);
    
    // Add struct tag to symbol table
    auto sym = std::make_shared<Symbol>(data.name, SymbolKind::StructTag, node->range.begin.line);
    sym->type = struct_type;
    sym->is_defined = data.is_definition;
    sym->decl_node = node; // Link back to the AST node
    symbols.insert_tag(sym);
}

void SemanticAnalyzer::analyze_union_decl(const AstNodePtr &node, SymbolTable &symbols) {
    auto &data = node->as<UnionDeclNodeData>();
    
    // Create and register the union type
    TypePtr union_type = make_union_type(data.name);
    
    // Add union tag to symbol table
    auto sym = std::make_shared<Symbol>(data.name, SymbolKind::StructTag, node->range.begin.line);
    sym->type = union_type;
    sym->is_defined = data.is_definition;
    sym->decl_node = node; // Link back to the AST node
    symbols.insert_tag(sym);
}

void SemanticAnalyzer::analyze_enum_decl(const AstNodePtr &node, SymbolTable &symbols) {
    auto &data = node->as<EnumDeclNodeData>();
    
    // Create and register the enum type
    TypePtr enum_type = make_enum_type(data.name);
    
    // Add enum tag to symbol table
    auto sym = std::make_shared<Symbol>(data.name, SymbolKind::StructTag, node->range.begin.line);
    sym->type = enum_type;
    sym->is_defined = data.is_definition;
    sym->decl_node = node; // Link back to the AST node
    symbols.insert_tag(sym);
    
    // Process enumerators if this is a definition
    if (data.is_definition) {
        for (const auto &enumerator : data.enumerators) {
            if (enumerator && enumerator->kind == AstNodeKind::EnumeratorDecl) {
                auto &enum_data = enumerator->as<EnumeratorDeclNodeData>();
                
                // Add enumerator as an enum constant
                auto enum_sym = std::make_shared<Symbol>(enum_data.name, SymbolKind::EnumConstant, enumerator->range.begin.line);
                enum_sym->type = enum_type;
                symbols.insert_ident(enum_sym);
            }
        }
    }
}

void SemanticAnalyzer::analyze_statement(const AstNodePtr &node) {
    if (!node) return;
    switch (node->kind) {
        case AstNodeKind::CompoundStmt:
            analyze_compound_stmt(node);
            break;
        case AstNodeKind::ExpressionStmt:
            analyze_expression_stmt(node);
            break;
        case AstNodeKind::VariableDecl:
            analyze_variable_decl(node, *current_symbols);
            break;
        case AstNodeKind::InitializerList: {
            // InitializerList can appear as a statement (e.g., from declaration)
            // Process each element recursively
            auto &data = node->as<InitializerListNodeData>();
            for (const auto &elem : data.elements) {
                analyze_statement(elem);
            }
            break;
        }
        case AstNodeKind::IfStmt:
            analyze_if_stmt(node);
            break;
        case AstNodeKind::WhileStmt:
            analyze_while_stmt(node);
            break;
        case AstNodeKind::ForStmt:
            analyze_for_stmt(node);
            break;
        case AstNodeKind::ReturnStmt:
            analyze_return_stmt(node);
            break;
        case AstNodeKind::BreakStmt:
            analyze_break_stmt(node);
            break;
        case AstNodeKind::ContinueStmt:
            analyze_continue_stmt(node);
            break;
        case AstNodeKind::GotoStmt:
            analyze_goto_stmt(node);
            break;
        case AstNodeKind::LabelStmt:
            analyze_label_stmt(node);
            break;
        default:
            break;
    }
}

void SemanticAnalyzer::analyze_compound_stmt(const AstNodePtr &node) {
    auto &data = node->as<CompoundStmtNodeData>();
    
    // Enter a new scope for this compound statement
    if (current_symbols) {
        current_symbols->enter_scope();
        
        // If this is a function body, add parameters to this scope
        if (!saved_params_for_body.empty()) {
            for (const auto &param_node : saved_params_for_body) {
                if (!param_node || param_node->kind != AstNodeKind::ParameterDecl) continue;
                auto &param_data = param_node->as<ParameterDeclNodeData>();
                
                if (!param_data.name.empty() && param_node->inferred_type) {
                    auto param_sym = std::make_shared<Symbol>();
                    param_sym->name = param_data.name;
                    param_sym->kind = SymbolKind::Variable;
                    param_sym->type = param_node->inferred_type;
                    param_sym->line_declared = param_node->range.begin.line;
                    param_sym->is_parameter = true;
                    param_sym->scope_level = current_symbols->current_scope_level();
                    
                    current_symbols->insert_ident(param_sym);
                    param_data.symbol = param_sym;
                }
            }
            // Clear after using so nested compound statements don't see them
            saved_params_for_body.clear();
        }
    }
    
    for (const auto &stmt : data.statements) {
        analyze_statement(stmt);
    }
    
    // Exit the scope after analyzing all statements
    if (current_symbols) {
        current_symbols->exit_scope();
    }
}

void SemanticAnalyzer::analyze_expression_stmt(const AstNodePtr &node) {
    auto &data = node->as<ExpressionStmtNodeData>();
    if (data.expression) {
        // Perform semantic analysis
        infer_expression_type(data.expression);
        
        // Generate IR for the expression
        generate_ir_for_expression(data.expression);
    }
}

TypePtr SemanticAnalyzer::infer_expression_type(const AstNodePtr &expr) {
    if (!expr) return nullptr;
    
    // Return cached type if already inferred
    if (expr->inferred_type) return expr->inferred_type;
    
    TypePtr result;
    switch (expr->kind) {
        case AstNodeKind::BinaryExpr:
            result = infer_binary_expr(expr);
            break;
        case AstNodeKind::UnaryExpr:
            result = infer_unary_expr(expr);
            break;
        case AstNodeKind::AssignmentExpr:
            result = infer_assignment_expr(expr);
            break;
        case AstNodeKind::CallExpr:
            result = infer_call_expr(expr);
            break;
        case AstNodeKind::IdentifierExpr:
            result = infer_identifier_expr(expr);
            break;
        case AstNodeKind::LiteralExpr:
            result = infer_literal_expr(expr);
            break;
        case AstNodeKind::SubscriptExpr:
            result = infer_subscript_expr(expr);
            break;
        case AstNodeKind::MemberAccessExpr:
            result = infer_member_access_expr(expr);
            break;
        case AstNodeKind::ConditionalExpr: {
            auto &data = expr->as<ConditionalExprNodeData>();
            TypePtr condition_type = infer_expression_type(data.condition);
            TypePtr then_type = infer_expression_type(data.then_expr);
            TypePtr else_type = infer_expression_type(data.else_expr);
            
            // Validate condition is scalar type
            if (condition_type && !condition_type->is_scalar()) {
                reporter.report(DiagnosticSeverity::Error, 
                    "Condition in ternary operator must be a scalar type", expr->range);
            }
            
            result = perform_usual_arithmetic_conversions(then_type, else_type);
            break;
        }
        case AstNodeKind::CastExpr: {
            // For now, always return int for casts to get basic functionality working
            // TODO: Properly parse and resolve target type from cast expression
            result = make_builtin_type(BuiltinTypeKind::Int);
            break;
        }
        default:
            break;
    }
    
    expr->inferred_type = result;
    return result;
}

TypePtr SemanticAnalyzer::infer_binary_expr(const AstNodePtr &expr) {
    auto &data = expr->as<BinaryExprNodeData>();
    TypePtr lhs_type = infer_expression_type(data.lhs);
    TypePtr rhs_type = infer_expression_type(data.rhs);
    
    if (!lhs_type || !rhs_type) return nullptr;
    
    // Arithmetic operators with special handling for pointers
    if (data.op == "+" || data.op == "-") {
        // Pointer + integer OR integer + pointer
        if (lhs_type->is_pointer() && rhs_type->is_integer()) {
            return lhs_type; // result is pointer type
        }
        if (rhs_type->is_pointer() && lhs_type->is_integer() && data.op == "+") {
            return rhs_type; // result is pointer type
        }
        // Pointer - pointer (result is ptrdiff_t, we'll use int for simplicity)
        if (lhs_type->is_pointer() && rhs_type->is_pointer() && data.op == "-") {
            return make_builtin_type(BuiltinTypeKind::Int);
        }
        // Normal arithmetic
        if (lhs_type->is_integer() && rhs_type->is_integer()) {
            return perform_usual_arithmetic_conversions(lhs_type, rhs_type);
        }
        if (lhs_type->is_floating() || rhs_type->is_floating()) {
            return perform_usual_arithmetic_conversions(lhs_type, rhs_type);
        }
        reporter.report(DiagnosticSeverity::Error, 
            "Invalid operand types for " + data.op, expr->range);
        return nullptr;
    }
    
    if (data.op == "*" || data.op == "/" || data.op == "%") {
        if (data.op == "%" && (!lhs_type->is_integer() || !rhs_type->is_integer())) {
            reporter.report(DiagnosticSeverity::Error, 
                "Modulo operator requires integer operands", expr->range);
            return nullptr;
        }
        return perform_usual_arithmetic_conversions(lhs_type, rhs_type);
    }
    
    // Relational/equality operators
    if (data.op == "<" || data.op == ">" || data.op == "<=" || data.op == ">=" ||
        data.op == "==" || data.op == "!=") {
        return make_builtin_type(BuiltinTypeKind::Int); // result is int
    }
    
    // Logical operators
    if (data.op == "&&" || data.op == "||") {
        return make_builtin_type(BuiltinTypeKind::Int);
    }
    
    // Bitwise operators
    if (data.op == "&" || data.op == "|" || data.op == "^" || data.op == "<<" || data.op == ">>") {
        if (!lhs_type->is_integer() || !rhs_type->is_integer()) {
            reporter.report(DiagnosticSeverity::Error, 
                "Bitwise operators require integer operands", expr->range);
            return nullptr;
        }
        return perform_usual_arithmetic_conversions(lhs_type, rhs_type);
    }
    
    // Comma operator
    if (data.op == ",") {
        return rhs_type;
    }
    
    return lhs_type;
}

TypePtr SemanticAnalyzer::infer_unary_expr(const AstNodePtr &expr) {
    auto &data = expr->as<UnaryExprNodeData>();
    TypePtr operand_type = infer_expression_type(data.operand);
    
    if (!operand_type) return nullptr;
    
    if (data.op == "&") {
        return make_pointer_type(operand_type);
    }
    
    if (data.op == "*") {
        if (!operand_type->is_pointer()) {
            reporter.report(DiagnosticSeverity::Error, 
                "Dereference operator requires pointer type", expr->range);
            return nullptr;
        }
        auto &ptr_info = std::get<PointerTypeInfo>(operand_type->payload);
        return ptr_info.pointee;
    }
    
    if (data.op == "++" || data.op == "--") {
        if (!operand_type->is_scalar()) {
            reporter.report(DiagnosticSeverity::Error, 
                "Increment/decrement requires scalar type", expr->range);
            return nullptr;
        }
        return operand_type;
    }
    
    if (data.op == "!" || data.op == "~") {
        return make_builtin_type(BuiltinTypeKind::Int);
    }
    
    if (data.op == "+" || data.op == "-") {
        if (!operand_type->is_integer() && !operand_type->is_floating()) {
            reporter.report(DiagnosticSeverity::Error, 
                "Unary +/- requires arithmetic type", expr->range);
            return nullptr;
        }
        return operand_type;
    }
    
    if (data.op == "sizeof") {
        return make_builtin_type(BuiltinTypeKind::UnsignedLong);
    }
    
    return operand_type;
}

TypePtr SemanticAnalyzer::infer_assignment_expr(const AstNodePtr &expr) {
    auto &data = expr->as<AssignmentExprNodeData>();
    TypePtr lhs_type = infer_expression_type(data.lhs);
    TypePtr rhs_type = infer_expression_type(data.rhs);
    
    if (!lhs_type || !rhs_type) return nullptr;
    
    if (!is_assignment_compatible(lhs_type, rhs_type)) {
        reporter.report(DiagnosticSeverity::Error, 
            "Incompatible types in assignment: " + type_to_string(lhs_type) + 
            " = " + type_to_string(rhs_type), expr->range);
    }
    
    return lhs_type;
}

// Helper function to generate a function signature from arguments
std::string SemanticAnalyzer::generate_call_signature(const std::string& func_name, const std::vector<TypePtr>& arg_types) {
    std::string signature = func_name + "(";
    for (size_t i = 0; i < arg_types.size(); i++) {
        if (i > 0) signature += ",";
        // Convert TypePtr to string representation
        if (arg_types[i]) {
            if (arg_types[i]->category == TypeCategory::Builtin) {
                auto builtin_kind = std::get<BuiltinTypeKind>(arg_types[i]->payload);
                switch (builtin_kind) {
                    case BuiltinTypeKind::Void: signature += "void"; break;
                    case BuiltinTypeKind::Char: signature += "char"; break;
                    case BuiltinTypeKind::Int: signature += "int"; break;
                    case BuiltinTypeKind::Bool: signature += "bool"; break;
                    case BuiltinTypeKind::Double: signature += "double"; break;
                    default: signature += "unknown"; break;
                }
            } else if (arg_types[i]->category == TypeCategory::Pointer) {
                // Handle pointer types - get the pointee type and add *
                auto &pointer_info = std::get<PointerTypeInfo>(arg_types[i]->payload);
                if (pointer_info.pointee && pointer_info.pointee->category == TypeCategory::Builtin) {
                    auto builtin_kind = std::get<BuiltinTypeKind>(pointer_info.pointee->payload);
                    switch (builtin_kind) {
                        case BuiltinTypeKind::Void: signature += "void*"; break;
                        case BuiltinTypeKind::Char: signature += "char*"; break;
                        case BuiltinTypeKind::Int: signature += "int*"; break;
                        case BuiltinTypeKind::Bool: signature += "bool*"; break;
                        case BuiltinTypeKind::Double: signature += "double*"; break;
                        default: signature += "unknown*"; break;
                    }
                } else {
                    signature += "unknown*";
                }
            } else {
                signature += "unknown";
            }
        } else {
            signature += "unknown";
        }
    }
    signature += ")";
    return signature;
}

// Helper function to find the best matching overload
SymbolPtr SemanticAnalyzer::find_best_overload(const std::string& func_name, const std::vector<TypePtr>& arg_types) {
    if (!current_symbols) return nullptr;
    
    // Generate the expected signature for this call
    std::string expected_signature = generate_call_signature(func_name, arg_types);
    
    // Try to find an exact match first
    SymbolPtr exact_match = current_symbols->lookup_ident(expected_signature);
    if (exact_match && exact_match->kind == SymbolKind::Function) {
        return exact_match;
    }
    
    // If no exact match, look for compatible overloads with implicit conversions
    // Check all functions with the same base name
    SymbolPtr base_func = current_symbols->lookup_ident(func_name);
    if (base_func && base_func->kind == SymbolKind::Function && base_func->type && 
        base_func->type->category == TypeCategory::Function) {
        
        auto &func_info = std::get<FunctionTypeInfo>(base_func->type->payload);
        
        // Check if argument count matches (for non-variadic functions)
        if (!func_info.is_variadic && arg_types.size() != func_info.param_types.size()) {
            return nullptr; // Wrong number of arguments
        }
        
        // Check if all arguments are assignment-compatible
        bool all_compatible = true;
        size_t check_count = std::min(arg_types.size(), func_info.param_types.size());
        for (size_t i = 0; i < check_count; ++i) {
            if (!is_assignment_compatible(func_info.param_types[i], arg_types[i])) {
                all_compatible = false;
                break;
            }
        }
        
        if (all_compatible) {
            return base_func; // Found a compatible overload
        }
    }
    
    return nullptr; // No matching overload found
}

TypePtr SemanticAnalyzer::infer_call_expr(const AstNodePtr &expr) {
    auto &data = expr->as<CallExprNodeData>();
    
    // Infer argument types first
    std::vector<TypePtr> arg_types;
    for (const auto &arg : data.arguments) {
        TypePtr arg_type = infer_expression_type(arg);
        if (arg_type) {
            arg_types.push_back(arg_type);
        }
    }
    
    // Check if callee is an identifier (for function overload resolution)
    if (data.callee && data.callee->kind == AstNodeKind::IdentifierExpr) {
        auto &callee_data = data.callee->as<IdentifierExprNodeData>();
        std::string func_name = callee_data.name;
        
        // Try to find the best matching overload
        SymbolPtr best_overload = find_best_overload(func_name, arg_types);
        if (best_overload && best_overload->type) {
            // Found a matching overload - use its type
            auto func_type = best_overload->type;
            if (func_type->category == TypeCategory::Function) {
                auto &func_info = std::get<FunctionTypeInfo>(func_type->payload);
                
                // Set the resolved symbol in the callee for later use
                callee_data.symbol = best_overload;
                
                // Check argument types match parameter types  
                size_t check_count = std::min(arg_types.size(), func_info.param_types.size());
                for (size_t i = 0; i < check_count; ++i) {
                    if (!is_assignment_compatible(func_info.param_types[i], arg_types[i])) {
                        reporter.report(DiagnosticSeverity::Error, 
                            "Argument " + std::to_string(i + 1) + " type mismatch: expected " + 
                            type_to_string(func_info.param_types[i]) + " but got " + 
                            type_to_string(arg_types[i]), expr->range);
                    }
                }
                
                // Return the function's return type
                return func_info.return_type;
            }
        }
        
        // If no exact overload match, report an error
        reporter.report(DiagnosticSeverity::Error, 
            "No matching function for call to '" + func_name + "' with " + 
            std::to_string(arg_types.size()) + " arguments", expr->range);
        return make_builtin_type(BuiltinTypeKind::Int);
    }
    
    // Fall back to original logic for non-identifier callees
    TypePtr callee_type = infer_expression_type(data.callee);
    
    if (!callee_type) {
        return make_builtin_type(BuiltinTypeKind::Int); // default fallback
    }
    
    // Check if callee is a function type
    if (callee_type->category != TypeCategory::Function) {
        reporter.report(DiagnosticSeverity::Error, 
            "Called object is not a function", expr->range);
        return make_builtin_type(BuiltinTypeKind::Int);
    }
    
    auto &func_info = std::get<FunctionTypeInfo>(callee_type->payload);
    
    // Check argument count (unless variadic)
    if (!func_info.is_variadic) {
        if (arg_types.size() != func_info.param_types.size()) {
            reporter.report(DiagnosticSeverity::Error, 
                "Function call argument count mismatch: expected " + 
                std::to_string(func_info.param_types.size()) + " but got " + 
                std::to_string(arg_types.size()), expr->range);
        }
    } else {
        // Variadic: must have at least as many args as non-variadic params
        if (arg_types.size() < func_info.param_types.size()) {
            reporter.report(DiagnosticSeverity::Error, 
                "Too few arguments to variadic function: expected at least " + 
                std::to_string(func_info.param_types.size()) + " but got " + 
                std::to_string(arg_types.size()), expr->range);
        }
    }
    
    // Check argument types match parameter types
    size_t check_count = std::min(arg_types.size(), func_info.param_types.size());
    for (size_t i = 0; i < check_count; ++i) {
        if (!is_assignment_compatible(func_info.param_types[i], arg_types[i])) {
            reporter.report(DiagnosticSeverity::Error, 
                "Argument " + std::to_string(i + 1) + " type mismatch: expected " + 
                type_to_string(func_info.param_types[i]) + " but got " + 
                type_to_string(arg_types[i]), expr->range);
        }
    }
    
    // Return the function's return type
    return func_info.return_type;
}

TypePtr SemanticAnalyzer::infer_identifier_expr(const AstNodePtr &expr) {
    auto &data = expr->as<IdentifierExprNodeData>();
    
    // Always try fresh lookup from symbol table
    if (current_symbols) {
        SymbolPtr sym;
        
        // If we have a scope restriction, use scope-aware lookup
        if (max_scope_for_lookup >= 0) {
            sym = current_symbols->lookup_ident_max_scope(data.name, max_scope_for_lookup);
        } else {
            sym = current_symbols->lookup_ident(data.name);
        }
        
        if (sym) {
            data.symbol = sym;
            if (sym->type) {
                return sym->type;
            } else {
                // Symbol exists but has no type yet - might be forward reference
                reporter.report(DiagnosticSeverity::Error, 
                    "Identifier '" + data.name + "' used before type resolution", expr->range);
                return nullptr;
            }
        }
    }
    
    reporter.report(DiagnosticSeverity::Error, 
        "Undeclared identifier '" + data.name + "'", expr->range);
    return nullptr;
}

TypePtr SemanticAnalyzer::infer_subscript_expr(const AstNodePtr &expr) {
    auto &data = expr->as<SubscriptExprNodeData>();
    TypePtr array_type = infer_expression_type(data.array);
    TypePtr index_type = infer_expression_type(data.index);
    
    if (!array_type) return nullptr;
    
    // Check index is integer type
    if (index_type && !index_type->is_integer()) {
        reporter.report(DiagnosticSeverity::Error, 
            "Array subscript must be an integer type", expr->range);
    }
    
    // Array subscript: array[index] returns element type
    if (array_type->category == TypeCategory::Array) {
        auto &array_info = std::get<ArrayTypeInfo>(array_type->payload);
        return array_info.element;
    }
    
    // Pointer subscript: pointer[index] returns pointee type
    if (array_type->is_pointer()) {
        auto &ptr_info = std::get<PointerTypeInfo>(array_type->payload);
        return ptr_info.pointee;
    }
    
    reporter.report(DiagnosticSeverity::Error, 
        "Subscript operator requires array or pointer type", expr->range);
    return nullptr;
}

TypePtr SemanticAnalyzer::infer_member_access_expr(const AstNodePtr &expr) {
    auto &data = expr->as<MemberAccessExprNodeData>();
    TypePtr object_type = infer_expression_type(data.object);
    
    if (!object_type) return nullptr;
    
    // Handle pointer access (->)
    if (data.is_arrow) {
        if (!object_type->is_pointer()) {
            reporter.report(DiagnosticSeverity::Error, 
                "Arrow operator requires pointer type", expr->range);
            return nullptr;
        }
        auto &ptr_info = std::get<PointerTypeInfo>(object_type->payload);
        object_type = ptr_info.pointee;
    }
    
    // Now object_type should be a struct/union/class type
    if (object_type->category != TypeCategory::Struct && 
        object_type->category != TypeCategory::Union &&
        object_type->category != TypeCategory::Enum) {
        reporter.report(DiagnosticSeverity::Error, 
            "Member access requires struct, union, or class type", expr->range);
        return nullptr;
    }
    
    auto &tagged_info = std::get<TaggedTypeInfo>(object_type->payload);
    
    // Look up the struct/class definition in symbol table
    SymbolPtr struct_sym = current_symbols ? current_symbols->lookup_tag(tagged_info.name) : nullptr;
    if (!struct_sym || !struct_sym->is_defined) {
        reporter.report(DiagnosticSeverity::Error, 
            "Incomplete type '" + tagged_info.name + "' in member access", expr->range);
        return nullptr;
    }
    
    // Get the struct declaration AST node to access members
    auto struct_node_weak = struct_sym->decl_node;
    if (struct_node_weak.expired()) {
        reporter.report(DiagnosticSeverity::Error, 
            "Internal error: struct definition not available", expr->range);
        return nullptr;
    }
    AstNodePtr struct_node = struct_node_weak.lock();
    
    // Handle struct and union declarations (classes are parsed as structs)
    std::vector<AstNodePtr> *members = nullptr;
    if (struct_node->kind == AstNodeKind::StructDecl) {
        auto &struct_data = struct_node->as<StructDeclNodeData>();
        members = &struct_data.members;
    } else if (struct_node->kind == AstNodeKind::UnionDecl) {
        auto &union_data = struct_node->as<UnionDeclNodeData>();
        members = &union_data.members;
    } else {
        reporter.report(DiagnosticSeverity::Error, 
            "Internal error: expected struct or union declaration", expr->range);
        return nullptr;
    }
    
    if (!members) {
        reporter.report(DiagnosticSeverity::Error, 
            "Internal error: no members available", expr->range);
        return nullptr;
    }
    
    // Look up the member in the struct/union members
    for (const auto &member_node : *members) {
        if (member_node->kind == AstNodeKind::MemberDecl) {
            auto &member_data = member_node->as<MemberDeclNodeData>();
            if (member_data.name == data.member_name) {
                // Check access control
                if (member_data.access == AccessSpecifier::Private) {
                    reporter.report(DiagnosticSeverity::Error, 
                        "Access to private member '" + data.member_name + "' is not allowed", expr->range);
                    return nullptr;
                } else if (member_data.access == AccessSpecifier::Protected) {
                    // For now, treat protected same as private (inheritance not fully implemented)
                    reporter.report(DiagnosticSeverity::Error, 
                        "Access to protected member '" + data.member_name + "' is not allowed", expr->range);
                    return nullptr;
                }
                
                // TODO: Properly analyze member type from type_expr
                // For now, handle common cases to get basic functionality working
                
                if (member_data.type_expr && member_data.type_expr->kind == AstNodeKind::TypeSpecifier) {
                    auto &type_spec = member_data.type_expr->as<TypeSpecifierNodeData>();
                    

                    // Handle builtin types
                    if (type_spec.kind == TypeSpecifierKind::Builtin && type_spec.name == "int") {
                        if (member_data.pointer_levels > 0) {
                            TypePtr base = make_builtin_type(BuiltinTypeKind::Int);
                            for (int i = 0; i < member_data.pointer_levels; i++) {
                                base = make_pointer_type(base);
                            }
                            return base;
                        }
                        return make_builtin_type(BuiltinTypeKind::Int);
                    }
                    
                    // Handle user-defined types (structs/classes)
                    if (type_spec.kind == TypeSpecifierKind::Identifier) {
                        TypePtr base = make_struct_type(type_spec.name);
                        if (member_data.pointer_levels > 0) {
                            for (int i = 0; i < member_data.pointer_levels; i++) {
                                base = make_pointer_type(base);
                            }
                        }
                        return base;
                    }
                    
                    // Handle user-defined types that might be classified as Builtin
                    if (type_spec.kind == TypeSpecifierKind::Builtin && type_spec.name != "int" && 
                        type_spec.name != "char" && type_spec.name != "double" &&
                        type_spec.name != "void" && type_spec.name != "short" && type_spec.name != "long") {
                        // This is likely a user-defined type (struct/class name)
                        TypePtr base = make_struct_type(type_spec.name);
                        if (member_data.pointer_levels > 0) {
                            for (int i = 0; i < member_data.pointer_levels; i++) {
                                base = make_pointer_type(base);
                            }
                        }
                        return base;
                    }
                }
                
                // If we can't determine the type, report an error for now
                reporter.report(DiagnosticSeverity::Error, 
                    "Unable to determine type of member '" + data.member_name + "'", expr->range);
                return nullptr;
            }
        }
    }
    
    reporter.report(DiagnosticSeverity::Error, 
        "No member named '" + data.member_name + "' in struct/class '" + tagged_info.name + "'", expr->range);
    return nullptr;
}

TypePtr SemanticAnalyzer::infer_literal_expr(const AstNodePtr &expr) {
    auto &data = expr->as<LiteralExprNodeData>();
    
    switch (data.literal_kind) {
        case LiteralKind::Integer:
            return make_builtin_type(BuiltinTypeKind::Int);
        case LiteralKind::Double:
            return make_builtin_type(BuiltinTypeKind::Double);
        case LiteralKind::Character:
            return make_builtin_type(BuiltinTypeKind::Char);
        case LiteralKind::Boolean:
            return make_builtin_type(BuiltinTypeKind::Bool);
        case LiteralKind::String:
            return make_pointer_type(make_builtin_type(BuiltinTypeKind::Char));
        case LiteralKind::Null:
        case LiteralKind::Nullptr:
            return make_pointer_type(make_builtin_type(BuiltinTypeKind::Void));
    }
    
    return nullptr;
}

bool SemanticAnalyzer::types_compatible(const TypePtr &lhs, const TypePtr &rhs) {
    if (!lhs || !rhs) return false;
    return type_equals(lhs, rhs, true); // ignore qualifiers
}

TypePtr SemanticAnalyzer::perform_usual_arithmetic_conversions(const TypePtr &lhs, const TypePtr &rhs) {
    if (!lhs || !rhs) return nullptr;
    
    // If either is floating, promote to floating
    if (lhs->is_floating() || rhs->is_floating()) {
        if (lhs->category == TypeCategory::Builtin && rhs->category == TypeCategory::Builtin) {
            auto lhs_kind = std::get<BuiltinTypeKind>(lhs->payload);
            auto rhs_kind = std::get<BuiltinTypeKind>(rhs->payload);
            
            if (lhs_kind == BuiltinTypeKind::LongDouble || rhs_kind == BuiltinTypeKind::LongDouble)
                return make_builtin_type(BuiltinTypeKind::LongDouble);
            return make_builtin_type(BuiltinTypeKind::Double);
        }
    }
    
    // Integer promotion
    return make_builtin_type(BuiltinTypeKind::Int);
}

bool SemanticAnalyzer::is_assignment_compatible(const TypePtr &lhs, const TypePtr &rhs) {
    if (!lhs || !rhs) return false;
    
    // Same type (ignoring qualifiers)
    if (types_compatible(lhs, rhs)) return true;
    
    // Array-to-pointer decay: array decays to pointer to first element
    // e.g., int[10] → int*
    if (lhs->is_pointer() && rhs->category == TypeCategory::Array) {
        auto &lhs_ptr = std::get<PointerTypeInfo>(lhs->payload);
        auto &rhs_array = std::get<ArrayTypeInfo>(rhs->payload);
        // Check if pointer type matches array element type
        return types_compatible(lhs_ptr.pointee, rhs_array.element);
    }
    
    // Arithmetic types are compatible with conversions
    if ((lhs->is_integer() || lhs->is_floating()) && 
        (rhs->is_integer() || rhs->is_floating())) {
        return true;
    }
    
    // Pointer assignments - check pointee type compatibility
    if (lhs->is_pointer() && rhs->is_pointer()) {
        auto &lhs_ptr = std::get<PointerTypeInfo>(lhs->payload);
        auto &rhs_ptr = std::get<PointerTypeInfo>(rhs->payload);
        
        // void* is compatible with any pointer
        if (lhs_ptr.pointee && lhs_ptr.pointee->category == TypeCategory::Builtin) {
            auto kind = std::get<BuiltinTypeKind>(lhs_ptr.pointee->payload);
            if (kind == BuiltinTypeKind::Void) return true;
        }
        if (rhs_ptr.pointee && rhs_ptr.pointee->category == TypeCategory::Builtin) {
            auto kind = std::get<BuiltinTypeKind>(rhs_ptr.pointee->payload);
            if (kind == BuiltinTypeKind::Void) return true;
        }
        
        // Otherwise, pointee types must be compatible
        return types_compatible(lhs_ptr.pointee, rhs_ptr.pointee);
    }
    
    return false;
}

// Control flow statement analysis functions
void SemanticAnalyzer::analyze_if_stmt(const AstNodePtr &node) {
    auto &data = node->as<IfStmtNodeData>();
    
    // Generate labels for control flow
    std::string else_label = ir_gen.new_label();    // L1: else branch
    std::string end_label = ir_gen.new_label();     // L2: end of if statement
    
    // Analyze and generate IR for condition expression
    if (data.condition) {
        // Set scope context for condition analysis (condition should only see outer scope symbols)
        int saved_max_scope = max_scope_for_lookup;
        // The if condition should only see symbols from the enclosing scope and above
        // We need to exclude any scopes created by the if/else blocks themselves
        max_scope_for_lookup = std::max(1, current_symbols->current_scope_level() - 3);
        
        TypePtr condition_type = infer_expression_type(data.condition);
        if (condition_type) {
            // Check that condition is a scalar type (can be used in boolean context)
            if (!condition_type->is_scalar()) {
                reporter.report(DiagnosticSeverity::Error, 
                    "Condition in if statement must be a scalar type", 
                    node->range);
            }
        }
        
        // Generate IR for condition and conditional jump (keep scope context)
        std::string condition_result = generate_ir_for_expression(data.condition);
        if (!condition_result.empty()) {
            // If condition is false, jump to else_label
            ir_gen.emit(IROpcode::CJUMP, "", condition_result, "0", else_label, node->range.begin.line);
        }
        
        // Restore scope context
        max_scope_for_lookup = saved_max_scope;
    }
    
    // Analyze the then branch
    if (data.then_branch) {
        analyze_statement(data.then_branch);
    }
    
    // Jump to end if there's an else branch
    if (data.else_branch) {
        ir_gen.emit(IROpcode::JUMP, "", "", "", end_label, node->range.begin.line);
    }
    
    // Emit else label
    ir_gen.emit(IROpcode::LABEL, "", "", "", else_label, node->range.begin.line);
    
    // Analyze the else branch if present
    if (data.else_branch) {
        analyze_statement(data.else_branch);
    }
    
    // Emit end label
    ir_gen.emit(IROpcode::LABEL, "", "", "", end_label, node->range.begin.line);
}

void SemanticAnalyzer::analyze_while_stmt(const AstNodePtr &node) {
    auto &data = node->as<WhileStmtNodeData>();
    
    // Enter loop context for break/continue validation
    bool saved_in_loop = in_loop_context;
    in_loop_context = true;
    
    // Analyze the condition expression
    if (data.condition) {
        TypePtr condition_type = infer_expression_type(data.condition);
        if (condition_type && !condition_type->is_scalar()) {
            reporter.report(DiagnosticSeverity::Error, 
                "Condition in while statement must be a scalar type", 
                node->range);
        }
    }
    
    // Analyze the loop body
    if (data.body) {
        analyze_statement(data.body);
    }
    
    // Restore loop context
    in_loop_context = saved_in_loop;
}

void SemanticAnalyzer::analyze_for_stmt(const AstNodePtr &node) {
    auto &data = node->as<ForStmtNodeData>();
    
    // Enter new scope for for-loop variable declarations
    if (current_symbols) {
        current_symbols->enter_scope();
    }
    
    // Enter loop context for break/continue validation
    bool saved_in_loop = in_loop_context;
    in_loop_context = true;
    
    // Analyze initialization
    if (data.init) {
        analyze_statement(data.init);
    }
    
    // Analyze condition
    if (data.condition) {
        TypePtr condition_type = infer_expression_type(data.condition);
        if (condition_type && !condition_type->is_scalar()) {
            reporter.report(DiagnosticSeverity::Error, 
                "Condition in for statement must be a scalar type", 
                node->range);
        }
    }
    
    // Analyze increment
    if (data.increment) {
        infer_expression_type(data.increment);
    }
    
    // Analyze loop body
    if (data.body) {
        analyze_statement(data.body);
    }
    
    // Restore context
    in_loop_context = saved_in_loop;
    
    // Exit for-loop scope
    if (current_symbols) {
        current_symbols->exit_scope();
    }
}

void SemanticAnalyzer::analyze_return_stmt(const AstNodePtr &node) {
    auto &data = node->as<ReturnStmtNodeData>();
    
    // Check if we're inside a function
    if (!current_function_return_type) {
        reporter.report(DiagnosticSeverity::Error, 
            "Return statement outside function", 
            node->range);
        return;
    }
    
    if (data.expression) {
        // Return with expression - first check if function is void
        if (current_function_return_type && 
            current_function_return_type->category == TypeCategory::Builtin) {
            auto builtin_kind = std::get<BuiltinTypeKind>(current_function_return_type->payload);
            if (builtin_kind == BuiltinTypeKind::Void) {
                reporter.report(DiagnosticSeverity::Error, 
                    "Return statement with value in void function", 
                    node->range);
                return; // Don't check type compatibility for void functions
            }
        }
        
        // Return with expression - check type compatibility for non-void functions
        TypePtr expr_type = infer_expression_type(data.expression);
        if (expr_type && current_function_return_type) {
            if (!is_assignment_compatible(current_function_return_type, expr_type)) {
                reporter.report(DiagnosticSeverity::Error, 
                    "Return type mismatch: expected " + 
                    type_to_string(current_function_return_type) + 
                    " but got " + type_to_string(expr_type), 
                    node->range);
            }
        }
    } else {
        // Return without expression - check if function returns void
        if (current_function_return_type && 
            current_function_return_type->category == TypeCategory::Builtin) {
            auto builtin_kind = std::get<BuiltinTypeKind>(current_function_return_type->payload);
            if (builtin_kind != BuiltinTypeKind::Void) {
                reporter.report(DiagnosticSeverity::Error, 
                    "Return statement without value in non-void function", 
                    node->range);
            }
        }
    }
}

void SemanticAnalyzer::analyze_break_stmt(const AstNodePtr &node) {
    if (!in_loop_context && !in_switch_context) {
        reporter.report(DiagnosticSeverity::Error, 
            "Break statement not within loop or switch", 
            node->range);
    }
}

void SemanticAnalyzer::analyze_continue_stmt(const AstNodePtr &node) {
    if (!in_loop_context) {
        reporter.report(DiagnosticSeverity::Error, 
            "Continue statement not within loop", 
            node->range);
    }
}

void SemanticAnalyzer::analyze_goto_stmt(const AstNodePtr &node) {
    auto &data = node->as<GotoStmtNodeData>();
    
    // Record that this label is referenced (for later validation)
    referenced_labels.insert(data.label);
}

void SemanticAnalyzer::analyze_label_stmt(const AstNodePtr &node) {
    auto &data = node->as<LabelStmtNodeData>();
    
    // Record that this label is defined
    if (defined_labels.find(data.label) != defined_labels.end()) {
        reporter.report(DiagnosticSeverity::Error, 
            "Label '" + data.label + "' already defined", 
            node->range);
    } else {
        defined_labels.insert(data.label);
    }
    
    // Analyze the labeled statement
    if (data.statement) {
        analyze_statement(data.statement);
    }
}

// IR Generation Methods
std::string SemanticAnalyzer::generate_ir_for_expression(const AstNodePtr &expr) {
    if (!expr) return "";
    
    switch (expr->kind) {
        case AstNodeKind::BinaryExpr:
            return generate_ir_for_binary_expr(expr);
        case AstNodeKind::UnaryExpr:
            return generate_ir_for_unary_expr(expr);
        case AstNodeKind::AssignmentExpr:
            return generate_ir_for_assignment_expr(expr);
        case AstNodeKind::IdentifierExpr:
            return generate_ir_for_identifier_expr(expr);
        case AstNodeKind::LiteralExpr:
            return generate_ir_for_literal_expr(expr);
        default:
            return ""; // Not implemented yet
    }
}

std::string SemanticAnalyzer::generate_ir_for_literal_expr(const AstNodePtr &expr) {
    auto &data = expr->as<LiteralExprNodeData>();
    return data.lexeme; // Return the literal value directly
}

std::string SemanticAnalyzer::generate_ir_for_identifier_expr(const AstNodePtr &expr) {
    auto &data = expr->as<IdentifierExprNodeData>();
    // Return scoped variable name
    return get_scoped_variable_name(data.name, data.symbol);
}

std::string SemanticAnalyzer::generate_ir_for_binary_expr(const AstNodePtr &expr) {
    auto &data = expr->as<BinaryExprNodeData>();
    
    // Generate IR for operands
    std::string left_result = generate_ir_for_expression(data.lhs);
    std::string right_result = generate_ir_for_expression(data.rhs);
    
    // Generate a temporary for the result
    std::string temp = ir_gen.new_temp();
    
    // Map C operators to IR opcodes
    IROpcode opcode;
    if (data.op == "+") opcode = IROpcode::ADD;
    else if (data.op == "-") opcode = IROpcode::SUB;
    else if (data.op == "*") opcode = IROpcode::MUL;
    else if (data.op == "/") opcode = IROpcode::DIV;
    else if (data.op == "%") opcode = IROpcode::MOD;
    else if (data.op == "&&") opcode = IROpcode::AND;
    else if (data.op == "||") opcode = IROpcode::OR;
    else if (data.op == "&") opcode = IROpcode::BIT_AND;
    else if (data.op == "|") opcode = IROpcode::BIT_OR;
    else if (data.op == "^") opcode = IROpcode::BIT_XOR;
    else if (data.op == "<<") opcode = IROpcode::LEFT_SHIFT;
    else if (data.op == ">>") opcode = IROpcode::RIGHT_SHIFT;
    else if (data.op == "==") opcode = IROpcode::EQ;
    else if (data.op == "!=") opcode = IROpcode::NE;
    else if (data.op == "<") opcode = IROpcode::LT;
    else if (data.op == "<=") opcode = IROpcode::LE;
    else if (data.op == ">") opcode = IROpcode::GT;
    else if (data.op == ">=") opcode = IROpcode::GE;
    else {
        // Unknown operator, use ADD as fallback
        opcode = IROpcode::ADD;
    }
    
    // Emit the instruction
    ir_gen.emit(opcode, temp, left_result, right_result, "", expr->range.begin.line);
    
    return temp;
}

std::string SemanticAnalyzer::generate_ir_for_unary_expr(const AstNodePtr &expr) {
    auto &data = expr->as<UnaryExprNodeData>();
    
    // Generate IR for operand
    std::string operand_result = generate_ir_for_expression(data.operand);
    
    // Generate a temporary for the result
    std::string temp = ir_gen.new_temp();
    
    // Map C operators to IR opcodes
    if (data.op == "-") {
        // Unary minus: temp = SUB 0, operand
        ir_gen.emit(IROpcode::SUB, temp, "0", operand_result);
    } else if (data.op == "+") {
        // Unary plus: temp = ADD 0, operand
        ir_gen.emit(IROpcode::ADD, temp, "0", operand_result);
    } else if (data.op == "!") {
        // Logical not
        ir_gen.emit(IROpcode::NOT, temp, operand_result);
    } else if (data.op == "~") {
        // Bitwise not
        ir_gen.emit(IROpcode::BIT_NOT, temp, operand_result);
    } else {
        // Unknown operator, use ASSIGN as fallback
        ir_gen.emit(IROpcode::ASSIGN, temp, operand_result);
    }
    
    return temp;
}

std::string SemanticAnalyzer::generate_ir_for_assignment_expr(const AstNodePtr &expr) {
    auto &data = expr->as<AssignmentExprNodeData>();
    
    // Generate IR for right-hand side
    std::string rhs_result = generate_ir_for_expression(data.rhs);
    
    // For now, assume LHS is an identifier
    if (data.lhs && data.lhs->kind == AstNodeKind::IdentifierExpr) {
        auto &lhs_data = data.lhs->as<IdentifierExprNodeData>();
        
        // Get scoped variable name for assignment
        std::string scoped_name = get_scoped_variable_name(lhs_data.name, lhs_data.symbol);
        
        // Simple assignment: var = rhs
        ir_gen.emit(IROpcode::ASSIGN, scoped_name, rhs_result, "", "", expr->range.begin.line);
        
        return scoped_name; // Assignment returns the assigned value
    }
    
    return ""; // Couldn't generate IR for this assignment
}

std::string SemanticAnalyzer::generate_simple_expression_string(const AstNodePtr &expr) {
    if (!expr) return "";
    
    switch (expr->kind) {
        case AstNodeKind::LiteralExpr: {
            auto &data = expr->as<LiteralExprNodeData>();
            return data.lexeme;
        }
        case AstNodeKind::IdentifierExpr: {
            auto &data = expr->as<IdentifierExprNodeData>();
            return data.name;
        }
        case AstNodeKind::BinaryExpr: {
            auto &data = expr->as<BinaryExprNodeData>();
            std::string left = generate_simple_expression_string(data.lhs);
            std::string right = generate_simple_expression_string(data.rhs);
            return left + " " + data.op + " " + right;
        }
        case AstNodeKind::UnaryExpr: {
            auto &data = expr->as<UnaryExprNodeData>();
            std::string operand = generate_simple_expression_string(data.operand);
            return data.op + operand;
        }
        default:
            return "";
    }
}

std::string SemanticAnalyzer::get_scoped_variable_name(const std::string& name, const SymbolWeakPtr& symbol) {
    if (auto sym = symbol.lock()) {
        // Create scoped name: variable_scopeLevel
        return name + "_" + std::to_string(sym->scope_level);
    }
    // Fallback to original name if symbol is not available
    return name;
}
