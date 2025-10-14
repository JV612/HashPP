%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include "symbol_table.h"
#include "symbol.h"
#include "ast.h"
#include "type.h"
#include "semantic_analyzer.h"
using namespace std;

struct SimpleSymbol
{
    char* name;
    char* type;
    char* category;
    int line;
    int scope;
};

enum TypeKind {
    TYPE_KIND_BUILTIN,
    TYPE_KIND_TYPEDEF,
    TYPE_KIND_TAG_STRUCT,
    TYPE_KIND_TAG_CLASS,
    TYPE_KIND_TAG_ENUM,
    TYPE_KIND_FORWARD_DECL
};

void types_init(void);
void types_free(void);
void types_enter_scope(void);
void types_leave_scope(void);
void types_add_builtin(const char *name);
void types_add_typedef(const char *name);
void types_add_tag(const char *name, TypeKind kind);
void types_mark_defined(const char *name, TypeKind concrete_kind);
bool types_is_known(const char *name);
bool types_is_typedef(const char *name);
bool types_is_complete(const char *name);

// Keep the old vector for printing, but add proper symbol table
vector<SimpleSymbol> symbol_table;
SymbolTable* proper_symbol_table = nullptr;
static std::vector<AstNodePtr> g_ast_pool;
static std::unordered_map<AstNode*, AstNodePtr> g_ast_pool_map;
static AstNodePtr g_translation_unit_ast;
bool has_redefinition_error = false;

// Helper function to check for function redefinition
bool check_function_redefinition(const char* func_name, int line) {
    if (!func_name || !proper_symbol_table) return false;
    
    // Look up function in all scopes (global lookup)
    SymbolPtr existing = proper_symbol_table->lookup_ident(func_name);
    if (existing && existing->kind == SymbolKind::Function) {
        fprintf(stderr, "Error: Function '%s' redefined at line %d (previously defined at line %d)\n", 
                func_name, line, existing->line_declared);
        has_redefinition_error = true;
        return true;
    }
    return false;
}



static void ast_reset_pool();
static AstNode* ast_make_type_specifier(const std::string &type_str);
static AstNode* ast_make_identifier_type_specifier(const std::string &name);
static AstNode* ast_make_variable_decl(const std::string &name, AstNode* type_node, AstNode* init_node, bool is_typedef);
static AstNode* ast_make_parameter_decl(const std::string &name, AstNode* type_node);
static AstNode* ast_make_initializer_list();
static void ast_initializer_list_append(AstNode* list_node, AstNode* element_node);
static AstNode* ast_make_compound_stmt();
static AstNode* ast_make_function_decl(const std::string &name, AstNode* return_type_node, std::vector<AstNodePtr> params, AstNode* body_node, bool is_definition, bool is_variadic);
static AstNode* ast_make_struct_decl(const std::string &name, std::vector<AstNodePtr> members, bool is_definition);
static AstNode* ast_make_union_decl(const std::string &name, std::vector<AstNodePtr> members, bool is_definition);
static AstNode* ast_make_enum_decl(const std::string &name, std::vector<AstNodePtr> enumerators, bool is_definition);
static AstNode* ast_make_member_decl(const std::string &name, AstNode* type_node);
static AstNode* ast_make_enumerator_decl(const std::string &name, AstNode* value_node);
static AstNode* ast_make_member_access_expr(AstNode* object, const std::string &member_name, bool is_arrow);
static void ast_translation_unit_append(AstNode* tu_node, AstNode* child_node);
static std::string ast_clean_type_string(const std::string &spec);
static std::string ast_trim(const std::string &s);
static AstNode* ast_make_identifier_expr(const std::string &name);
static AstNode* ast_make_literal_expr(LiteralKind kind, const std::string &lexeme);
static AstNode* ast_make_integer_literal(int value);
static AstNode* ast_make_floating_literal(double value);
static AstNode* ast_make_char_literal(char value);
static AstNode* ast_make_bool_literal(bool value);
static AstNode* ast_make_null_literal();
static AstNode* ast_make_nullptr_literal();
static AstNode* ast_make_binary_expr(const std::string &op, AstNode* lhs, AstNode* rhs);
static AstNode* ast_make_unary_expr(const std::string &op, AstNode* operand, bool is_prefix);
static AstNode* ast_make_assignment_expr(const std::string &op, AstNode* lhs, AstNode* rhs);
static AstNode* ast_make_conditional_expr(AstNode* condition, AstNode* then_expr, AstNode* else_expr);
static AstNode* ast_make_call_expr(AstNode* callee, AstNode* args_list);
static AstNode* ast_make_expression_stmt(AstNode* expr_node);
static AstNode* ast_make_cast_expr(AstNode* type_node, AstNode* expr_node);
static AstNode* ast_make_subscript_expr(AstNode* array, AstNode* index);
static AstNode* ast_make_if_stmt(AstNode* condition, AstNode* then_stmt, AstNode* else_stmt);
static AstNode* ast_make_while_stmt(AstNode* condition, AstNode* body);
static AstNode* ast_make_for_stmt(AstNode* init, AstNode* condition, AstNode* update, AstNode* body);
static AstNode* ast_make_return_stmt(AstNode* value);
static AstNode* ast_make_break_stmt();
static AstNode* ast_make_continue_stmt();
static AstNode* ast_make_goto_stmt(const std::string &label);
static AstNode* ast_make_label_stmt(const std::string &label, AstNode* stmt);
static int evaluate_constant_expression(AstNode* expr, bool* is_valid);
static std::vector<AstNodePtr> ast_list_to_vector(AstNode* list_node);

void add_symbol(const char* name, const char* type, const char* category, int line) {
    // Get scope level from proper symbol table
    int scope_level = proper_symbol_table ? proper_symbol_table->current_scope_level() : 0;
    
    // Add to old table for printing
    symbol_table.push_back({strdup(name), strdup(type), strdup(category), line, scope_level});
    
    // Add to proper symbol table with error checking
    if (proper_symbol_table && name) {
        auto sym = std::make_shared<::Symbol>();
        sym->name = name;
        sym->line_declared = line;
        sym->scope_level = scope_level;
        
        if (strcmp(category, "variable") == 0 || strcmp(category, "pointer") == 0 || strcmp(category, "reference") == 0) {
            sym->kind = SymbolKind::Variable;
            InsertResult result = proper_symbol_table->insert_ident(sym);
            if (result == InsertResult::RedeclaredInSameScope) {
                fprintf(stderr, "Error: Variable '%s' redeclared at line %d\n", name, line);
                // Don't use YYABORT here since we're in a function, not in grammar action
                // Instead, set a flag or handle differently
            }
        } else if (strcmp(category, "function") == 0) {
            sym->kind = SymbolKind::Function;
            InsertResult result = proper_symbol_table->insert_ident(sym);
            if (result == InsertResult::RedeclaredInSameScope) {
                fprintf(stderr, "Error: Function '%s' redeclared at line %d\n", name, line);
                has_redefinition_error = true;
            }
        } else if (strcmp(category, "typedef") == 0) {
            sym->kind = SymbolKind::TypedefName;
            sym->storage = StorageClass::Typedef_;
            InsertResult result = proper_symbol_table->insert_typedef(sym);
            if (result == InsertResult::RedeclaredInSameScope) {
                fprintf(stderr, "Error: Typedef '%s' redeclared at line %d\n", name, line);
            }
        } else if (strcmp(category, "class") == 0 || strcmp(category, "struct") == 0 || 
                   strcmp(category, "union") == 0 || strcmp(category, "enum") == 0) {
            sym->kind = SymbolKind::StructTag;
            InsertResult result = proper_symbol_table->insert_tag(sym);
            if (result == InsertResult::RedeclaredInSameScope) {
                fprintf(stderr, "Error: Tag '%s' redeclared at line %d\n", name, line);
            }
        }
    }
}

string current_type_str;
char current_type[128];
static char saved_decl_type[128];
bool isp=false;
bool isr=false;
int pointer_count = 0;
bool declarator_is_array = false;
std::vector<int> declarator_array_dimensions;

// Track parameters during function declaration parsing
std::vector<AstNodePtr> current_param_list;
bool current_func_is_variadic = false;

// Track struct/union members and enum enumerators
std::vector<AstNodePtr> current_member_list;
std::vector<AstNodePtr> current_enumerator_list;

// Track current access level for class members
int current_access_level = 1; // 0=public, 1=private, 2=protected (default private for class)
bool inside_struct_or_class = false; // Track if we're currently parsing struct/class members

void update_current_type() {
    strncpy(current_type, current_type_str.c_str(), sizeof(current_type)-1);
    current_type[sizeof(current_type)-1] = '\0';
}

bool need_type_reset = true;

void append_type_specifier(const string& spec) {
    if (need_type_reset) {
        current_type_str = spec;
        need_type_reset = false;
    } else {
        current_type_str += " " + spec;
    }
    update_current_type();
}

// Helper function to extract type name from AST node
std::string extract_type_name(const AstNodePtr& type_node) {
    if (!type_node) return "unknown";
    
    if (type_node->kind == AstNodeKind::TypeSpecifier) {
        auto &type_data = type_node->as<TypeSpecifierNodeData>();
        return type_data.name;
    }
    
    return "unknown";
}

// Helper function to extract full parameter type including pointers
std::string extract_parameter_type(const AstNodePtr& param_node) {
    if (!param_node || param_node->kind != AstNodeKind::ParameterDecl) {
        return "unknown";
    }
    
    auto &param_data = param_node->as<ParameterDeclNodeData>();
    std::string base_type = extract_type_name(param_data.type_expr);
    
    // Add pointer indicators
    for (int i = 0; i < param_data.pointer_levels; i++) {
        base_type += "*";
    }
    
    return base_type;
}

// Function overloading support
std::string generate_function_signature(const std::string& func_name, const std::vector<AstNodePtr>& params) {
    std::string signature = func_name + "(";
    for (size_t i = 0; i < params.size(); i++) {
        if (i > 0) signature += ",";
        // Extract parameter type from AST node including pointer levels
        if (params[i] && params[i]->kind == AstNodeKind::ParameterDecl) {
            std::string type_name = extract_parameter_type(params[i]);
            signature += type_name;
        }
    }
    signature += ")";
    return signature;
}

bool functions_have_same_signature(const SymbolPtr& sym1, const SymbolPtr& sym2) {
    if (sym1->kind != SymbolKind::Function || sym2->kind != SymbolKind::Function) {
        return false;
    }
    return sym1->function_signature == sym2->function_signature;
}

bool check_function_overload(const char* func_name, const std::vector<AstNodePtr>& params, int line) {
    if (!func_name || !proper_symbol_table) return false;
    
    std::string new_signature = generate_function_signature(func_name, params);
    
    // Look up all functions with this name
    SymbolPtr existing = proper_symbol_table->lookup_ident(func_name);
    if (existing && existing->kind == SymbolKind::Function) {
        if (existing->function_signature == new_signature) {
            fprintf(stderr, "Error: Function '%s' with signature '%s' redefined at line %d (previously defined at line %d)\n", 
                    func_name, new_signature.c_str(), line, existing->line_declared);
            has_redefinition_error = true;
            return true;
        }
        // Different signature - this is a valid overload
    }
    return false;
}

// Add function symbol with overloading support
void add_function_symbol(const char* func_name, const char* return_type, int line) {
    if (!func_name || !proper_symbol_table) return;
    
    // Generate function signature using current parameter list
    std::string signature = generate_function_signature(func_name, current_param_list);
    
    // Check for exact signature match (redefinition) by looking up the signature as the key
    SymbolPtr existing = proper_symbol_table->lookup_ident(signature);
    if (existing && existing->kind == SymbolKind::Function) {
        fprintf(stderr, "Error: Function '%s' with signature '%s' redefined at line %d (previously defined at line %d)\n", 
                func_name, signature.c_str(), line, existing->line_declared);
        has_redefinition_error = true;
        return;
    }
    
    // Create new function symbol with full signature as name
    auto sym = std::make_shared<::Symbol>();
    sym->name = signature; // Use signature as the key for symbol table
    sym->kind = SymbolKind::Function;
    sym->line_declared = line;
    sym->scope_level = proper_symbol_table->current_scope_level();
    sym->function_signature = signature;
    
    // Create and store the function type information
    // First, create parameter types
    std::vector<TypePtr> param_types;
    for (const auto& param : current_param_list) {
        if (param && param->kind == AstNodeKind::ParameterDecl) {
            std::string type_name = extract_parameter_type(param);
            sym->parameter_types.push_back(type_name);
            
            // Create TypePtr for this parameter - handle pointers
            TypePtr base_type;
            std::string base_name = type_name;
            int pointer_count = 0;
            
            // Count and remove pointer stars
            while (!base_name.empty() && base_name.back() == '*') {
                pointer_count++;
                base_name.pop_back();
            }
            
            // Create base type
            if (base_name == "int") {
                base_type = make_builtin_type(BuiltinTypeKind::Int);
            } else if (base_name == "char") {
                base_type = make_builtin_type(BuiltinTypeKind::Char);
            } else if (base_name == "bool") {
                base_type = make_builtin_type(BuiltinTypeKind::Bool);
            } else if (base_name == "double") {
                base_type = make_builtin_type(BuiltinTypeKind::Double);
            } else if (base_name == "double") {
                base_type = make_builtin_type(BuiltinTypeKind::Double);
            } else if (base_name == "void") {
                base_type = make_builtin_type(BuiltinTypeKind::Void);
            } else {
                // Default to int for unknown types
                base_type = make_builtin_type(BuiltinTypeKind::Int);
            }
            
            // Wrap in pointer types if needed
            TypePtr final_type = base_type;
            for (int i = 0; i < pointer_count; i++) {
                final_type = make_pointer_type(final_type);
            }
            
            param_types.push_back(final_type);
        }
    }
    
    // Create return type - simplified to int for now
    TypePtr return_type_ptr = make_builtin_type(BuiltinTypeKind::Int);
    
    // Create the function type
    sym->type = make_function_type(return_type_ptr, param_types, false); // false = not variadic
    
    // Add to old table for printing (use original function name for display)
    int scope_level = proper_symbol_table ? proper_symbol_table->current_scope_level() : 0;
    symbol_table.push_back({strdup(func_name), strdup(return_type), strdup("function"), line, scope_level});
    
    // Insert into proper symbol table using signature as key
    InsertResult result = proper_symbol_table->insert_ident(sym);
    if (result == InsertResult::RedeclaredInSameScope) {
        // This shouldn't happen since we checked above, but handle it
        fprintf(stderr, "Error: Function signature collision for '%s' at line %d\n", func_name, line);
        has_redefinition_error = true;
    }
    
    // Also add a base symbol for the function name to enable identifier resolution
    // This will be used for semantic analysis to find that the function exists
    SymbolPtr base_sym = proper_symbol_table->lookup_ident(func_name);
    if (!base_sym) {
        // Create a base symbol for the function name that points to any overload
        auto base = std::make_shared<::Symbol>();
        base->name = func_name;
        base->kind = SymbolKind::Function;
        base->line_declared = line;
        base->scope_level = proper_symbol_table->current_scope_level();
        base->function_signature = signature; // Point to first overload
        proper_symbol_table->insert_ident(base);
    }
}

void reset_current_type() {
    current_type_str.clear();
    current_type[0] = '\0';
    need_type_reset = true;  
}

void force_reset_type() {
    reset_current_type();
}

int yylex(void);
void yyerror(const char *s);
extern int yylineno;

bool is_variable_declared(const char* name) {
    // Use the proper scope-aware symbol table only
    if (proper_symbol_table && name) {
        SymbolPtr sym = proper_symbol_table->lookup_ident(name);
        if (sym && (sym->kind == SymbolKind::Variable)) {
            return true;
        }
    }
    return false;
}

bool is_variable_already_declared_in_current_scope(const char* name) {
    if (!name) return false;
    if (proper_symbol_table) {
        SymbolPtr sym = proper_symbol_table->lookup_ident_current(name);
        return (bool)sym;
    }
    return false;
}

static void ast_reset_pool() {
    g_ast_pool.clear();
    g_ast_pool_map.clear();
    g_translation_unit_ast.reset();
}

static AstNode* ast_pool_create(AstNodeKind kind) {
    auto node_ptr = std::make_shared<AstNode>(kind);
    AstNode* raw = node_ptr.get();
    g_ast_pool_map[raw] = node_ptr;
    g_ast_pool.push_back(std::move(node_ptr));
    return raw;
}

static AstNodePtr ast_ptr_from_raw(AstNode* raw) {
    if (!raw) return nullptr;
    auto it = g_ast_pool_map.find(raw);
    if (it != g_ast_pool_map.end()) {
        return it->second;
    }
    return nullptr;
}

static std::string ast_trim(const std::string &s) {
    const char* whitespace = " \t\n\r";
    auto start = s.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(whitespace);
    return s.substr(start, end - start + 1);
}

static std::string ast_clean_type_string(const std::string &spec) {
    std::string trimmed = ast_trim(spec);
    const std::string typedef_kw = "typedef";
    if (trimmed.compare(0, typedef_kw.size(), typedef_kw) == 0) {
        trimmed = ast_trim(trimmed.substr(typedef_kw.size()));
    }
    return trimmed;
}

static AstNode* ast_make_type_specifier(const std::string &type_str) {
    AstNode* node = ast_pool_create(AstNodeKind::TypeSpecifier);
    node->payload = TypeSpecifierNodeData{};
    auto &data = node->as<TypeSpecifierNodeData>();
    data.kind = TypeSpecifierKind::Builtin;
    data.name = ast_clean_type_string(type_str);
    if (data.name.empty()) {
        data.name = "-";
    }
    return node;
}

static AstNode* ast_make_identifier_type_specifier(const std::string &name) {
    AstNode* node = ast_pool_create(AstNodeKind::TypeSpecifier);
    node->payload = TypeSpecifierNodeData{};
    auto &data = node->as<TypeSpecifierNodeData>();
    data.kind = TypeSpecifierKind::Identifier;
    data.name = name;
    return node;
}

static AstNode* ast_make_initializer_list() {
    AstNode* node = ast_pool_create(AstNodeKind::InitializerList);
    node->payload = InitializerListNodeData{};
    return node;
}

static void ast_initializer_list_append(AstNode* list_node, AstNode* element_node) {
    if (!list_node || !element_node) return;
    auto &elements = list_node->as<InitializerListNodeData>().elements;
    auto elem_ptr = ast_ptr_from_raw(element_node);
    if (elem_ptr) {
        elements.push_back(elem_ptr);
    }
}

static AstNode* ast_make_variable_decl(const std::string &name, AstNode* type_node, AstNode* init_node, bool is_typedef) {
    AstNode* node = ast_pool_create(AstNodeKind::VariableDecl);
    node->payload = VariableDeclNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0; // Column info not available in basic lexer
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<VariableDeclNodeData>();
    data.name = name;
    data.type_expr = ast_ptr_from_raw(type_node);
    data.initializer = ast_ptr_from_raw(init_node);
    data.is_typedef = is_typedef;
    data.is_static = current_type_str.find("static") != std::string::npos;
    data.is_extern = current_type_str.find("extern") != std::string::npos;
    data.pointer_levels = pointer_count; // Capture pointer indirection count
    data.is_array = declarator_is_array; // Capture array flag
    data.array_dimensions = declarator_array_dimensions; // Capture all array dimensions
    if (proper_symbol_table) {
        SymbolPtr sym;
        if (is_typedef) {
            sym = proper_symbol_table->lookup_typedef_current(name);
        } else {
            sym = proper_symbol_table->lookup_ident_current(name);
        }
        if (sym) {
            data.symbol = sym;
        }
    }
    return node;
}

static AstNode* ast_make_parameter_decl(const std::string &name, AstNode* type_node) {
    AstNode* node = ast_pool_create(AstNodeKind::ParameterDecl);
    node->payload = ParameterDeclNodeData{};
    auto &data = node->as<ParameterDeclNodeData>();
    data.name = name;
    data.type_expr = ast_ptr_from_raw(type_node);
    data.pointer_levels = pointer_count;
    data.is_array = declarator_is_array;
    data.array_dimensions = declarator_array_dimensions;
    if (proper_symbol_table) {
        SymbolPtr sym = proper_symbol_table->lookup_ident_current(name);
        if (sym) {
            data.symbol = sym;
        }
    }
    return node;
}

static AstNode* ast_make_compound_stmt() {
    AstNode* node = ast_pool_create(AstNodeKind::CompoundStmt);
    node->payload = CompoundStmtNodeData{};
    return node;
}

static AstNode* ast_make_function_decl(const std::string &name, AstNode* return_type_node, std::vector<AstNodePtr> params, AstNode* body_node, bool is_definition, bool is_variadic) {
    AstNode* node = ast_pool_create(AstNodeKind::FunctionDecl);
    node->payload = FunctionDeclNodeData{};
    auto &data = node->as<FunctionDeclNodeData>();
    data.name = name;
    data.return_type = ast_ptr_from_raw(return_type_node);
    data.parameters = std::move(params);
    data.body = ast_ptr_from_raw(body_node);
    data.is_definition = is_definition;
    data.is_variadic = is_variadic;
    if (proper_symbol_table) {
        SymbolPtr sym = proper_symbol_table->lookup_ident(name);
        if (sym) {
            data.symbol = sym;
        }
    }
    return node;
}

static AstNode* ast_make_struct_decl(const std::string &name, std::vector<AstNodePtr> members, bool is_definition) {
    AstNode* node = ast_pool_create(AstNodeKind::StructDecl);
    node->payload = StructDeclNodeData{};
    auto &data = node->as<StructDeclNodeData>();
    data.name = name;
    data.members = std::move(members);
    data.is_definition = is_definition;
    if (proper_symbol_table && !name.empty()) {
        SymbolPtr sym = proper_symbol_table->lookup_tag_current(name);
        if (sym) {
            data.symbol = sym;
        }
    }
    return node;
}

static AstNode* ast_make_union_decl(const std::string &name, std::vector<AstNodePtr> members, bool is_definition) {
    AstNode* node = ast_pool_create(AstNodeKind::UnionDecl);
    node->payload = UnionDeclNodeData{};
    auto &data = node->as<UnionDeclNodeData>();
    data.name = name;
    data.members = std::move(members);
    data.is_definition = is_definition;
    if (proper_symbol_table && !name.empty()) {
        SymbolPtr sym = proper_symbol_table->lookup_tag_current(name);
        if (sym) {
            data.symbol = sym;
        }
    }
    return node;
}

static AstNode* ast_make_enum_decl(const std::string &name, std::vector<AstNodePtr> enumerators, bool is_definition) {
    AstNode* node = ast_pool_create(AstNodeKind::EnumDecl);
    node->payload = EnumDeclNodeData{};
    auto &data = node->as<EnumDeclNodeData>();
    data.name = name;
    data.enumerators = std::move(enumerators);
    data.is_definition = is_definition;
    if (proper_symbol_table && !name.empty()) {
        SymbolPtr sym = proper_symbol_table->lookup_tag_current(name);
        if (sym) {
            data.symbol = sym;
        }
    }
    return node;
}

static AstNode* ast_make_member_decl(const std::string &name, AstNode* type_node) {
    AstNode* node = ast_pool_create(AstNodeKind::MemberDecl);
    node->payload = MemberDeclNodeData{};
    auto &data = node->as<MemberDeclNodeData>();
    data.name = name;
    data.type_expr = ast_ptr_from_raw(type_node);
    data.pointer_levels = pointer_count;
    data.is_array = declarator_is_array;
    data.array_dimensions = declarator_array_dimensions;
    
    // Set access level based on current context
    if (current_access_level == 0) {
        data.access = AccessSpecifier::Public;
    } else if (current_access_level == 1) {
        data.access = AccessSpecifier::Private;
    } else {
        data.access = AccessSpecifier::Protected;
    }
    
    return node;
}

static AstNode* ast_make_enumerator_decl(const std::string &name, AstNode* value_node) {
    AstNode* node = ast_pool_create(AstNodeKind::EnumeratorDecl);
    node->payload = EnumeratorDeclNodeData{};
    auto &data = node->as<EnumeratorDeclNodeData>();
    data.name = name;
    data.value_expr = ast_ptr_from_raw(value_node);
    return node;
}

static AstNode* ast_make_member_access_expr(AstNode* object, const std::string &member_name, bool is_arrow) {
    AstNode* node = ast_pool_create(AstNodeKind::MemberAccessExpr);
    node->payload = MemberAccessExprNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<MemberAccessExprNodeData>();
    data.object = ast_ptr_from_raw(object);
    data.member_name = member_name;
    data.is_arrow = is_arrow;
    return node;
}

static void ast_translation_unit_append(AstNode* tu_node, AstNode* child_node) {
    if (!tu_node || !child_node) return;
    auto &decls = tu_node->as<TranslationUnitNodeData>().declarations;
    if (child_node->kind == AstNodeKind::InitializerList) {
        const auto &elements = child_node->as<InitializerListNodeData>().elements;
        decls.insert(decls.end(), elements.begin(), elements.end());
    } else {
        auto ptr = ast_ptr_from_raw(child_node);
        if (ptr) decls.push_back(ptr);
    }
}

static AstNode* ast_make_identifier_expr(const std::string &name) {
    AstNode* node = ast_pool_create(AstNodeKind::IdentifierExpr);
    node->payload = IdentifierExprNodeData{};
    auto &data = node->as<IdentifierExprNodeData>();
    data.name = name;
    if (proper_symbol_table) {
        if (auto sym = proper_symbol_table->lookup_ident(name)) {
            data.symbol = sym;
        }
    }
    return node;
}

static AstNode* ast_make_literal_expr(LiteralKind kind, const std::string &lexeme) {
    AstNode* node = ast_pool_create(AstNodeKind::LiteralExpr);
    node->payload = LiteralExprNodeData{};
    auto &data = node->as<LiteralExprNodeData>();
    data.literal_kind = kind;
    data.lexeme = lexeme;
    return node;
}

static AstNode* ast_make_integer_literal(int value) {
    return ast_make_literal_expr(LiteralKind::Integer, std::to_string(value));
}

static AstNode* ast_make_floating_literal(double value) {
    std::ostringstream oss;
    oss << value;
    return ast_make_literal_expr(LiteralKind::Double, oss.str());
}

static AstNode* ast_make_char_literal(char value) {
    std::string lexeme;
    lexeme.push_back(value);
    return ast_make_literal_expr(LiteralKind::Character, lexeme);
}

static AstNode* ast_make_bool_literal(bool value) {
    return ast_make_literal_expr(LiteralKind::Boolean, value ? "true" : "false");
}

static AstNode* ast_make_null_literal() {
    return ast_make_literal_expr(LiteralKind::Null, "null");
}

static AstNode* ast_make_nullptr_literal() {
    return ast_make_literal_expr(LiteralKind::Nullptr, "nullptr");
}

static AstNode* ast_make_binary_expr(const std::string &op, AstNode* lhs, AstNode* rhs) {
    if (!lhs || !rhs) return nullptr;
    AstNode* node = ast_pool_create(AstNodeKind::BinaryExpr);
    node->payload = BinaryExprNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<BinaryExprNodeData>();
    data.op = op;
    data.lhs = ast_ptr_from_raw(lhs);
    data.rhs = ast_ptr_from_raw(rhs);
    return node;
}

static AstNode* ast_make_unary_expr(const std::string &op, AstNode* operand, bool is_prefix) {
    AstNode* node = ast_pool_create(AstNodeKind::UnaryExpr);
    node->payload = UnaryExprNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<UnaryExprNodeData>();
    data.op = op;
    data.operand = ast_ptr_from_raw(operand);
    data.is_prefix = is_prefix;
    return node;
}

static AstNode* ast_make_assignment_expr(const std::string &op, AstNode* lhs, AstNode* rhs) {
    if (!lhs || !rhs) return nullptr;
    AstNode* node = ast_pool_create(AstNodeKind::AssignmentExpr);
    node->payload = AssignmentExprNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<AssignmentExprNodeData>();
    data.op = op;
    data.lhs = ast_ptr_from_raw(lhs);
    data.rhs = ast_ptr_from_raw(rhs);
    return node;
}

static AstNode* ast_make_conditional_expr(AstNode* condition, AstNode* then_expr, AstNode* else_expr) {
    if (!condition || !then_expr || !else_expr) return nullptr;
    AstNode* node = ast_pool_create(AstNodeKind::ConditionalExpr);
    node->payload = ConditionalExprNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<ConditionalExprNodeData>();
    data.condition = ast_ptr_from_raw(condition);
    data.then_expr = ast_ptr_from_raw(then_expr);
    data.else_expr = ast_ptr_from_raw(else_expr);
    return node;
}

static std::vector<AstNodePtr> ast_list_to_vector(AstNode* list_node) {
    std::vector<AstNodePtr> result;
    if (!list_node) return result;
    if (list_node->kind == AstNodeKind::InitializerList) {
        const auto &elements = list_node->as<InitializerListNodeData>().elements;
        result.insert(result.end(), elements.begin(), elements.end());
    } else {
        auto ptr = ast_ptr_from_raw(list_node);
        if (ptr) result.push_back(ptr);
    }
    return result;
}

static AstNode* ast_make_call_expr(AstNode* callee, AstNode* args_list) {
    if (!callee) return nullptr;
    AstNode* node = ast_pool_create(AstNodeKind::CallExpr);
    node->payload = CallExprNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<CallExprNodeData>();
    data.callee = ast_ptr_from_raw(callee);
    data.arguments = ast_list_to_vector(args_list);
    return node;
}

static AstNode* ast_make_expression_stmt(AstNode* expr_node) {
    if (!expr_node) return nullptr;
    AstNode* node = ast_pool_create(AstNodeKind::ExpressionStmt);
    node->payload = ExpressionStmtNodeData{};
    auto &data = node->as<ExpressionStmtNodeData>();
    data.expression = ast_ptr_from_raw(expr_node);
    return node;
}

static AstNode* ast_make_cast_expr(AstNode* type_node, AstNode* expr_node) {
    if (!expr_node) return nullptr;
    AstNode* node = ast_pool_create(AstNodeKind::CastExpr);
    node->payload = CastExprNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<CastExprNodeData>();
    data.target_type = ast_ptr_from_raw(type_node);
    data.expression = ast_ptr_from_raw(expr_node);
    return node;
}

static AstNode* ast_make_subscript_expr(AstNode* array, AstNode* index) {
    if (!array || !index) return nullptr;
    AstNode* node = ast_pool_create(AstNodeKind::SubscriptExpr);
    node->payload = SubscriptExprNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<SubscriptExprNodeData>();
    data.array = ast_ptr_from_raw(array);
    data.index = ast_ptr_from_raw(index);
    return node;
}

static AstNode* ast_make_if_stmt(AstNode* condition, AstNode* then_stmt, AstNode* else_stmt) {
    if (!condition || !then_stmt) return nullptr;
    AstNode* node = ast_pool_create(AstNodeKind::IfStmt);
    node->payload = IfStmtNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<IfStmtNodeData>();
    data.condition = ast_ptr_from_raw(condition);
    data.then_branch = ast_ptr_from_raw(then_stmt);
    data.else_branch = else_stmt ? ast_ptr_from_raw(else_stmt) : nullptr;
    return node;
}

static AstNode* ast_make_while_stmt(AstNode* condition, AstNode* body) {
    if (!condition || !body) return nullptr;
    AstNode* node = ast_pool_create(AstNodeKind::WhileStmt);
    node->payload = WhileStmtNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<WhileStmtNodeData>();
    data.condition = ast_ptr_from_raw(condition);
    data.body = ast_ptr_from_raw(body);
    return node;
}

static AstNode* ast_make_for_stmt(AstNode* init, AstNode* condition, AstNode* update, AstNode* body) {
    if (!body) return nullptr;
    AstNode* node = ast_pool_create(AstNodeKind::ForStmt);
    node->payload = ForStmtNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<ForStmtNodeData>();
    data.init = init ? ast_ptr_from_raw(init) : nullptr;
    data.condition = condition ? ast_ptr_from_raw(condition) : nullptr;
    data.increment = update ? ast_ptr_from_raw(update) : nullptr;
    data.body = ast_ptr_from_raw(body);
    return node;
}

static AstNode* ast_make_return_stmt(AstNode* value) {
    AstNode* node = ast_pool_create(AstNodeKind::ReturnStmt);
    node->payload = ReturnStmtNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<ReturnStmtNodeData>();
    data.expression = value ? ast_ptr_from_raw(value) : nullptr;
    return node;
}

static AstNode* ast_make_break_stmt() {
    AstNode* node = ast_pool_create(AstNodeKind::BreakStmt);
    node->payload = BreakStmtNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    return node;
}

static AstNode* ast_make_continue_stmt() {
    AstNode* node = ast_pool_create(AstNodeKind::ContinueStmt);
    node->payload = ContinueStmtNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    return node;
}

static AstNode* ast_make_goto_stmt(const std::string &label) {
    AstNode* node = ast_pool_create(AstNodeKind::GotoStmt);
    node->payload = GotoStmtNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<GotoStmtNodeData>();
    data.label = label;
    return node;
}

static AstNode* ast_make_label_stmt(const std::string &label, AstNode* stmt) {
    AstNode* node = ast_pool_create(AstNodeKind::LabelStmt);
    node->payload = LabelStmtNodeData{};
    // Set source range information
    node->range.begin.line = yylineno;
    node->range.begin.column = 0;
    node->range.end.line = yylineno;
    node->range.end.column = 0;
    auto &data = node->as<LabelStmtNodeData>();
    data.label = label;
    data.statement = stmt ? ast_ptr_from_raw(stmt) : nullptr;
    return node;
}

void print_symbol_table() {
    if (symbol_table.empty()) { printf("\n(symbol table empty)\n"); return; }
    printf("\n%-8s %-25s %-15s %-12s %-8s\n", "Line", "Name", "Type", "Category", "Scope");
    printf("---------------------------------------------------------------------------\n");
    for (auto &s : symbol_table) {
        printf("%-8d %-25s %-15s %-12s %-8d\n", s.line, s.name, s.type, s.category, s.scope);
    }
}

static int evaluate_constant_expression(AstNode* expr, bool* is_valid) {
    if (!expr || !is_valid) {
        if (is_valid) *is_valid = false;
        return 0;
    }
    
    *is_valid = true;
    
    switch (expr->kind) {
        case AstNodeKind::LiteralExpr: {
            auto &lit = expr->as<LiteralExprNodeData>();
            if (lit.literal_kind == LiteralKind::Integer) {
                return std::stoi(lit.lexeme);
            } else {
                *is_valid = false;
                return 0;
            }
        }
        case AstNodeKind::BinaryExpr: {
            auto &bin = expr->as<BinaryExprNodeData>();
            bool left_valid, right_valid;
            int left_val = evaluate_constant_expression(bin.lhs.get(), &left_valid);
            int right_val = evaluate_constant_expression(bin.rhs.get(), &right_valid);
            
            if (!left_valid || !right_valid) {
                *is_valid = false;
                return 0;
            }
            
            if (bin.op == "+") return left_val + right_val;
            else if (bin.op == "-") return left_val - right_val;
            else if (bin.op == "*") return left_val * right_val;
            else if (bin.op == "/") {
                if (right_val == 0) {
                    *is_valid = false;
                    return 0;
                }
                return left_val / right_val;
            }
            else if (bin.op == "%") {
                if (right_val == 0) {
                    *is_valid = false;
                    return 0;
                }
                return left_val % right_val;
            }
            else if (bin.op == "^") return left_val ^ right_val;  // Bitwise XOR
            else if (bin.op == "&") return left_val & right_val;  // Bitwise AND
            else if (bin.op == "|") return left_val | right_val;  // Bitwise OR
            else if (bin.op == "<<") return left_val << right_val; // Left shift
            else if (bin.op == ">>") return left_val >> right_val; // Right shift
            else {
                *is_valid = false;
                return 0;
            }
        }
        case AstNodeKind::UnaryExpr: {
            auto &un = expr->as<UnaryExprNodeData>();
            bool operand_valid;
            int operand_val = evaluate_constant_expression(un.operand.get(), &operand_valid);
            
            if (!operand_valid) {
                *is_valid = false;
                return 0;
            }
            
            if (un.op == "+") return operand_val;
            else if (un.op == "-") return -operand_val;
            else {
                *is_valid = false;
                return 0;
            }
        }
        default:
            *is_valid = false;
            return 0;
    }
}

AstNodePtr get_translation_unit_ast() {
    return g_translation_unit_ast;
}

static const char* diagnostic_severity_to_cstr(DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::Note: return "note";
        case DiagnosticSeverity::Warning: return "warning";
        case DiagnosticSeverity::Error: return "error";
    }
    return "message";
}

void print_diagnostics(const DiagnosticReporter &reporter) {
    for (const auto &diag : reporter.diagnostics()) {
        if (diag.range.begin.line > 0) {
            fprintf(stderr, "%s at line %d: %s\n", diagnostic_severity_to_cstr(diag.severity), 
                    diag.range.begin.line, diag.message.c_str());
        } else {
            fprintf(stderr, "%s: %s\n", diagnostic_severity_to_cstr(diag.severity), diag.message.c_str());
        }
    }
}

%}

%code requires {
#include "ast.h"
}

%union {
    char* str;
    int ival;
    double fval;
    char cval;
    void* ptr;
    AstNode* node;
}

%token <str> IDENTIFIER STRING_LITERAL TYPE_NAME
%token <ival> INT_CONSTANT BOOL_TRUE BOOL_FALSE
%token <fval> DOUBLE_CONSTANT
%token <cval> CHAR_CONSTANT
%token <ptr> NULL_CONSTANT NULLPTR_CONSTANT

%token SIZEOF
%token PTR_OP INC_OP DEC_OP LEFT_OP RIGHT_OP LE_OP GE_OP EQ_OP NE_OP
%token AND_OP OR_OP MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN ADD_ASSIGN
%token SUB_ASSIGN LEFT_ASSIGN RIGHT_ASSIGN AND_ASSIGN
%token XOR_ASSIGN OR_ASSIGN

%token TYPEDEF STATIC
%token CHAR INT SIGNED UNSIGNED DOUBLE BOOL CONST VOLATILE VOID 
%token STRUCT UNION ENUM ELLIPSIS

%token CLASS PUBLIC PRIVATE PROTECTED

%token CASE DEFAULT IF ELSE SWITCH WHILE UNTIL DO FOR GOTO CONTINUE BREAK RETURN

%start translation_unit

%type <str> struct_or_union
%type <str> declarator
%type <str> direct_declarator
%type <str> identifier_list

%type <node> parameter_declaration
%type <node> parameter_list
%type <node> parameter_type_list

%type <node> struct_or_union_specifier
%type <node> enum_specifier
%type <node> type_specifier
%type <node> class_specifier
%type <node> class_member
%type <node> class_member_or_access_spec
%type <node> declaration_specifiers
%type <node> struct_declaration_list
%type <node> struct_declaration
%type <node> struct_declarator_list
%type <node> struct_declarator
%type <node> enumerator_list
%type <node> enumerator

%type <node> translation_unit
%type <node> external_declaration
%type <node> declaration
%type <node> init_declarator_list
%type <node> init_declarator
%type <node> function_definition
%type <node> compound_statement
%type <node> statement_list
%type <node> statement
%type <node> labeled_statement
%type <node> selection_statement
%type <node> iteration_statement
%type <node> jump_statement

%type <node> expression
%type <node> assignment_expression
%type <node> conditional_expression
%type <node> logical_or_expression
%type <node> logical_and_expression
%type <node> inclusive_or_expression
%type <node> exclusive_or_expression
%type <node> and_expression
%type <node> equality_expression
%type <node> relational_expression
%type <node> shift_expression
%type <node> additive_expression
%type <node> multiplicative_expression
%type <node> cast_expression
%type <node> unary_expression
%type <node> postfix_expression
%type <node> primary_expression
%type <node> constant
%type <node> argument_expression_list
%type <node> expression_statement
%type <node> initializer
%type <node> initializer_list
%type <node> constant_expression
%type <node> type_name

%type <str> assignment_operator



/* Operator Precedence and Associativity Declarations */
%right  '=' MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN ADD_ASSIGN SUB_ASSIGN LEFT_ASSIGN RIGHT_ASSIGN AND_ASSIGN XOR_ASSIGN OR_ASSIGN
%right  '?' ':'
%left   OR_OP
%left   AND_OP
%left   '|'
%left   '^'
%left   '&'
%left   EQ_OP NE_OP
%left   '<' '>' LE_OP GE_OP
%left   LEFT_OP RIGHT_OP
%left   '+' '-'
%left   '*' '/' '%'
%right  '!' '~' INC_OP DEC_OP
%right UMINUS

%nonassoc THEN
%nonassoc ELSE

%%

primary_expression
    : IDENTIFIER {
        // Don't check if variable is declared during parsing - semantic analysis will do this
        $$ = ast_make_identifier_expr($1 ? std::string($1) : std::string());
    }
	| constant { $$ = $1; }
	| STRING_LITERAL {
          $$ = ast_make_literal_expr(LiteralKind::String, $1 ? std::string($1) : std::string());
      }
	| '(' expression ')' { $$ = $2; }
	;

constant
    : INT_CONSTANT { $$ = ast_make_integer_literal($1); }
    | DOUBLE_CONSTANT { $$ = ast_make_floating_literal($1); }
    | CHAR_CONSTANT { $$ = ast_make_char_literal($1); }
    | BOOL_TRUE    { $$ = ast_make_bool_literal(true); }
    | BOOL_FALSE   { $$ = ast_make_bool_literal(false); }
    | NULL_CONSTANT { $$ = ast_make_null_literal(); }
    | NULLPTR_CONSTANT { $$ = ast_make_nullptr_literal(); }
    ;


postfix_expression
	: primary_expression { $$ = $1; }
	| postfix_expression '[' expression ']' {
          $$ = ast_make_subscript_expr($1, $3);
      }
	| postfix_expression '(' ')' {
          $$ = ast_make_call_expr($1, nullptr);
      }
	| postfix_expression '(' argument_expression_list ')' {
          $$ = ast_make_call_expr($1, $3);
      }
	| postfix_expression '.' IDENTIFIER {
          $$ = ast_make_member_access_expr($1, $3 ? $3 : "", false);
      }
	| postfix_expression PTR_OP IDENTIFIER {
          $$ = ast_make_member_access_expr($1, $3 ? $3 : "", true);
      }
	| postfix_expression INC_OP {
          $$ = ast_make_unary_expr("++", $1, false);
      }
	| postfix_expression DEC_OP {
          $$ = ast_make_unary_expr("--", $1, false);
      }
	;

argument_expression_list
	: assignment_expression {
          AstNode* list = ast_make_initializer_list();
          ast_initializer_list_append(list, $1);
          $$ = list;
      }
	| argument_expression_list ',' assignment_expression {
          AstNode* list = $1 ? $1 : ast_make_initializer_list();
          ast_initializer_list_append(list, $3);
          $$ = list;
      }
	;

unary_expression
    : postfix_expression { $$ = $1; }

| INC_OP unary_expression { $$ = ast_make_unary_expr("++", $2, true); }
| DEC_OP unary_expression { $$ = ast_make_unary_expr("--", $2, true); }
| '&' cast_expression     { $$ = ast_make_unary_expr("&", $2, true); }
| '*' cast_expression     { $$ = ast_make_unary_expr("*", $2, true); }
| '+' cast_expression     { $$ = ast_make_unary_expr("+", $2, true); }
| '-' cast_expression %prec UMINUS { $$ = ast_make_unary_expr("-", $2, true); }
| '~' cast_expression     { $$ = ast_make_unary_expr("~", $2, true); }
| '!' cast_expression     { $$ = ast_make_unary_expr("!", $2, true); }
| SIZEOF unary_expression { $$ = ast_make_unary_expr("sizeof", $2, true); }
| SIZEOF '(' type_name ')' { $$ = ast_make_unary_expr("sizeof", nullptr, true); }
    ;

cast_expression
	: unary_expression { $$ = $1; }
	| '(' type_name ')' cast_expression {
          $$ = ast_make_cast_expr($2, $4);
      }
	;

multiplicative_expression
    : cast_expression { $$ = $1; }
    | multiplicative_expression '*' cast_expression {
          $$ = ast_make_binary_expr("*", $1, $3);
      }
    | multiplicative_expression '/' cast_expression {
          $$ = ast_make_binary_expr("/", $1, $3);
      }
    | multiplicative_expression '%' cast_expression {
          $$ = ast_make_binary_expr("%", $1, $3);
      }
	;

additive_expression
    : multiplicative_expression { $$ = $1; }
    | additive_expression '+' multiplicative_expression {
          $$ = ast_make_binary_expr("+", $1, $3);
      }
    | additive_expression '-' multiplicative_expression {
          $$ = ast_make_binary_expr("-", $1, $3);
      }
	;

shift_expression
    : additive_expression { $$ = $1; }
    | shift_expression LEFT_OP additive_expression {
          $$ = ast_make_binary_expr("<<", $1, $3);
      }
    | shift_expression RIGHT_OP additive_expression {
          $$ = ast_make_binary_expr(">>", $1, $3);
      }
	;

relational_expression
    : shift_expression { $$ = $1; }
    | relational_expression '<' shift_expression {
          $$ = ast_make_binary_expr("<", $1, $3);
      }
    | relational_expression '>' shift_expression {
          $$ = ast_make_binary_expr(">", $1, $3);
      }
    | relational_expression LE_OP shift_expression {
          $$ = ast_make_binary_expr("<=", $1, $3);
      }
    | relational_expression GE_OP shift_expression {
          $$ = ast_make_binary_expr(">=", $1, $3);
      }
	;

equality_expression
    : relational_expression { $$ = $1; }
    | equality_expression EQ_OP relational_expression {
          $$ = ast_make_binary_expr("==", $1, $3);
      }
    | equality_expression NE_OP relational_expression {
          $$ = ast_make_binary_expr("!=", $1, $3);
      }
	;

and_expression
	: equality_expression { $$ = $1; }
	| and_expression '&' equality_expression {
	      $$ = ast_make_binary_expr("&", $1, $3);
	  }
	;

exclusive_or_expression
	: and_expression { $$ = $1; }
	| exclusive_or_expression '^' and_expression {
	      $$ = ast_make_binary_expr("^", $1, $3);
	  }
	;

inclusive_or_expression
	: exclusive_or_expression { $$ = $1; }
	| inclusive_or_expression '|' exclusive_or_expression {
	      $$ = ast_make_binary_expr("|", $1, $3);
	  }
	;

logical_and_expression
	: inclusive_or_expression { $$ = $1; }
	| logical_and_expression AND_OP inclusive_or_expression {
	      $$ = ast_make_binary_expr("&&", $1, $3);
	  }
	;

logical_or_expression
	: logical_and_expression { $$ = $1; }
	| logical_or_expression OR_OP logical_and_expression {
	      $$ = ast_make_binary_expr("||", $1, $3);
	  }
	;

conditional_expression
	: logical_or_expression { $$ = $1; }
	| logical_or_expression '?' expression ':' conditional_expression {
	      $$ = ast_make_conditional_expr($1, $3, $5);
	  }
	;

assignment_expression
    : conditional_expression { $$ = $1; }
    | unary_expression assignment_operator assignment_expression {
          $$ = ast_make_assignment_expr($2 ? std::string($2) : std::string("="), $1, $3);
      }
	;

assignment_operator
    : '='         { $$ = strdup("="); }
    | MUL_ASSIGN  { $$ = strdup("*="); }
    | DIV_ASSIGN  { $$ = strdup("/="); }
    | MOD_ASSIGN  { $$ = strdup("%="); }
    | ADD_ASSIGN  { $$ = strdup("+="); }
    | SUB_ASSIGN  { $$ = strdup("-="); }
    | LEFT_ASSIGN { $$ = strdup("<<="); }
    | RIGHT_ASSIGN { $$ = strdup(">>="); }
    | AND_ASSIGN  { $$ = strdup("&="); }
    | XOR_ASSIGN  { $$ = strdup("^="); }
    | OR_ASSIGN   { $$ = strdup("|="); }
	;

expression
    : assignment_expression { $$ = $1; }
    | expression ',' assignment_expression {
          $$ = ast_make_binary_expr(",", $1, $3);
      }
	;

constant_expression
    : conditional_expression { $$ = $1; }
	;


declaration
	: declaration_specifiers ';' {
          reset_current_type();
          $$ = $1; // Return the declaration_specifiers AST node if it exists
      }
	| declaration_specifiers init_declarator_list ';' {
          // If we're inside a struct/class, the members were already processed by init_declarator
          // and added to current_member_list, so we don't need to return anything
          if (inside_struct_or_class) {
              $$ = nullptr;
          } else {
              $$ = $2;
          }
          reset_current_type();
      }
	;

declaration_specifiers
	: storage_class_specifier { $$ = nullptr; }
	| storage_class_specifier declaration_specifiers { $$ = $2; }
	| type_specifier { $$ = $1; }
	| type_specifier declaration_specifiers { $$ = $1 ? $1 : $2; }
	| type_qualifier { $$ = nullptr; }
	| type_qualifier declaration_specifiers { $$ = $2; }
	;

init_declarator_list
	: init_declarator {
          AstNode* list = nullptr;
          if ($1) {
              list = ast_make_initializer_list();
              ast_initializer_list_append(list, $1);
          }
          $$ = list;
      }
	| init_declarator_list ',' init_declarator {
          AstNode* list = $1;
          if (!list) {
              list = ast_make_initializer_list();
          }
          if ($3) {
              ast_initializer_list_append(list, $3);
          }
          $$ = list;
      }
	;

init_declarator
	: declarator {
          AstNode* result_node = nullptr;
          if ($1) {
              // Check for redeclaration before adding
              if (is_variable_already_declared_in_current_scope($1)) {
                  fprintf(stderr, "Error: Variable '%s' redeclared at line %d\n", $1, yylineno);
                  has_redefinition_error = true;
                  // Continue parsing to find more errors instead of aborting
              }
              
              if (current_type_str.find("typedef") != string::npos) {
                  types_add_typedef($1);
                  add_symbol($1, current_type[0] ? current_type : "-", "typedef", yylineno);
              } else {
                  const char* symbol_category = inside_struct_or_class ? "member" : (isp ? "pointer" : isr ? "reference" : "variable");
                  add_symbol($1, current_type[0] ? current_type : "-", symbol_category, yylineno);
              }

              std::string type_spec = current_type_str;
              AstNode* type_node = ast_make_type_specifier(type_spec);
              bool is_typedef_decl = current_type_str.find("typedef") != string::npos;
              
              if (inside_struct_or_class) {
                  // Create a MemberDecl node and add to current_member_list
                  result_node = ast_make_member_decl($1, type_node);
                  if (result_node) {
                      current_member_list.push_back(ast_ptr_from_raw(result_node));
                  }
              } else {
                  // Create a VariableDecl node as usual
                  result_node = ast_make_variable_decl($1, type_node, nullptr, is_typedef_decl);
              }
          }
          $$ = result_node;
      }
	| declarator '=' initializer {
          AstNode* result_node = nullptr;
          if ($1) {
              // Check for redeclaration before adding
              if (is_variable_already_declared_in_current_scope($1)) {
                  fprintf(stderr, "Error: Variable '%s' redeclared at line %d\n", $1, yylineno);
                  has_redefinition_error = true;
                  // Continue parsing to find more errors instead of aborting
              }
              
              if (current_type_str.find("typedef") != string::npos) {
                  types_add_typedef($1);
                  add_symbol($1, current_type[0] ? current_type : "-", "typedef", yylineno);
              } else {
                  const char* symbol_category = inside_struct_or_class ? "member" : (isp ? "pointer" : isr ? "reference" : "variable");
                  add_symbol($1, current_type[0] ? current_type : "-", symbol_category, yylineno);
              }

              std::string type_spec = current_type_str;
              AstNode* type_node = ast_make_type_specifier(type_spec);
              bool is_typedef_decl = current_type_str.find("typedef") != string::npos;
              
              if (inside_struct_or_class) {
                  // Create a MemberDecl node and add to current_member_list
                  // Note: Member initializers are not typically supported in C++, but we'll create the node anyway
                  result_node = ast_make_member_decl($1, type_node);
                  if (result_node) {
                      current_member_list.push_back(ast_ptr_from_raw(result_node));
                  }
              } else {
                  // Create a VariableDecl node as usual
                  result_node = ast_make_variable_decl($1, type_node, $3, is_typedef_decl);
              }
          }
          $$ = result_node;
      }
	;


storage_class_specifier
	: TYPEDEF { append_type_specifier("typedef"); }
	| STATIC  { append_type_specifier("static"); }
	;

type_specifier
    : VOID       { append_type_specifier("void"); $$ = ast_make_type_specifier("void"); }
    | CHAR       { append_type_specifier("char"); $$ = ast_make_type_specifier("char"); }
    | INT        { append_type_specifier("int"); $$ = ast_make_type_specifier("int"); }
    | BOOL       { append_type_specifier("bool"); $$ = ast_make_type_specifier("bool"); }
    | DOUBLE     { append_type_specifier("double"); $$ = ast_make_type_specifier("double"); }
    | SIGNED     { append_type_specifier("signed"); $$ = ast_make_type_specifier("signed"); }
    | UNSIGNED   { append_type_specifier("unsigned"); $$ = ast_make_type_specifier("unsigned"); }
    | struct_or_union_specifier { $$ = $1; }
    | enum_specifier { $$ = $1; }
    | TYPE_NAME  { 
          if ($1) {
              append_type_specifier($1);
          }
          $$ = ast_make_identifier_type_specifier($1 ? $1 : "");
      }
    | class_specifier { $$ = $1; }
    | CLASS IDENTIFIER {
          if ($2) {
              append_type_specifier(string("class ") + $2);
              add_symbol($2, "class", "class", yylineno);
              types_add_tag($2, TYPE_KIND_TAG_CLASS);
          }
      }
    ;

class_specifier
    : CLASS IDENTIFIER '{' { 
          // Add the class as a forward declaration immediately so it can be used in self-referential pointers
          if ($2) {
              add_symbol($2, "class", "class", yylineno);
              types_add_tag($2, TYPE_KIND_TAG_CLASS);
          }
          types_enter_scope(); 
          reset_current_type(); 
          current_member_list.clear();
          current_access_level = 1; // Default to private for classes
          inside_struct_or_class = true; // We're now parsing class members
      } class_member_declarations '}' { 
          types_leave_scope();
          inside_struct_or_class = false; // We're done parsing class members
          if ($2) {
              // Mark the class as fully defined now that we've parsed the body
              types_mark_defined($2, TYPE_KIND_TAG_CLASS);
              current_type_str = "class " + string($2);
              update_current_type();
          }
          // This is a class definition, return a StructDecl AST node
          $$ = ast_make_struct_decl($2 ? $2 : "", std::move(current_member_list), true);
          current_member_list.clear();
      }
    | CLASS IDENTIFIER ':' inheritence '{' { 
          // Add the class as a forward declaration immediately
          if ($2) {
              add_symbol($2, "class", "class", yylineno);
              types_add_tag($2, TYPE_KIND_TAG_CLASS);
          }
          types_enter_scope(); 
          reset_current_type(); 
          current_member_list.clear();
          current_access_level = 1; // Default to private for classes
          inside_struct_or_class = true; // We're now parsing class members
      } class_member_declarations '}' { 
          types_leave_scope();
          inside_struct_or_class = false; // We're done parsing class members
          if ($2) {
              // Mark the class as fully defined
              types_mark_defined($2, TYPE_KIND_TAG_CLASS);
              current_type_str = "class " + string($2);
              update_current_type();
          }
          // This is a class definition with inheritance, return a StructDecl AST node
          $$ = ast_make_struct_decl($2 ? $2 : "", std::move(current_member_list), true);
          current_member_list.clear();
      }
    | CLASS IDENTIFIER {
          if ($2) {
              add_symbol($2, "class", "class", yylineno);
              types_add_tag($2, TYPE_KIND_FORWARD_DECL);
              current_type_str = "class " + string($2);
              update_current_type();
          }
          // This is a forward declaration, return a TypeSpecifier
          $$ = ast_make_identifier_type_specifier($2 ? $2 : "");
      }
    | CLASS IDENTIFIER '{' { 
          // Add the class as a forward declaration immediately
          if ($2) {
              add_symbol($2, "class", "class", yylineno);
              types_add_tag($2, TYPE_KIND_TAG_CLASS);
          }
          types_enter_scope(); 
          reset_current_type(); 
          current_member_list.clear();
          current_access_level = 1; // Default to private for classes
      } '}' { 
          types_leave_scope();
          if ($2) {
              // Mark the class as fully defined
              types_mark_defined($2, TYPE_KIND_TAG_CLASS);
              current_type_str = "class " + string($2);
              update_current_type();
          }
          // This is an empty class definition, return a StructDecl AST node
          $$ = ast_make_struct_decl($2 ? $2 : "", std::move(current_member_list), true);
          current_member_list.clear();
      }
    ;

inheritence
    : access_specifier IDENTIFIER
    | access_specifier TYPE_NAME
    | inheritence ',' access_specifier IDENTIFIER
    | inheritence ',' access_specifier TYPE_NAME
    ;

class_member_declarations
    : /* empty */
    | struct_declaration_list
    | class_member_declarations class_member_or_access_spec
    ;

class_member_or_access_spec
    : class_member

| access_specifier ':' {
          // Update current access level based on specifier
          // $$ is not used for access specifier rules
      }

access_specifier
    : PUBLIC {
          current_access_level = 0; // public
      }
    | PRIVATE {
          current_access_level = 1; // private
      }
    | PROTECTED {
          current_access_level = 2; // protected
      }
    ;



class_member
    : specifier_qualifier_list struct_declarator_list ';' {
          // Members are collected in current_member_list by struct_declarator_list
          reset_current_type();
          $$ = nullptr;
      }
    | declaration {
          $$ = $1;
      }
    | function_definition
    | '~' IDENTIFIER '(' ')' compound_statement {
          if ($2) add_symbol($2, "void", "destructor", yylineno);
      }
    ;

struct_or_union_specifier
    : struct_or_union IDENTIFIER '{' { 
          // Add the struct/union as a forward declaration immediately so it can be used in self-referential declarations
          if ($2) {
              add_symbol($2, $1, $1, yylineno);
              types_add_tag($2, TYPE_KIND_TAG_STRUCT);
          }
          types_enter_scope(); 
          reset_current_type();
          current_member_list.clear();
          current_access_level = 0; // Default to public for structs/unions
          inside_struct_or_class = true; // We're now parsing struct/union members
      } struct_declaration_list '}' { 
          types_leave_scope();
          inside_struct_or_class = false; // We're done parsing struct/union members
          std::string name = $2 ? std::string($2) : std::string();
          bool is_struct = ($1 && strcmp($1, "struct") == 0);
          if (is_struct) {
              $$ = ast_make_struct_decl(name, std::move(current_member_list), true);
          } else {
              $$ = ast_make_union_decl(name, std::move(current_member_list), true);
          }
          current_member_list.clear();
          if ($2) {
              // Mark the struct/union as fully defined now that we've parsed the body
              types_mark_defined($2, TYPE_KIND_TAG_STRUCT);
              current_type_str = string($1) + " " + string($2);
              update_current_type();
          }
      }
    | struct_or_union '{' { 
          types_enter_scope(); 
          reset_current_type();
          current_member_list.clear();
          inside_struct_or_class = true; // We're now parsing struct/union members
      } struct_declaration_list '}' { 
          types_leave_scope();
          inside_struct_or_class = false; // We're done parsing struct/union members
          bool is_struct = ($1 && strcmp($1, "struct") == 0);
          if (is_struct) {
              $$ = ast_make_struct_decl("", std::move(current_member_list), true);
          } else {
              $$ = ast_make_union_decl("", std::move(current_member_list), true);
          }
          current_member_list.clear();
      }
    | struct_or_union IDENTIFIER {
          std::string name = $2 ? std::string($2) : std::string();
          bool is_struct = ($1 && strcmp($1, "struct") == 0);
          if (is_struct) {
              $$ = ast_make_struct_decl(name, {}, false); // forward declaration
          } else {
              $$ = ast_make_union_decl(name, {}, false);
          }
          if ($2) {
              add_symbol($2, $1, $1, yylineno);
              types_add_tag($2, TYPE_KIND_FORWARD_DECL);
              current_type_str = string($1) + " " + string($2);
              update_current_type();
          }
      }
    | struct_or_union IDENTIFIER '{' { types_enter_scope(); reset_current_type(); } '}' { types_leave_scope();
          if ($2) {
              add_symbol($2, $1, $1, yylineno);
              types_add_tag($2, TYPE_KIND_TAG_STRUCT);
              types_mark_defined($2, TYPE_KIND_TAG_STRUCT);
              current_type_str = string($1) + " " + string($2);
              update_current_type();
          }
      }
    ;


struct_or_union
    : STRUCT { $$ = strdup("struct"); }
    | UNION  { $$ = strdup("union"); }
    ;

struct_declaration_list
	: struct_declaration { $$ = $1; }
	| struct_declaration_list struct_declaration { $$ = $1; /* members collected in current_member_list */ }
	;

struct_declaration
	: specifier_qualifier_list struct_declarator_list ';' {
          // Members are collected in current_member_list by struct_declarator_list
          reset_current_type();
          $$ = nullptr; // Not used, members are in global list
      }
	;

specifier_qualifier_list
	: type_specifier specifier_qualifier_list
	| type_specifier
	| type_qualifier specifier_qualifier_list
	| type_qualifier
	;

struct_declarator_list
	: struct_declarator {
          if ($1) current_member_list.push_back(ast_ptr_from_raw($1));
          $$ = $1;
      }
	| struct_declarator_list ',' struct_declarator {
          if ($3) current_member_list.push_back(ast_ptr_from_raw($3));
          $$ = $1;
      }
	;

struct_declarator
    : declarator {
          AstNode* type_node = ast_make_type_specifier(current_type);
          const char* member_name = $1 ? $1 : "";
          if ($1) {
              add_symbol($1, current_type[0] ? current_type : "-", "member", yylineno);
          }
          $$ = ast_make_member_decl(member_name, type_node);
          pointer_count = 0;
          declarator_is_array = false;
          declarator_array_dimensions.clear();
      }
    | ':' constant_expression {
          // Anonymous bit field
          AstNode* type_node = ast_make_type_specifier(current_type);
          $$ = ast_make_member_decl("", type_node);
          // TODO: handle bit field width from $2
      }
    | declarator ':' constant_expression {
          // Named bit field
          AstNode* type_node = ast_make_type_specifier(current_type);
          const char* member_name = $1 ? $1 : "";
          if ($1) {
              add_symbol($1, current_type[0] ? current_type : "-", "member", yylineno);
          }
          $$ = ast_make_member_decl(member_name, type_node);
          // TODO: handle bit field width from $3
          pointer_count = 0;
          declarator_is_array = false;
          declarator_array_dimensions.clear();
      }
    ;

enum_specifier
    : ENUM '{' { 
          types_enter_scope(); 
          reset_current_type(); 
          current_enumerator_list.clear(); 
      } enumerator_list '}' { 
          types_leave_scope(); 
          $$ = ast_make_enum_decl("", current_enumerator_list, true);
      }
    | ENUM IDENTIFIER '{' { 
          types_enter_scope(); 
          reset_current_type(); 
          current_enumerator_list.clear(); 
      } enumerator_list '}' { 
          types_leave_scope();
          if ($2) {
              add_symbol($2, "enum", "enum", yylineno);
              types_add_tag($2, TYPE_KIND_TAG_ENUM);
              types_mark_defined($2, TYPE_KIND_TAG_ENUM);
              types_add_typedef($2);
          }
          $$ = ast_make_enum_decl($2 ? $2 : "", current_enumerator_list, true);
      }
    | ENUM IDENTIFIER {
          if ($2) {
              add_symbol($2, "enum", "enum", yylineno);
              types_add_tag($2, TYPE_KIND_TAG_ENUM);
              types_add_typedef($2);
          }
          $$ = ast_make_enum_decl($2 ? $2 : "", {}, false);
      }
    ;



enumerator_list
	: enumerator {
          if ($1) current_enumerator_list.push_back(ast_ptr_from_raw($1));
      }
	| enumerator_list ',' enumerator {
          if ($3) current_enumerator_list.push_back(ast_ptr_from_raw($3));
      }
	;

enumerator
    : IDENTIFIER {
          if ($1) add_symbol($1, "enum-constant", "constant", yylineno);
          $$ = ast_make_enumerator_decl($1 ? $1 : "", nullptr);
      }
    | IDENTIFIER '=' constant_expression {
          if ($1) add_symbol($1, "enum-constant", "constant", yylineno);
          $$ = ast_make_enumerator_decl($1 ? $1 : "", $3);
      }
    ;


type_qualifier
	: CONST    { append_type_specifier("const"); }
	| VOLATILE { append_type_specifier("volatile"); }
	;

declarator
	: pointer direct_declarator        { $$ = $2; isp=true; isr = false; }
	| direct_declarator               { $$ = $1; isp=false; isr=false; pointer_count=0;}
	| '&' direct_declarator           { $$ = $2; isp=false; isr=true; pointer_count=0; declarator_is_array=false;}
	;

direct_declarator
    : IDENTIFIER                      { $$ = $1; declarator_is_array = false; declarator_array_dimensions.clear(); }
    | '(' declarator ')'              { $$ = $2; }
    | direct_declarator '[' constant_expression ']' { 
          $$ = $1; 
          declarator_is_array = true;
          // Try to extract array size from constant expression
          int size = 0;
          bool valid_array_size = false;
          
          if ($3) {
              // Try to evaluate constant expression
              size = evaluate_constant_expression($3, &valid_array_size);
              if (!valid_array_size) {
                  fprintf(stderr, "Error at line %d: Array size must be a constant integer expression\n", yylineno);
                  has_redefinition_error = true;
              }
          }
          declarator_array_dimensions.push_back(size);
      }
    | direct_declarator '[' ']' { 
          $$ = $1; 
          declarator_is_array = true; 
          declarator_array_dimensions.push_back(0); // unsized
      }
    | direct_declarator '(' { 
          strncpy(saved_decl_type, current_type, sizeof(saved_decl_type)-1);
          saved_decl_type[sizeof(saved_decl_type)-1] = '\0';
          force_reset_type();
          current_param_list.clear(); // Clear parameter list for new function
      } parameter_type_list ')' {
          $$ = $1;
          if ($1) add_function_symbol($1, saved_decl_type[0] ? saved_decl_type : "-", yylineno);
      }
    | direct_declarator '(' { 
          strncpy(saved_decl_type, current_type, sizeof(saved_decl_type)-1);
          saved_decl_type[sizeof(saved_decl_type)-1] = '\0';
          force_reset_type();
          current_param_list.clear(); // Clear for identifier list
      } identifier_list ')' {
          $$ = $1;
          if ($1) add_function_symbol($1, saved_decl_type[0] ? saved_decl_type : "-", yylineno);
      }
    | direct_declarator '(' ')' {
          $$ = $1;
          current_param_list.clear(); // No parameters
          if ($1) add_function_symbol($1, current_type[0] ? current_type : "-", yylineno);
      }
    ;


pointer
	: '*' { pointer_count = 1; }
	| '*' type_qualifier_list { pointer_count = 1; }
	| '*' pointer { pointer_count++; }
	| '*' type_qualifier_list pointer { pointer_count++; }
	;

type_qualifier_list
	: type_qualifier
	| type_qualifier_list type_qualifier
	;

parameter_type_list
	: parameter_list { 
          $$ = $1; 
          current_func_is_variadic = false;
      }
	| parameter_list ',' ELLIPSIS { 
          $$ = $1; 
          current_func_is_variadic = true;
      }
	;

parameter_list
	: parameter_declaration {
          // Don't clear - already cleared at start of parameter_type_list
          if ($1) current_param_list.push_back(ast_ptr_from_raw($1));
          $$ = $1;
      }
	| parameter_list ',' parameter_declaration {
          if ($3) current_param_list.push_back(ast_ptr_from_raw($3));
          $$ = $1; // Return first param (not really used)
      }
	;
    
parameter_declaration
    : declaration_specifiers declarator {
          AstNode* type_node = ast_make_type_specifier(current_type);
          const char* param_name = $2 ? $2 : "";
          
          // Build complete type string including pointers for display
          std::string complete_type = current_type[0] ? current_type : "-";
          for (int i = 0; i < pointer_count; i++) {
              complete_type += "*";
          }
          
          if ($2) add_symbol($2, complete_type.c_str(), "parameter", yylineno);
          $$ = ast_make_parameter_decl(param_name, type_node);
          pointer_count = 0;
          declarator_is_array = false;
          declarator_array_dimensions.clear();
          reset_current_type();
      }
    | declaration_specifiers abstract_declarator {
          // Parameter without name (e.g., in function prototype)
          AstNode* type_node = ast_make_type_specifier(current_type);
          $$ = ast_make_parameter_decl("", type_node);
          pointer_count = 0;
          declarator_is_array = false;
          declarator_array_dimensions.clear();
          reset_current_type();
      }
    | declaration_specifiers {
          // Just the type, no declarator
          AstNode* type_node = ast_make_type_specifier(current_type);
          $$ = ast_make_parameter_decl("", type_node);
          pointer_count = 0;
          declarator_is_array = false;
          declarator_array_dimensions.clear();
          reset_current_type();
      }
    ;

identifier_list
    : IDENTIFIER {
          if ($1) add_symbol($1, current_type[0] ? current_type : "-", "parameter", yylineno);
          $$ = nullptr;
      }
    | identifier_list ',' IDENTIFIER {
          if ($3) add_symbol($3, current_type[0] ? current_type : "-", "parameter", yylineno);
          $$ = nullptr;
      }
    ;


type_name
	: specifier_qualifier_list {
          std::string type_str = current_type;  // Save before reset
          reset_current_type();
          $$ = ast_make_type_specifier(type_str);
      }
	| specifier_qualifier_list abstract_declarator {
          std::string type_str = current_type;  // Save before reset
          reset_current_type();
          $$ = ast_make_type_specifier(type_str);
      }
	;

abstract_declarator
	: pointer
	| direct_abstract_declarator
	| pointer direct_abstract_declarator
	;

direct_abstract_declarator
	: '(' abstract_declarator ')'
	| '[' ']'
	| '[' constant_expression ']'
	| direct_abstract_declarator '[' ']'
	| direct_abstract_declarator '[' constant_expression ']'
	| '(' ')'
	| '(' parameter_type_list ')'
	| direct_abstract_declarator '(' ')'
	| direct_abstract_declarator '(' parameter_type_list ')'
	;

initializer
    : assignment_expression { $$ = $1; }
    | '{' initializer_list '}' { $$ = $2; }
    | '{' initializer_list ',' '}' { $$ = $2; }
	;

initializer_list
    : initializer {
          AstNode* list = ast_make_initializer_list();
          ast_initializer_list_append(list, $1);
          $$ = list;
      }
    | initializer_list ',' initializer {
          AstNode* list = $1 ? $1 : ast_make_initializer_list();
          ast_initializer_list_append(list, $3);
          $$ = list;
      }
	;

statement
    : labeled_statement { $$ = $1; }
    | compound_statement { $$ = $1; }
    | expression_statement { $$ = $1; }
    | selection_statement { $$ = $1; }
    | iteration_statement { $$ = $1; }
    | jump_statement { $$ = $1; }
    | declaration { $$ = $1; }
	;

labeled_statement
    : IDENTIFIER ':' statement {
          if ($1) add_symbol($1, "label", "label", yylineno);
          $$ = ast_make_label_stmt($1 ? $1 : "", $3);
      }
    | CASE constant_expression ':' statement { $$ = nullptr; }
    | DEFAULT ':' statement { $$ = nullptr; }
    ;


compound_statement
	: '{' { types_enter_scope(); reset_current_type(); } '}' {
          // NOTE: Don't exit scope here - semantic analyzer will manage scopes
          // types_leave_scope();
          AstNode* node = ast_make_compound_stmt();
          $$ = node;
      }
	| '{' { types_enter_scope(); reset_current_type(); } statement_list '}' {
          AstNode* list = $3;
          // NOTE: Don't exit scope here - semantic analyzer will manage scopes
          // types_leave_scope();
          AstNode* node = ast_make_compound_stmt();
          if (list && list->kind == AstNodeKind::InitializerList) {
              auto &stmts = node->as<CompoundStmtNodeData>().statements;
              const auto &elements = list->as<InitializerListNodeData>().elements;
              stmts.insert(stmts.end(), elements.begin(), elements.end());
          }
          $$ = node;
      }
    ;



declaration_list
	: declaration 
	| declaration_list declaration 
	;

statement_list
	: statement {
          AstNode* list = nullptr;
          if ($1) {
              list = ast_make_initializer_list();
              ast_initializer_list_append(list, $1);
          }
          $$ = list;
      }
	| statement_list statement {
          AstNode* list = $1;
          if (!list) {
              list = ast_make_initializer_list();
          }
          if ($2) {
              ast_initializer_list_append(list, $2);
          }
          $$ = list;
      }
	;

expression_statement
    : ';' { $$ = nullptr; }
    | expression ';' {
          $$ = ast_make_expression_stmt($1);
      }
	;
selection_statement
    : IF '(' expression ')' statement %prec THEN {
          $$ = ast_make_if_stmt($3, $5, nullptr);
      }
    | IF '(' expression ')' statement ELSE statement {
          $$ = ast_make_if_stmt($3, $5, $7);
      }
    | SWITCH '(' expression ')' statement { $$ = nullptr; }
    ;

iteration_statement
	: WHILE '(' expression ')' statement {
	      $$ = ast_make_while_stmt($3, $5);
	  }
	| UNTIL '(' expression ')' statement {
	      $$ = ast_make_while_stmt($3, $5);
	  }
	| DO statement WHILE '(' expression ')' ';' { $$ = nullptr; }
	| FOR '(' expression_statement expression_statement ')' statement {
	      $$ = ast_make_for_stmt($3, $4, nullptr, $6);
	  }
	| FOR '(' expression_statement expression_statement expression ')' statement {
	      $$ = ast_make_for_stmt($3, $4, $5, $7);
	  }
	| FOR '(' declaration expression_statement ')' statement {
	      $$ = ast_make_for_stmt($3, $4, nullptr, $6);
	  }
	| FOR '(' declaration expression_statement expression ')' statement {
	      $$ = ast_make_for_stmt($3, $4, $5, $7);
	  }
	;

jump_statement
    : GOTO IDENTIFIER ';' {
          if ($2) add_symbol($2, "label", "goto-reference", yylineno);
          $$ = ast_make_goto_stmt($2 ? $2 : "");
      }
    | CONTINUE ';' {
          $$ = ast_make_continue_stmt();
      }
    | BREAK ';' {
          $$ = ast_make_break_stmt();
      }
    | RETURN ';' {
          $$ = ast_make_return_stmt(nullptr);
      }
    | RETURN expression ';' {
          $$ = ast_make_return_stmt($2);
      }
    ;

translation_unit
	: external_declaration {
          AstNode* tu = ast_pool_create(AstNodeKind::TranslationUnit);
          tu->payload = TranslationUnitNodeData{};
          if ($1) {
              ast_translation_unit_append(tu, $1);
          }
          g_translation_unit_ast = ast_ptr_from_raw(tu);
          $$ = tu;
          reset_current_type();
      }
	| translation_unit external_declaration {
          if ($2) {
              ast_translation_unit_append($1, $2);
          }
          g_translation_unit_ast = ast_ptr_from_raw($1);
          $$ = $1;
          reset_current_type();
      }
	;

external_declaration
    : function_definition {
          $$ = $1;
      }
    | declaration {
          $$ = $1;
      }
	;

function_definition
	: declaration_specifiers declarator declaration_list compound_statement {
          std::string func_name = $2 ? std::string($2) : std::string();
          
          // Use the declaration_specifiers AST node directly instead of global strings
          AstNode* return_type = $1; // declaration_specifiers should contain the type
          if (!return_type) {
              // Fallback if declaration_specifiers didn't provide a type node
              std::string ret_spec = current_type_str.empty() ? std::string(saved_decl_type) : current_type_str;
              if (ret_spec.empty()) ret_spec = "int";
              return_type = ast_make_type_specifier(ret_spec);
          }
          AstNode* body_node = $4;
          $$ = ast_make_function_decl(func_name, return_type, std::move(current_param_list), body_node, true, current_func_is_variadic);
          current_param_list.clear();
          current_func_is_variadic = false;
          reset_current_type();
      }
	| declaration_specifiers declarator compound_statement {
          std::string func_name = $2 ? std::string($2) : std::string();
          
          // Use the declaration_specifiers AST node directly instead of global strings
          AstNode* return_type = $1; // declaration_specifiers should contain the type
          if (!return_type) {
              // Fallback if declaration_specifiers didn't provide a type node
              std::string ret_spec = current_type_str.empty() ? std::string(saved_decl_type) : current_type_str;
              if (ret_spec.empty()) ret_spec = "int";
              return_type = ast_make_type_specifier(ret_spec);
          }
          AstNode* body_node = $3;
          $$ = ast_make_function_decl(func_name, return_type, std::move(current_param_list), body_node, true, current_func_is_variadic);
          current_param_list.clear();
          current_func_is_variadic = false;
          reset_current_type();
      }
	| declarator declaration_list compound_statement {
          std::string func_name = $1 ? std::string($1) : std::string();
          std::string ret_spec = current_type_str.empty() ? std::string(saved_decl_type) : current_type_str;
          if (ret_spec.empty()) ret_spec = "int";
          AstNode* return_type = ast_make_type_specifier(ret_spec);
          AstNode* body_node = $3;
          $$ = ast_make_function_decl(func_name, return_type, std::move(current_param_list), body_node, true, current_func_is_variadic);
          current_param_list.clear();
          current_func_is_variadic = false;
          reset_current_type();
      }
	| declarator compound_statement {
          std::string func_name = $1 ? std::string($1) : std::string();
          std::string ret_spec = current_type_str.empty() ? std::string(saved_decl_type) : current_type_str;
          if (ret_spec.empty()) ret_spec = "int";
          AstNode* return_type = ast_make_type_specifier(ret_spec);
          AstNode* body_node = $2;
          $$ = ast_make_function_decl(func_name, return_type, std::move(current_param_list), body_node, true, current_func_is_variadic);
          current_param_list.clear();
          current_func_is_variadic = false;
          reset_current_type();
      }
	;

%%

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void yyerror(const char *s) {
    fprintf(stderr, "Error at line %d: %s\n", yylineno, s);
}

int main(int argc, char *argv[]) {
    
    reset_current_type();
    ast_reset_pool();
    types_init();
    
    // Initialize proper symbol table with error callback
    proper_symbol_table = new SymbolTable([](const std::string& msg) {
        fprintf(stderr, "%s\n", msg.c_str());
    });

    reset_current_type();
    
    if (argc > 1) {
        FILE *file = fopen(argv[1], "r");
        if (!file) {
            fprintf(stderr, "Error: Cannot open file %s\n", argv[1]);
            return 1;
        }
        extern FILE *yyin;
        yyin = file;
    }
    
    int parse_ok = (yyparse() == 0);

    int exit_code = parse_ok ? 0 : 1;
    
    // Check for redefinition errors
    if (has_redefinition_error) {
        exit_code = 1;
    }

    SemanticAnalyzer analyzer;
    if (parse_ok && proper_symbol_table) {
        analyzer.analyze(g_translation_unit_ast, *proper_symbol_table);
        print_diagnostics(analyzer.diagnostics());
        if (analyzer.diagnostics().has_errors()) {
            exit_code = 1;
        }
    }

    types_free();
    
    // Clean up proper symbol table
    delete proper_symbol_table;
    proper_symbol_table = nullptr;

    if (exit_code == 0) {
        printf("Parsing completed successfully!\n");
        print_symbol_table();
        // Print generated IR
        analyzer.get_ir_generator().print_ir();
        return 0;
    } else {
        printf("Parsing failed!\n");
        print_symbol_table();
        // Print IR even if parsing failed (might have partial IR)
        analyzer.get_ir_generator().print_ir();
        return exit_code;
    }
}