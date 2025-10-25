%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbol_table.h"
#include "tac.h"
#include "expression.h"
#include "statement.h"
#include "declaration.h"
#include "diagnostics.h"

int yylex(void);
void yyerror(const char *s);
extern int yylineno;
extern int semantic_error_count;

static inline int diag_line(int line)
{
    return line > 0 ? line : yylineno;
}

#define PARSE_ERROR(line, ...) report_parse_error(diag_line(line), __VA_ARGS__)
#define SEM_ERROR(line, ...) report_semantic_error(diag_line(line), __VA_ARGS__)
#define SEM_WARN(line, ...) report_semantic_warning(diag_line(line), __VA_ARGS__)

// Current type being declared
Type current_type;

// Current function return type (value). TYPE_ERROR sentinel means "no active function".
Type current_function_return_type;

Type backup_current_type;
bool in_function_declaration = false;

// Separate backup for method return types (to avoid interfering with function handling)
Type method_return_type_backup;

// Track pointer levels and array dimensions for current declarator
int current_pointer_level = 0;
bool current_is_array = false;
std::vector<int> current_array_sizes;

// Track storage class for current declaration
bool current_is_static = false;

// Track current enum being parsed
EnumType* current_enum = nullptr;

// Track current struct being parsed (declared in symbol_table.h)
// extern StructType* current_struct;

// Track whether we're currently parsing a union (true) or struct (false)
bool current_is_parsing_union = false;

// List to hold declarators from comma-separated lists that need deferred TAC generation
std::vector<Declaration*> pending_declarator_tac;

// List to hold left-hand expressions from comma operator that need TAC generation
std::vector<Expression*> pending_comma_expr_tac;

// Snapshot of function header info captured when parsing a function declarator
std::string pending_function_name;
Type pending_function_return_type;
bool pending_function_header = false;

// Collect function parameters during parsing; insert at function-body scope entry
std::vector<std::pair<std::string, Type>> pending_function_params;
// Also keep just the type list for signature registration
std::vector<Type> pending_function_param_types;

// ========== METHOD-SPECIFIC VARIABLES (completely separate from functions) ==========
// Method name being parsed
std::string pending_method_name;
// Method return type (saved BEFORE parameter parsing to avoid contamination)
Type pending_method_return_type;
// Method parameters (types only, for signature registration)
std::vector<Type> pending_method_param_types;
// Note: current_method_signature (declared in symbol_table.h) tracks which method we're inside
// Note: method_return_type_backup is declared earlier with other Type variables
// Flag to ensure class is finalized before first method (only once)
bool class_finalized_for_methods = false;
// ==================================================================================

// For methods: collect parameters (excluding implicit 'this')
std::vector<Type> current_method_params;

// Helper function to create variable declarations with static support
VariableDeclaration* create_variable_declaration(Type* var_type, const char* var_name, Expression* initializer) {
    return new VariableDeclaration(var_type, var_name, initializer, current_is_static);
}

%}

%locations  /* Enable location tracking */


%union {
    char* str;
    int ival;
    float fval;
    char cval;
    void* ptr;
    Type* type_ptr;
    Expression* expr;
    Declaration* decl;
    Statement* stmt;
    std::vector<Expression*>* expr_list;
}

%token <str> IDENTIFIER STRING_LITERAL TYPE_NAME
%token <ival> INT_CONSTANT BOOL_TRUE BOOL_FALSE
%token <fval> FLOAT_CONSTANT
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
%type <str> parameter_declaration
%type <ival> pointer
%type <ival> pointer_opt
%type <ival> type_qualifier_list_opt

%type <expr> expression
%type <expr> assignment_expression
%type <expr> conditional_expression
%type <expr> logical_or_expression
%type <expr> logical_and_expression
%type <expr> inclusive_or_expression
%type <expr> exclusive_or_expression
%type <expr> and_expression
%type <expr> equality_expression
%type <expr> relational_expression
%type <expr> shift_expression
%type <expr> additive_expression
%type <expr> multiplicative_expression
%type <expr> cast_expression
%type <expr> unary_expression
%type <expr> postfix_expression
%type <expr> primary_expression
%type <expr> constant

%type <expr> initializer
%type <expr_list> initializer_list
%type <expr> constant_expression
%type <expr_list> argument_expression_list

%type <type_ptr> type_specifier

%type <decl> declaration
%type <decl> init_declarator
%type <decl> init_declarator_list
%type <decl> class_ctor_declarator
%type <decl> class_ctor_declarator_list

%type <stmt> statement
%type <stmt> labeled_statement
%type <stmt> compound_statement
%type <stmt> expression_statement
%type <stmt> selection_statement
%type <stmt> iteration_statement
%type <stmt> jump_statement
%type <stmt> statement_list

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
	: IDENTIFIER
        { 
            $$ = create_primary_expression($1, @1.first_line, @1.first_column);
            free($1); 
        }
	| constant
        { 
            $$ = $1;
        }
	| STRING_LITERAL
        {
            $$ = create_string_literal_expression($1);
            free($1);
        }
	| '(' expression ')'
        { 
            $$ = create_paren_expression($2);
        }
	;

constant
    : INT_CONSTANT
        { 
            $$ = create_primary_expression($1, @1.first_line, @1.first_column);
        }
    | FLOAT_CONSTANT
        { 
            $$ = create_primary_expression($1, @1.first_line, @1.first_column);
        }
    | CHAR_CONSTANT
        { 
            $$ = create_primary_expression($1, @1.first_line, @1.first_column);
        }
    | BOOL_TRUE
        { 
            $$ = create_bool_constant_expression(true);
        }
    | BOOL_FALSE
        { 
            $$ = create_bool_constant_expression(false);
        }
    | NULL_CONSTANT
        { 
            $$ = create_null_constant_expression();
        }
    | NULLPTR_CONSTANT
        { 
            $$ = create_null_constant_expression();
        }
    ;


postfix_expression
	: primary_expression
        { $$ = $1; }
	| postfix_expression '[' expression ']'
        {
            $$ = create_array_access_expression($1, $3);
        }
	| postfix_expression '(' ')'
        {
            // Function call with no args: require LHS to be identifier primary
            PrimaryExpression* id = dynamic_cast<PrimaryExpression*>($1);
            std::vector<Expression*> empty;
            if (id && id->prim_type == PrimaryExpression::PRIM_IDENTIFIER) {
                $$ = create_call_expression(id->name, empty);
            } else {
                $$ = $1; // fallback
            }
        }
	| postfix_expression '(' argument_expression_list ')'
        {
            // Function call with args
            PrimaryExpression* id = dynamic_cast<PrimaryExpression*>($1);
            if (id && id->prim_type == PrimaryExpression::PRIM_IDENTIFIER) {
                $$ = create_call_expression(id->name, *$3);
                delete $3;
            } else {
                $$ = $1; // fallback
            }
        }
	| postfix_expression '.' IDENTIFIER {
          $$ = create_member_access_expression($1, $3);
          free($3);
      }
	| postfix_expression '.' IDENTIFIER '(' ')' {
          // Method call with no arguments: object.method()
          std::vector<Expression*> empty_args;
          $$ = create_method_call_expression($1, $3, &empty_args);
          free($3);
      }
	| postfix_expression '.' IDENTIFIER '(' argument_expression_list ')' {
          // Method call with arguments: object.method(arg1, arg2, ...)
          $$ = create_method_call_expression($1, $3, $5);
          delete $5;  // argument list managed by MethodCallExpression
          free($3);
      }
	| postfix_expression PTR_OP IDENTIFIER {
          $$ = create_member_access_ptr_expression($1, $3);
          free($3);
      }
	| postfix_expression INC_OP
        {
            $$ = create_postfix_expression(TAC_POST_INC, $1);
        }
	| postfix_expression DEC_OP
        {
            $$ = create_postfix_expression(TAC_POST_DEC, $1);
        }
	;

argument_expression_list
    : assignment_expression
        {
            $$ = new std::vector<Expression*>();
            $$->push_back($1);
        }
    | argument_expression_list ',' assignment_expression
        {
            $1->push_back($3);
            $$ = $1;
        }
	;

unary_expression
    : postfix_expression
        { $$ = $1; }
| INC_OP unary_expression
        { $$ = create_unary_expression(TAC_PRE_INC, $2); }
| DEC_OP unary_expression
        { $$ = create_unary_expression(TAC_PRE_DEC, $2); }
| '&' cast_expression
        { $$ = create_unary_expression(TAC_ADDR_OF, $2); }
| '*' cast_expression
        { $$ = create_unary_expression(TAC_DEREF, $2); }
| '+' cast_expression
        { $$ = create_unary_expression(TAC_UPLUS, $2); }
| '-' cast_expression %prec UMINUS
        { $$ = create_unary_expression(TAC_UMINUS, $2); }
| '~' cast_expression
        { $$ = create_unary_expression(TAC_BITWISE_NOT, $2); }
| '!' cast_expression
        { $$ = create_unary_expression(TAC_LOGICAL_NOT, $2); }
| SIZEOF unary_expression
| SIZEOF '(' type_name ')'
    ;

cast_expression
	: unary_expression
        { $$ = $1; }
	| '(' type_name ')' cast_expression
	;

multiplicative_expression
	: cast_expression
        { $$ = $1; }
	| multiplicative_expression '*' cast_expression
        { 
            $$ = create_binary_expression($1, TAC_MUL, $3);
        }
	| multiplicative_expression '/' cast_expression
        { $$ = create_binary_expression($1, TAC_DIV, $3); }
	| multiplicative_expression '%' cast_expression
        { $$ = create_binary_expression($1, TAC_MOD, $3); }
	;

additive_expression
	: multiplicative_expression
        { $$ = $1; }
	| additive_expression '+' multiplicative_expression
        { 
            $$ = create_binary_expression($1, TAC_ADD, $3);
        }
	| additive_expression '-' multiplicative_expression
        { $$ = create_binary_expression($1, TAC_SUB, $3); }
	;

shift_expression
	: additive_expression
        { $$ = $1; }
	| shift_expression LEFT_OP additive_expression
        { $$ = create_binary_expression($1, TAC_LEFT_SHIFT, $3); }
	| shift_expression RIGHT_OP additive_expression
        { $$ = create_binary_expression($1, TAC_RIGHT_SHIFT, $3); }
	;

relational_expression
	: shift_expression
        { $$ = $1; }
	| relational_expression '<' shift_expression
        { $$ = create_binary_expression($1, TAC_LT, $3); }
	| relational_expression '>' shift_expression
        { $$ = create_binary_expression($1, TAC_GT, $3); }
	| relational_expression LE_OP shift_expression
        { $$ = create_binary_expression($1, TAC_LE, $3); }
	| relational_expression GE_OP shift_expression
        { $$ = create_binary_expression($1, TAC_GE, $3); }
	;

equality_expression
	: relational_expression
        { $$ = $1; }
	| equality_expression EQ_OP relational_expression
        { $$ = create_binary_expression($1, TAC_EQ, $3); }
	| equality_expression NE_OP relational_expression
        { $$ = create_binary_expression($1, TAC_NE, $3); }
	;

and_expression
	: equality_expression
        { $$ = $1; }
	| and_expression '&' equality_expression
        { $$ = create_binary_expression($1, TAC_BITWISE_AND, $3); }
	;

exclusive_or_expression
	: and_expression
        { $$ = $1; }
	| exclusive_or_expression '^' and_expression
        { $$ = create_binary_expression($1, TAC_BITWISE_XOR, $3); }
	;

inclusive_or_expression
	: exclusive_or_expression
        { $$ = $1; }
	| inclusive_or_expression '|' exclusive_or_expression
        { $$ = create_binary_expression($1, TAC_BITWISE_OR, $3); }
	;

logical_and_expression
	: inclusive_or_expression
        { $$ = $1; }
	| logical_and_expression AND_OP inclusive_or_expression
        { $$ = create_binary_expression($1, TAC_LOGICAL_AND, $3); }
	;

logical_or_expression
	: logical_and_expression
        { $$ = $1; }
	| logical_or_expression OR_OP logical_and_expression
        { $$ = create_binary_expression($1, TAC_LOGICAL_OR, $3); }
	;

conditional_expression
	: logical_or_expression
        { $$ = $1; }
	| logical_or_expression '?' expression ':' conditional_expression
	;

assignment_expression
    : conditional_expression
        { $$ = $1; }
    | IDENTIFIER '=' assignment_expression
        {
            $$ = create_assignment_expression($1, $3);
        }
    | unary_expression '=' assignment_expression
        {
            // General assignment (LHS can be *ptr, arr[i], etc.)
            $$ = create_general_assignment_expression($1, $3);
        }
    | IDENTIFIER ADD_ASSIGN assignment_expression
        {
            // a += b becomes a = a + b
            Expression* lhs = create_primary_expression($1);
            Expression* rhs = create_binary_expression(lhs, TAC_ADD, $3);
            $$ = create_assignment_expression($1, rhs);
            free($1);
        }
    | unary_expression ADD_ASSIGN assignment_expression
        {
            // Handle compound assignments for complex lvalues like ptr->member += value
            Expression* rhs = create_binary_expression($1, TAC_ADD, $3);
            $$ = create_general_assignment_expression($1, rhs);
        }
    | IDENTIFIER SUB_ASSIGN assignment_expression
        {
            Expression* lhs = create_primary_expression($1);
            Expression* rhs = create_binary_expression(lhs, TAC_SUB, $3);
            $$ = create_assignment_expression($1, rhs);
            free($1);
        }
    | unary_expression SUB_ASSIGN assignment_expression
        {
            Expression* rhs = create_binary_expression($1, TAC_SUB, $3);
            $$ = create_general_assignment_expression($1, rhs);
        }
    | IDENTIFIER MUL_ASSIGN assignment_expression
        {
            Expression* lhs = create_primary_expression($1);
            Expression* rhs = create_binary_expression(lhs, TAC_MUL, $3);
            $$ = create_assignment_expression($1, rhs);
            free($1);
        }
    | unary_expression MUL_ASSIGN assignment_expression
        {
            Expression* rhs = create_binary_expression($1, TAC_MUL, $3);
            $$ = create_general_assignment_expression($1, rhs);
        }
    | IDENTIFIER DIV_ASSIGN assignment_expression
        {
            Expression* lhs = create_primary_expression($1);
            Expression* rhs = create_binary_expression(lhs, TAC_DIV, $3);
            $$ = create_assignment_expression($1, rhs);
            free($1);
        }
    | unary_expression DIV_ASSIGN assignment_expression
        {
            Expression* rhs = create_binary_expression($1, TAC_DIV, $3);
            $$ = create_general_assignment_expression($1, rhs);
        }
    | IDENTIFIER MOD_ASSIGN assignment_expression
        {
            Expression* lhs = create_primary_expression($1);
            Expression* rhs = create_binary_expression(lhs, TAC_MOD, $3);
            $$ = create_assignment_expression($1, rhs);
            free($1);
        }
    | unary_expression MOD_ASSIGN assignment_expression
        {
            Expression* rhs = create_binary_expression($1, TAC_MOD, $3);
            $$ = create_general_assignment_expression($1, rhs);
        }
    ;

assignment_operator
	: '='
	| MUL_ASSIGN
	| DIV_ASSIGN
	| MOD_ASSIGN
	| ADD_ASSIGN
	| SUB_ASSIGN
	| LEFT_ASSIGN
	| RIGHT_ASSIGN
	| AND_ASSIGN
	| XOR_ASSIGN
	| OR_ASSIGN
	;

expression
	: assignment_expression
        { $$ = $1; }
	| expression ',' assignment_expression
        {
            // Comma operator: evaluate left side for side effects, return right side
            // Defer TAC generation for left expression until the whole expression is used
            if ($1) {
                pending_comma_expr_tac.push_back($1);
            }
            $$ = $3;  // Return the right side
        }
	;

constant_expression
	: conditional_expression
        { $$ = $1; }
	;


// ============================================================================
// *** DECLARATIONS - Where Variables are Declared and Initialized ***
// This is where AST/TAC integration is most important for Phase 1
// ============================================================================

declaration
	: declaration_specifiers ';' {
          // Reset current_type after declaration to avoid contamination
          current_type = Type(TYPE_ERROR);
          in_function_declaration = false;
          $$ = nullptr;
      }
	| declaration_specifiers init_declarator_list ';'
        {
            if(debug) printf("\n--- Declaration complete ---\n");
            // Reset after declaration
            current_type = Type(TYPE_ERROR);
            in_function_declaration = false;
            $$ = $2; /* Return the last declarator from the list (for use in for-loops) */
        }
	| class_specifier class_ctor_declarator_list ';'
        {
            if(debug) printf("\n--- Class declaration (with optional ctor-init) complete ---\n");
            // Reset after declaration
            current_type = Type(TYPE_ERROR);
            in_function_declaration = false;
            $$ = $2;
        }
	;

declaration_specifiers
	: storage_class_specifier { 
        if (!in_function_declaration) { backup_current_type = current_type; in_function_declaration = true; } 
    } // Ex:- static, typedef
	| storage_class_specifier type_specifier { 
        if (!in_function_declaration) { backup_current_type = current_type; in_function_declaration = true; } 
    } // Ex:- static int
	| storage_class_specifier type_qualifier type_specifier { 
        if (!in_function_declaration) { backup_current_type = current_type; in_function_declaration = true; } 
    } // Ex:- static const int
	| type_specifier { 
        if (!in_function_declaration) { 
            backup_current_type = current_type; 
            in_function_declaration = true;
        } 
    } // Ex:- int
	| type_qualifier type_specifier { 
        if (!in_function_declaration) { backup_current_type = current_type; in_function_declaration = true; } 
    } // Ex:- const int
	| storage_class_specifier type_qualifier { 
        if (!in_function_declaration) { backup_current_type = current_type; in_function_declaration = true; } 
    } // Ex:- static const (followed by type elsewhere)
	| type_qualifier { 
        if (!in_function_declaration) { backup_current_type = current_type; in_function_declaration = true; } 
    } // Ex:- const (followed by type elsewhere)
	;

init_declarator_list
	: init_declarator
        { $$ = $1; }  /* Return the first (and possibly only) declarator */
	| init_declarator_list ',' init_declarator
        { 
            /* For multiple declarators, insert previous one into symbol table */
            /* and save it for deferred TAC generation */
            if ($1) {
                /* Insert symbol only if not already inserted (e.g., declarations with initializers) */
                VariableDeclaration* var_decl = dynamic_cast<VariableDeclaration*>($1);
                if (var_decl && !var_decl->inserted_symbol) {
                    $1->insert_symbol();
                }
                pending_declarator_tac.push_back($1);  // Defer TAC generation
            }
            $$ = $3;  /* Return the latest declarator */
        }
	;

// Special declarator list for class object declarations to support ctor-style initialization
class_ctor_declarator_list
    : class_ctor_declarator
        { $$ = $1; }
    | class_ctor_declarator_list ',' class_ctor_declarator
        {
            if ($1) {
                VariableDeclaration* var_decl = dynamic_cast<VariableDeclaration*>($1);
                if (var_decl && !var_decl->inserted_symbol) {
                    $1->insert_symbol();
                }
                pending_declarator_tac.push_back($1);
            }
            $$ = $3;
        }
    ;

class_ctor_declarator
    : declarator {
          if ($1) {
              if(debug) printf("[Parser] Class object variable: %s\n", $1);
              // Create type with pointer level from declarator
              Type* var_type = new Type(current_type);
              var_type->pointer_level = current_pointer_level;
              var_type->is_array = current_is_array;
              var_type->array_dim = current_array_sizes.size();
              var_type->array_sizes = current_array_sizes;

              VariableDeclaration* decl = create_variable_declaration(
                  var_type,
                  $1,
                  nullptr
              );
              $$ = decl;

              // Reset for next declarator
              current_pointer_level = 0;
              current_is_array = false;
              current_array_sizes.clear();
              current_is_static = false;
          } else {
              $$ = nullptr;
          }
      }
    | IDENTIFIER '(' ')'
      {
          // Constructor-style initialization with no arguments: class T var();
          if ($1) {
              if (debug) printf("[Parser] Constructor-style init (no args): %s\n", $1);

              // Must be a class type on the left (guaranteed in this production)
              Type* var_type = new Type(current_type);
              var_type->pointer_level = 0; // direct object

              VariableDeclaration* decl = create_variable_declaration(var_type, $1, nullptr);
              decl->insert_symbol();

              // Create object expression for the just-declared variable
              Expression* obj = create_primary_expression(std::string($1));

              // Create a method call expression: obj.ClassName()
              std::vector<Expression*> no_args;
              Expression* ctor_call = create_method_call_expression(obj, current_type.class_name.c_str(), &no_args);

              // Attach initializer
              decl->initializer = ctor_call;
              $$ = decl;

              // Reset for next declarator
              current_pointer_level = 0;
              current_is_array = false;
              current_array_sizes.clear();
              current_is_static = false;
          } else {
              $$ = nullptr;
          }
      }
    | IDENTIFIER '(' argument_expression_list ')'
      {
          // Constructor-style initialization with arguments: class T var(arg1, ...);
          if ($1) {
              if (debug) printf("[Parser] Constructor-style init (with args): %s\n", $1);

              // Build declaration first and insert symbol so the object is visible
              Type* var_type = new Type(current_type);
              var_type->pointer_level = 0; // direct object

              VariableDeclaration* decl = create_variable_declaration(var_type, $1, nullptr);
              decl->insert_symbol();

              // Create object expression for the just-declared variable
              Expression* obj = create_primary_expression(std::string($1));

              // Create a method call expression: obj.ClassName(args...)
              Expression* ctor_call = create_method_call_expression(obj, current_type.class_name.c_str(), $3);

              // Attach initializer
              decl->initializer = ctor_call;
              $$ = decl;

              // Cleanup and reset for next declarator
              delete $3;
              current_pointer_level = 0;
              current_is_array = false;
              current_array_sizes.clear();
              current_is_static = false;
          } else {
              $$ = nullptr;
          }
      }
    ;

init_declarator
	: declarator {
          if ($1) {
              if(debug) printf("[Parser] Variable: %s\n", $1);
              // Create type with pointer level from declarator
              Type* var_type = new Type(current_type);
              var_type->pointer_level = current_pointer_level;
              var_type->is_array = current_is_array;
              var_type->array_dim = current_array_sizes.size();
              
              // Array sizes are already in correct order (left-to-right as declared)
              var_type->array_sizes = current_array_sizes;
              // If this is an array with unspecified size (e.g., int a[];) and
              // there is no initializer, that's a semantic error in C.
              if (var_type->is_array) {
                  bool has_unspecified = false;
                  for (int i = 0; i < (int)var_type->array_sizes.size(); ++i) {
                      if (var_type->array_sizes[i] == 0) { has_unspecified = true; break; }
                  }
                  if (has_unspecified) {
                      SEM_ERROR(yylineno, "Array size not specified for '%s'", $1);
                      semantic_error_count++;
                      // Mark the variable type as an error to avoid cascading failures
                      var_type->base_type = TYPE_ERROR;
                      var_type->is_array = false;
                      var_type->array_sizes.clear();
                      var_type->array_dim = 0;
                  }
              }
              
              VariableDeclaration* decl = create_variable_declaration(
                  var_type,
                  $1,
                  nullptr
              );
              $$ = decl;
              
              // Reset for next declarator
              current_pointer_level = 0;
              current_is_array = false;
              current_array_sizes.clear();
              current_is_static = false;
          } else {
              $$ = nullptr;
          }
      }
	| declarator '=' initializer {
          if ($1) {
              if(debug) printf("[Parser] Variable with initializer: %s\n", $1);
              // Create type with pointer level from declarator
              Type* var_type = new Type(current_type);
              var_type->pointer_level = current_pointer_level;
              var_type->is_array = current_is_array;
              var_type->array_dim = current_array_sizes.size();
              
              // Array sizes are already in correct order (left-to-right as declared)
              var_type->array_sizes = current_array_sizes;
              
              VariableDeclaration* decl = create_variable_declaration(
                  var_type,
                  $1,
                  $3
              );
              
              // Insert symbol immediately for declarations with initializers
              // This is crucial for for-loop declarations like "for (int i = 0; ...)"
              // where the variable needs to be visible in the condition expression
              decl->insert_symbol();
              
              $$ = decl;
              
              // Reset for next declarator
              current_pointer_level = 0;
              current_is_array = false;
              current_array_sizes.clear();
              current_is_static = false;
          } else {
              $$ = nullptr;
          }
      }
	;


storage_class_specifier
	: TYPEDEF
	| STATIC
        {
            current_is_static = true;
            if (debug) printf("[Parser] Found static storage class\n");
        }
	;

type_specifier
    : VOID { 
          current_type = Type(TYPE_VOID);
          $$ = new Type(TYPE_VOID);
      }
    | CHAR { 
          current_type = Type(TYPE_CHAR);
          $$ = new Type(TYPE_CHAR);
      }
    | INT { 
          current_type = Type(TYPE_INT);
          $$ = new Type(TYPE_INT);
      }
    | BOOL { 
          current_type = Type(TYPE_BOOL);
          $$ = new Type(TYPE_BOOL);
      }
    | DOUBLE { 
          current_type = Type(TYPE_FLOAT);
          $$ = new Type(TYPE_FLOAT);
      }
    | SIGNED
    | UNSIGNED
    | struct_or_union_specifier {
          // struct_or_union_specifier already sets current_type
          $$ = new Type(current_type);
      }
    | enum_specifier {
          // After parsing enum, set current_type to enum type
          current_type = Type(TYPE_ENUM);
          $$ = new Type(TYPE_ENUM);
      }
    | TYPE_NAME
    | class_specifier
    ;

/* Class support - using separate ClassType infrastructure */
class_specifier
    : CLASS IDENTIFIER '{' {
          // Check if class already exists in current scope
          if (class_exists_in_current_scope($2)) {
              SEM_ERROR(@2.first_line, "Redefinition of class '%s'", $2);
              semantic_error_count++;
              current_class = nullptr;
          } else {
              // Create new class type
              current_class = new ClassType($2);
              register_class_in_scope($2, current_class);
              current_access_level = ACCESS_PUBLIC; // Classes start with public access
              class_finalized_for_methods = false;  // Reset flag for new class
              if (debug) printf("[CLASS] Created class '%s'\n", $2);
          }
      } class_member_declarations '}' {
          // Finalize class: calculate sizes and offsets (only if successfully created)
          if (current_class) {
              current_class->finalize();
              if (debug) printf("[CLASS] Finalized class '%s', size=%d\n", current_class->name.c_str(), current_class->total_size);
          }
          
          // Set current_type to this class
          current_type = Type();
          current_type.is_class = true;
          current_type.class_name = $2;
          current_type.class_type_ptr = lookup_class_in_scope($2);
          current_class = nullptr;
          
          free($2);
      }
    | CLASS IDENTIFIER ':' inheritance '{' class_member_declarations '}'
        { 
            // TODO: inheritance not yet implemented - treat as simple class for now
            SEM_WARN(@2.first_line, "Class inheritance not yet implemented, treating as simple class");
            
            // For now, create a simple class without inheritance
            current_type = Type();
            current_type.is_class = true;
            current_type.class_name = $2;
            free($2);
        }
    | CLASS IDENTIFIER '{' '}' {
          // Empty class definition
          ClassType* ct = new ClassType($2);
          register_class_in_scope($2, ct);
          ct->finalize();
          
          current_type = Type();
          current_type.is_class = true;
          current_type.class_name = $2;
          current_type.class_type_ptr = ct;
          if (debug) printf("[CLASS] Created empty class '%s'\n", $2);
          free($2);
      }
    | CLASS IDENTIFIER {
          // Reference to existing class (usage, not definition)
          ClassType* ct = lookup_class_in_scope($2);
          if (ct) {
              current_type = Type();
              current_type.is_class = true;
              current_type.class_name = $2;
              current_type.class_type_ptr = ct;
              if (debug) printf("[CLASS] Using class '%s'\n", $2);
          } else {
              SEM_ERROR(@2.first_line, "Class '%s' not defined", $2);
              semantic_error_count++;
              current_type = Type(TYPE_ERROR);
          }
          free($2);
      }
    ;

inheritance
    : access_specifier IDENTIFIER
    | access_specifier TYPE_NAME
    | inheritance ',' access_specifier IDENTIFIER
    | inheritance ',' access_specifier TYPE_NAME
    ;

class_member_declarations
    : /* empty */
    | class_member_declarations class_member_or_access_spec
    ;

class_member_or_access_spec
    : class_member
    | access_specifier ':'
        {
            // Access specifier sets current access level for subsequent members
            if (debug) printf("[CLASS] Set access specifier\n");
        }
    ;

access_specifier
    : PUBLIC
        {
            current_access_level = ACCESS_PUBLIC;
        }
    | PRIVATE
        {
            current_access_level = ACCESS_PRIVATE;
        }
    | PROTECTED
        {
            current_access_level = ACCESS_PROTECTED;
        }
    ;

class_member
    : type_specifier class_declarator_list ';' {
          // Data members - using type_specifier instead of specifier_qualifier_list
          // to avoid conflicts with method definitions
          current_type = Type(TYPE_ERROR);
          current_pointer_level = 0;
          current_is_array = false;
          current_array_sizes.clear();
      }
    | IDENTIFIER '(' ')' {
          // Constructor with no parameters (no explicit return type)
          if (current_class) {
              // Finalize class before first method
              if (!class_finalized_for_methods) {
                  current_class->finalize();
                  class_finalized_for_methods = true;
                  if (debug) printf("[CLASS] Finalized class '%s' before methods, size=%d\n", 
                                   current_class->name.c_str(), current_class->total_size);
              }
              
              // Validate constructor name matches class name
              if (std::string($1) != current_class->name) {
                  SEM_ERROR(@1.first_line, "Constructor name '%s' must match class name '%s'",
                            $1, current_class->name.c_str());
                  semantic_error_count++;
              }
              
              pending_method_name = $1;
              pending_method_return_type = Type(TYPE_VOID); // Constructors are always void
              
              if (debug) printf("[CLASS] Starting constructor '%s()' in class '%s'\n", $1, current_class->name.c_str());
              
              // Register constructor
              std::vector<Type> empty_params;
              MethodSignature* method = register_method(current_class->name, pending_method_name,
                                                        empty_params, pending_method_return_type, true, false, current_access_level);
              if (method) {
                  current_class->add_method(method);
                  current_method_signature = method;
                  current_function_return_type = method->returnType;
                  
                  // Emit TAC label
                  tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL, method->mangled_name), TACOperand());
                  if (debug) printf("[TAC] %s:\n", method->mangled_name.c_str());
                  
                  current_function_has_return = false;
                  if (debug) printf("[CLASS] Registered constructor '%s::%s' (no params)\n",
                                   current_class->name.c_str(), pending_method_name.c_str());
              }
              free($1);
          }
      } compound_statement {
          // Generate TAC for constructor body
          if ($5 && current_class) {
              if (debug) printf("[CONSTRUCTOR] Generating TAC for constructor body\n");
              $5->generate_tac();
          }
          
          // Constructors always get implicit return
          if (current_method_signature && current_method_signature->is_constructor) {
              tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
              if(debug) printf("[Constructor] Added implicit return for constructor '%s'\n", 
                              current_method_signature->mangled_name.c_str());
          }
          
          // Clear method context
          current_method_signature = nullptr;
          current_function_return_type = Type(TYPE_ERROR);
          pending_method_name = "";
          pending_method_return_type = Type(TYPE_ERROR);
      }
    | IDENTIFIER '(' {
          // Constructor with parameters (no explicit return type)
          if (current_class) {
              // Finalize class before first method
              if (!class_finalized_for_methods) {
                  current_class->finalize();
                  class_finalized_for_methods = true;
                  if (debug) printf("[CLASS] Finalized class '%s' before methods, size=%d\n", 
                                   current_class->name.c_str(), current_class->total_size);
              }
              
              // Validate constructor name matches class name
              if (std::string($1) != current_class->name) {
                  SEM_ERROR(@1.first_line, "Constructor name '%s' must match class name '%s'",
                            $1, current_class->name.c_str());
                  semantic_error_count++;
              }
              
              pending_method_name = $1;
              pending_method_return_type = Type(TYPE_VOID); // Constructors are always void
              
              if (debug) printf("[CLASS] Starting constructor '%s(...)' in class '%s'\n", $1, current_class->name.c_str());
              free($1);
          }
      } parameter_type_list ')' {
          // Register constructor
          if (current_class) {
              if (debug) printf("[CLASS] About to register constructor '%s::%s' with %d params\n",
                               current_class->name.c_str(), pending_method_name.c_str(),
                               (int)pending_method_param_types.size());
              MethodSignature* method = register_method(current_class->name, pending_method_name,
                                                        pending_method_param_types, pending_method_return_type, true, false, current_access_level);
              if (method) {
                  current_class->add_method(method);
                  current_method_signature = method;
                  current_function_return_type = method->returnType;
                  
                  // Emit TAC label
                  tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL, method->mangled_name), TACOperand());
                  if (debug) printf("[TAC] %s:\n", method->mangled_name.c_str());
                  
                  current_function_has_return = false;
                  if (debug) printf("[CLASS] Registered constructor '%s::%s' with %d params\n",
                                   current_class->name.c_str(), pending_method_name.c_str(),
                                   (int)pending_method_param_types.size());
              }
          }
      } compound_statement {
          // Generate TAC for constructor body
          if ($7 && current_class) {
              if (debug) printf("[CONSTRUCTOR] Generating TAC for constructor body\n");
              $7->generate_tac();
          }
          
          // Constructors always get implicit return
          if (current_method_signature && current_method_signature->is_constructor) {
              tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
              if(debug) printf("[Constructor] Added implicit return for constructor '%s'\n", 
                              current_method_signature->mangled_name.c_str());
          }
          
          // Clear method context
          current_method_signature = nullptr;
          current_function_return_type = Type(TYPE_ERROR);
          pending_method_param_types.clear();
          pending_method_name = "";
          pending_method_return_type = Type(TYPE_ERROR);
      }
    | '~' IDENTIFIER '(' ')' {
          // Destructor (no parameters, no return type)
          if (current_class) {
              // Finalize class before first method
              if (!class_finalized_for_methods) {
                  current_class->finalize();
                  class_finalized_for_methods = true;
                  if (debug) printf("[CLASS] Finalized class '%s' before methods, size=%d\n", 
                                   current_class->name.c_str(), current_class->total_size);
              }
              
              // Validate destructor name matches class name
              if (std::string($2) != current_class->name) {
                  SEM_ERROR(@2.first_line, "Destructor name '~%s' must match class name '~%s'",
                            $2, current_class->name.c_str());
                  semantic_error_count++;
              }
              
              pending_method_name = "~" + std::string($2);
              pending_method_return_type = Type(TYPE_VOID); // Destructors are always void
              
              if (debug) printf("[CLASS] Starting destructor '~%s()' in class '%s'\n", $2, current_class->name.c_str());
              
              // Register destructor
              std::vector<Type> empty_params;
              MethodSignature* method = register_method(current_class->name, pending_method_name,
                                                        empty_params, pending_method_return_type, false, true, current_access_level);
              if (method) {
                  current_class->add_method(method);
                  current_method_signature = method;
                  current_function_return_type = method->returnType;
                  
                  // Emit TAC label
                  tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL, method->mangled_name), TACOperand());
                  if (debug) printf("[TAC] %s:\n", method->mangled_name.c_str());
                  
                  current_function_has_return = false;
                  if (debug) printf("[CLASS] Registered destructor '%s::%s'\n",
                                   current_class->name.c_str(), pending_method_name.c_str());
              }
              free($2);
          }
      } compound_statement {
          // Generate TAC for destructor body
          if ($6 && current_class) {
              if (debug) printf("[DESTRUCTOR] Generating TAC for destructor body\n");
              $6->generate_tac();
          }
          
          // Destructors always get implicit return
          if (current_method_signature && current_method_signature->is_destructor) {
              tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
              if(debug) printf("[Destructor] Added implicit return for destructor '%s'\n", 
                              current_method_signature->mangled_name.c_str());
          }
          
          // Clear method context
          current_method_signature = nullptr;
          current_function_return_type = Type(TYPE_ERROR);
          pending_method_name = "";
          pending_method_return_type = Type(TYPE_ERROR);
      }
    | type_specifier pointer class_declarator_list ';' {
          // Data members with pointer (e.g., int* ptr;)
          current_type = Type(TYPE_ERROR);
          current_pointer_level = 0;
          current_is_array = false;
          current_array_sizes.clear();
      }
    | type_specifier IDENTIFIER '(' ')' {
          // Method with no parameters - use DEDICATED METHOD VARIABLES (not function variables)
          method_return_type_backup = current_type;  // Save clean type immediately
          if (current_class) {
              // Finalize class before first method (calculates member offsets)
              if (!class_finalized_for_methods) {
                  current_class->finalize();
                  class_finalized_for_methods = true;
                  if (debug) printf("[CLASS] Finalized class '%s' before methods, size=%d\n", 
                                   current_class->name.c_str(), current_class->total_size);
              }
              
              pending_method_name = $2;  // METHOD variable (not pending_function_name)
              pending_method_return_type = method_return_type_backup;  // METHOD variable
              pending_method_return_type.pointer_level = 0;
              
              // Check if this is a constructor - method name same as class name
              bool is_constructor = (pending_method_name == current_class->name);
              if (is_constructor) {
                  // Constructor validation: should have void return type (implicitly)
                  if (method_return_type_backup.base_type != TYPE_VOID) {
                      SEM_ERROR(@2.first_line, "Constructor '%s' cannot have explicit return type",
                                pending_method_name.c_str());
                      semantic_error_count++;
                  }
                  pending_method_return_type = Type(TYPE_VOID); // Force constructor to be void
                  if (debug) printf("[CLASS] Starting constructor '%s()' in class '%s'\n", $2, current_class->name.c_str());
              } else {
                  if (debug) printf("[CLASS] Starting method '%s()' in class '%s'\n", $2, current_class->name.c_str());
              }
              
              // Register method BEFORE compound_statement so we can emit label
              std::vector<Type> empty_params;
              MethodSignature* method = register_method(current_class->name, pending_method_name,
                                                        empty_params, pending_method_return_type, is_constructor, false, current_access_level);
              if (method) {
                  current_class->add_method(method);
                  
                  // Set current method context for member access inside method body
                  current_method_signature = method;
                  
                  // Set function return type for return statement validation
                  current_function_return_type = method->returnType;
                  
                  // Emit TAC label for method entry using mangled name
                  tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL, method->mangled_name), TACOperand());
                  if (debug) printf("[TAC] %s:\n", method->mangled_name.c_str());
                  
                  // Reset return flag for new method
                  current_function_has_return = false;
                  
                  if (debug) printf("[CLASS] Registered %s '%s::%s' (no params)\n",
                                   is_constructor ? "constructor" : "method",
                                   current_class->name.c_str(), pending_method_name.c_str());
              }
              free($2);
          }
      } compound_statement {
          // Generate TAC for method body
          if ($6 && current_class) {
              if (debug) printf("[METHOD] Generating TAC for method body\n");
              $6->generate_tac();
          }
          
          // Check for missing return statements (same as function)
          if (current_method_signature && !current_function_has_return) {
              if (current_function_return_type.base_type == TYPE_VOID || current_method_signature->is_constructor) {
                  // Emit implicit return for void methods and constructors
                  tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
                  if(debug) printf("[Method] Added implicit return for %s '%s'\n", 
                                  current_method_signature->is_constructor ? "constructor" : "void method",
                                  current_method_signature->mangled_name.c_str());
              } else {
                  // Error for non-void methods without return
                  SEM_ERROR(yylloc.first_line, "Method '%s::%s' with non-void return type must have a return statement",
                            current_class->name.c_str(), pending_method_name.c_str());
                  semantic_error_count++;
              }
          }
          
          // Clear current method context
          current_method_signature = nullptr;
          current_function_return_type = Type(TYPE_ERROR);
          
          // Reset METHOD-specific state
          pending_method_name = "";
          pending_method_return_type = Type(TYPE_ERROR);
          method_return_type_backup = Type(TYPE_ERROR);
          current_type = Type(TYPE_ERROR);
      }
    | type_specifier IDENTIFIER '(' {
          // Method with parameters - use DEDICATED METHOD VARIABLES (not function variables)
          method_return_type_backup = current_type;  // Save clean type immediately
          if (current_class) {
              // Finalize class before first method (calculates member offsets)
              if (!class_finalized_for_methods) {
                  current_class->finalize();
                  class_finalized_for_methods = true;
                  if (debug) printf("[CLASS] Finalized class '%s' before methods, size=%d\n", 
                                   current_class->name.c_str(), current_class->total_size);
              }
              
              pending_method_name = $2;  // METHOD variable (not pending_function_name)
              pending_method_return_type = method_return_type_backup;  // METHOD variable
              pending_method_return_type.pointer_level = 0;
              
              // Check if this is a constructor - method name same as class name
              bool is_constructor = (pending_method_name == current_class->name);
              if (is_constructor) {
                  // Constructor validation: should have void return type (implicitly)
                  if (method_return_type_backup.base_type != TYPE_VOID) {
                      SEM_ERROR(@2.first_line, "Constructor '%s' cannot have explicit return type",
                                pending_method_name.c_str());
                      semantic_error_count++;
                  }
                  pending_method_return_type = Type(TYPE_VOID); // Force constructor to be void
                  if (debug) printf("[CLASS] Starting constructor '%s(...)' in class '%s'\n", $2, current_class->name.c_str());
              } else {
                  if (debug) printf("[CLASS] Starting method '%s(...)' in class '%s', return type=%s\n", 
                                   $2, current_class->name.c_str(), method_return_type_backup.to_string().c_str());
              }
              free($2);
          }
      } parameter_type_list ')' {
          // Register method BEFORE compound_statement so we can emit label
          if (current_class) {
              bool is_constructor = (pending_method_name == current_class->name);
              if (debug) printf("[CLASS] About to register %s '%s::%s' with return type=%s, %d params\n",
                               is_constructor ? "constructor" : "method",
                               current_class->name.c_str(), pending_method_name.c_str(),
                               pending_method_return_type.to_string().c_str(),
                               (int)pending_method_param_types.size());
              MethodSignature* method = register_method(current_class->name, pending_method_name,
                                                        pending_method_param_types, pending_method_return_type, is_constructor, false, current_access_level);
              if (method) {
                  current_class->add_method(method);
                  
                  // Set current method context for member access inside method body
                  current_method_signature = method;
                  
                  // Set function return type for return statement validation
                  current_function_return_type = method->returnType;
                  
                  // Emit TAC label for method entry using mangled name
                  tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL, method->mangled_name), TACOperand());
                  if (debug) printf("[TAC] %s:\n", method->mangled_name.c_str());
                  
                  // Reset return flag for new method
                  current_function_has_return = false;
                  
                  if (debug) printf("[CLASS] Registered %s '%s::%s' with %d params\n",
                                   is_constructor ? "constructor" : "method",
                                   current_class->name.c_str(), pending_method_name.c_str(),
                                   (int)pending_method_param_types.size());
              }
          }
      } compound_statement {
          // Generate TAC for method body
          if ($8 && current_class) {
              if (debug) printf("[METHOD] Generating TAC for method body\n");
              $8->generate_tac();
          }
          
          // Check for missing return statements (same as function)
          if (current_method_signature && !current_function_has_return) {
              if (current_function_return_type.base_type == TYPE_VOID || current_method_signature->is_constructor) {
                  // Emit implicit return for void methods and constructors
                  tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
                  if(debug) printf("[Method] Added implicit return for %s '%s'\n", 
                                  current_method_signature->is_constructor ? "constructor" : "void method",
                                  current_method_signature->mangled_name.c_str());
              } else {
                  // Error for non-void methods without return
                  SEM_ERROR(yylloc.first_line, "Method '%s::%s' with non-void return type must have a return statement",
                            current_class->name.c_str(), pending_method_name.c_str());
                  semantic_error_count++;
              }
          }
          
          // Clear current method context
          current_method_signature = nullptr;
          current_function_return_type = Type(TYPE_ERROR);
          
          // Reset METHOD-specific state
          pending_method_param_types.clear();  // Clear method params (not pending_method_params)
          pending_method_name = "";
          pending_method_return_type = Type(TYPE_ERROR);
          method_return_type_backup = Type(TYPE_ERROR);
          current_type = Type(TYPE_ERROR);
      }
    | type_specifier pointer IDENTIFIER '(' ')' {
          // Method with pointer return type, no parameters - use METHOD VARIABLES
          method_return_type_backup = current_type;  // Save clean type
          if (current_class) {
              // Finalize class before first method (calculates member offsets)
              if (!class_finalized_for_methods) {
                  current_class->finalize();
                  class_finalized_for_methods = true;
                  if (debug) printf("[CLASS] Finalized class '%s' before methods, size=%d\n", 
                                   current_class->name.c_str(), current_class->total_size);
              }
              
              pending_method_name = $3;  // METHOD variable
              pending_method_return_type = method_return_type_backup;  // METHOD variable
              pending_method_return_type.pointer_level = $2;
              if (debug) printf("[CLASS] Starting method '%s()' (ptr return) in class '%s'\n", $3, current_class->name.c_str());
              
              // Register method BEFORE compound_statement
              std::vector<Type> empty_params;
              // Check if this is a constructor
              bool is_constructor = (pending_method_name == current_class->name);
              if (is_constructor) {
                  // Constructor validation: should have void return type (implicitly)
                  if (pending_method_return_type.base_type != TYPE_VOID || pending_method_return_type.pointer_level != 0) {
                      SEM_ERROR(@3.first_line, "Constructor '%s' cannot have explicit return type",
                                pending_method_name.c_str());
                      semantic_error_count++;
                  }
                  pending_method_return_type = Type(TYPE_VOID); // Force constructor to be void
              }

              MethodSignature* method = register_method(current_class->name, pending_method_name,
                                                        empty_params, pending_method_return_type, is_constructor, false, current_access_level);
              if (method) {
                  current_class->add_method(method);
                  
                  // Set current method context for member access inside method body
                  current_method_signature = method;
                  
                  // Set function return type for return statement validation
                  current_function_return_type = method->returnType;
                  
                  // Emit TAC label for method entry using mangled name
                  tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL, method->mangled_name), TACOperand());
                  if (debug) printf("[TAC] %s:\n", method->mangled_name.c_str());
                  
                  // Reset return flag for new method
                  current_function_has_return = false;
                  
                  if (debug) printf("[CLASS] Registered %s '%s::%s' (ptr return, no params)\n",
                                   is_constructor ? "constructor" : "method",
                                   current_class->name.c_str(), pending_method_name.c_str());
              }
              free($3);
          }
      } compound_statement {
          // Generate TAC for method body
          if ($7 && current_class) {
              if (debug) printf("[METHOD] Generating TAC for method body\n");
              $7->generate_tac();
          }
          
          // Check for missing return statements (same as function)
          if (current_method_signature && !current_function_has_return) {
              if (current_function_return_type.base_type == TYPE_VOID || current_method_signature->is_constructor || current_method_signature->is_destructor) {
                  // Emit implicit return for void methods, constructors, and destructors
                  tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
                  const char* method_type = current_method_signature->is_constructor ? "constructor" : 
                                           (current_method_signature->is_destructor ? "destructor" : "void method");
                  if(debug) printf("[Method] Added implicit return for %s '%s'\n", 
                                  method_type, current_method_signature->mangled_name.c_str());
              } else {
                  // Error for non-void methods without return
                  SEM_ERROR(yylloc.first_line, "Method '%s::%s' with non-void return type must have a return statement",
                            current_class->name.c_str(), pending_method_name.c_str());
                  semantic_error_count++;
              }
          }
          
          // Clear current method context
          current_method_signature = nullptr;
          current_function_return_type = Type(TYPE_ERROR);
          
          pending_method_name = "";
          pending_method_return_type = Type(TYPE_ERROR);
          method_return_type_backup = Type(TYPE_ERROR);
          current_type = Type(TYPE_ERROR);
      }
    | type_specifier pointer IDENTIFIER '(' {
          // Method with pointer return type and parameters - use METHOD VARIABLES
          method_return_type_backup = current_type;  // Save clean type
          if (current_class) {
              // Finalize class before first method (calculates member offsets)
              if (!class_finalized_for_methods) {
                  current_class->finalize();
                  class_finalized_for_methods = true;
                  if (debug) printf("[CLASS] Finalized class '%s' before methods, size=%d\n", 
                                   current_class->name.c_str(), current_class->total_size);
              }
              
              pending_method_name = $3;  // METHOD variable
              pending_method_return_type = method_return_type_backup;  // METHOD variable
              pending_method_return_type.pointer_level = $2;
              if (debug) printf("[CLASS] Starting method '%s(...)' (ptr return) in class '%s'\n", $3, current_class->name.c_str());
              free($3);
          }
      } parameter_type_list ')' {
          // Register method BEFORE compound_statement
          if (current_class) {
              // Check if this is a constructor
              bool is_constructor = (pending_method_name == current_class->name);
              if (is_constructor) {
                  // Constructor validation: should have void return type (implicitly)
                  if (pending_method_return_type.base_type != TYPE_VOID || pending_method_return_type.pointer_level != 0) {
                      SEM_ERROR(@3.first_line, "Constructor '%s' cannot have explicit return type",
                                pending_method_name.c_str());
                      semantic_error_count++;
                  }
                  pending_method_return_type = Type(TYPE_VOID); // Force constructor to be void
              }

              if (debug) printf("[CLASS] About to register %s '%s::%s' (ptr return) with %d params\n",
                               is_constructor ? "constructor" : "method",
                               current_class->name.c_str(), pending_method_name.c_str(),
                               (int)pending_method_param_types.size());
              MethodSignature* method = register_method(current_class->name, pending_method_name,
                                                        pending_method_param_types, pending_method_return_type, is_constructor, false, current_access_level);
              if (method) {
                  current_class->add_method(method);
                  
                  // Set current method context for member access inside method body
                  current_method_signature = method;
                  
                  // Set function return type for return statement validation
                  current_function_return_type = method->returnType;
                  
                  // Emit TAC label for method entry using mangled name
                  tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL, method->mangled_name), TACOperand());
                  if (debug) printf("[TAC] %s:\n", method->mangled_name.c_str());
                  
                  // Reset return flag for new method
                  current_function_has_return = false;
                  
                  if (debug) printf("[CLASS] Registered %s '%s::%s' (ptr return, %d params)\n",
                                   is_constructor ? "constructor" : "method",
                                   current_class->name.c_str(), pending_method_name.c_str(),
                                   (int)pending_method_param_types.size());
              }
          }
      } compound_statement {
          // Generate TAC for method body
          if ($9 && current_class) {
              if (debug) printf("[METHOD] Generating TAC for method body\n");
              $9->generate_tac();
          }
          
          // Check for missing return statements (same as function)
          if (current_method_signature && !current_function_has_return) {
              if (current_function_return_type.base_type == TYPE_VOID || current_method_signature->is_constructor || current_method_signature->is_destructor) {
                  // Emit implicit return for void methods, constructors, and destructors
                  tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
                  const char* method_type = current_method_signature->is_constructor ? "constructor" : 
                                           (current_method_signature->is_destructor ? "destructor" : "void method");
                  if(debug) printf("[Method] Added implicit return for %s '%s'\n", 
                                  method_type, current_method_signature->mangled_name.c_str());
              } else {
                  // Error for non-void methods without return
                  SEM_ERROR(yylloc.first_line, "Method '%s::%s' with non-void return type must have a return statement",
                            current_class->name.c_str(), pending_method_name.c_str());
                  semantic_error_count++;
              }
          }
          
          // Clear current method context
          current_method_signature = nullptr;
          current_function_return_type = Type(TYPE_ERROR);
          
          pending_method_param_types.clear();  // Clear method params (not pending_method_params)
          pending_method_name = "";
          pending_method_return_type = Type(TYPE_ERROR);
          method_return_type_backup = Type(TYPE_ERROR);
          current_type = Type(TYPE_ERROR);
      }
    ;

class_declarator_list
    : class_declarator
    | class_declarator_list ',' class_declarator
    ;

class_declarator
    : declarator {
          // Add member to current class
          if (current_class && $1) {
              Type* member_type = new Type(current_type);
              member_type->pointer_level = current_pointer_level;
              member_type->is_array = current_is_array;
              member_type->array_dim = current_array_sizes.size();
              member_type->array_sizes = current_array_sizes;
              
              current_class->add_member($1, member_type, @1.first_line, current_access_level);
              if (debug) printf("[CLASS] Added member '%s' of type '%s' to class '%s'\n", 
                               $1, member_type->to_string().c_str(), current_class->name.c_str());
              
              // Reset for next member
              current_pointer_level = 0;
              current_is_array = false;
              current_array_sizes.clear();
              free($1);
          }
      }
    | ':' constant_expression {
          // Bit-field (not fully implemented for classes, just skip)
          if (debug) printf("[CLASS] Bit-field (skipped)\n");
      }
    | declarator ':' constant_expression {
          // Bit-field with declarator (not fully implemented)
          if ($1) {
              Type* member_type = new Type(current_type);
              member_type->pointer_level = current_pointer_level;
              current_class->add_member($1, member_type, @1.first_line, current_access_level);
              if (debug) printf("[CLASS] Added bit-field member '%s' (simplified)\n", $1);
              current_pointer_level = 0;
              current_is_array = false;
              current_array_sizes.clear();
              free($1);
          }
      }
    ;

struct_or_union_specifier
    : struct_or_union IDENTIFIER '{' {
          // Check if struct already exists in current scope
          if (struct_exists_in_current_scope($2)) {
              SEM_ERROR(@2.first_line, "Redefinition of struct '%s'", $2);
              semantic_error_count++;
              current_struct = nullptr;
          } else {
              // Create new struct type
              current_struct = new StructType($2);
              // Mark as union if needed
              current_struct->is_union = current_is_parsing_union;
              register_struct_in_scope($2, current_struct);
              if (debug) printf("[STRUCT] Created struct '%s'\n", $2);
          }
      } struct_declaration_list '}' {
          // Finalize struct: calculate sizes and offsets (only if successfully created)
          if (current_struct) {
              current_struct->finalize();
              if (debug) printf("[STRUCT] Finalized struct '%s', size=%d\n", current_struct->name.c_str(), current_struct->total_size);
          }
          
          // Set current_type to this struct
          current_type = Type();
          current_type.is_struct = true;
          current_type.is_union = current_struct->is_union;
          current_type.struct_name = $2;
          current_type.struct_type_ptr = lookup_struct_in_scope($2);
          current_struct = nullptr;
          
          // NOTE: Don't reset in_function_declaration here, as struct can be a return type
          
          free($2);
      }
    | struct_or_union '{' {
          // Anonymous struct - create temporary name
          std::string temp_name = "__anon_struct_" + std::to_string(next_scope_id);
          current_struct = new StructType(temp_name);
          current_struct->is_union = current_is_parsing_union;
          register_struct_in_scope(temp_name, current_struct);
          if (debug) printf("[STRUCT] Created anonymous struct\n");
      } struct_declaration_list '}' {
          // Finalize struct
          current_struct->finalize();
          if (debug) printf("[STRUCT] Finalized anonymous struct, size=%d\n", current_struct->total_size);
          
          // Set current_type to this struct
          current_type = Type();
          current_type.is_struct = true;
          current_type.is_union = current_struct->is_union;
          current_type.struct_name = current_struct->name;
          current_type.struct_type_ptr = current_struct;
          current_struct = nullptr;
          
          // NOTE: Don't reset in_function_declaration here
      }
    | struct_or_union IDENTIFIER '{' '}' {
          // Empty struct definition
          StructType* st = new StructType($2);
          st->is_union = current_is_parsing_union;
          register_struct_in_scope($2, st);
          st->finalize();
          
          current_type = Type();
          current_type.is_struct = true;
          current_type.is_union = st->is_union;
          current_type.struct_name = $2;
          current_type.struct_type_ptr = st;
          if (debug) printf("[STRUCT] Created empty struct '%s'\n", $2);
          free($2);
      }
    | struct_or_union '{' '}' {
          // Anonymous empty struct
          std::string temp_name = "__anon_struct_" + std::to_string(next_scope_id);
          StructType* st = new StructType(temp_name);
          st->is_union = current_is_parsing_union;
          register_struct_in_scope(temp_name, st);
          st->finalize();
          
          current_type = Type();
          current_type.is_struct = true;
          current_type.is_union = st->is_union;
          current_type.struct_name = temp_name;
          current_type.struct_type_ptr = st;
          if (debug) printf("[STRUCT] Created anonymous empty struct\n");
      }
    | struct_or_union IDENTIFIER {
          // Reference to existing struct (usage, not definition)
          StructType* st = lookup_struct_in_scope($2);
          if (st) {
              // Check kind matches (struct vs union)
              if (st->is_union != current_is_parsing_union) {
                  SEM_ERROR(@2.first_line, "Tag '%s' is a %s, not a %s",
                            $2, st->is_union ? "union" : "struct", st->is_union ? "struct" : "union");
                  semantic_error_count++;
              }
              current_type = Type();
              current_type.is_struct = true;
              current_type.is_union = st->is_union;
              current_type.struct_name = $2;
              current_type.struct_type_ptr = st;
              if (debug) printf("[STRUCT] Using struct '%s'\n", $2);
          } else {
              SEM_ERROR(@2.first_line, "Struct '%s' not defined", $2);
              semantic_error_count++;
              current_type = Type(TYPE_ERROR);
          }
          free($2);
      }
    ;

struct_declaration_list
    : struct_declaration
    | struct_declaration_list struct_declaration
    ;

struct_declaration
    : specifier_qualifier_list struct_declarator_list ';' {
          // Members have been added to current_struct during struct_declarator_list
          // Just reset current_type for next declaration
          current_type = Type(TYPE_ERROR);
          current_pointer_level = 0;
          current_is_array = false;
          current_array_sizes.clear();
      }
    ;

struct_declarator_list
    : struct_declarator
    | struct_declarator_list ',' struct_declarator
    ;

struct_declarator
    : declarator {
          // Add member to current struct
          if (current_struct && $1) {
              Type* member_type = new Type(current_type);
              member_type->pointer_level = current_pointer_level;
              member_type->is_array = current_is_array;
              member_type->array_dim = current_array_sizes.size();
              member_type->array_sizes = current_array_sizes;
              
              current_struct->add_member($1, member_type, @1.first_line);
              if (debug) printf("[STRUCT] Added member '%s' of type '%s' to struct '%s'\n", 
                               $1, member_type->to_string().c_str(), current_struct->name.c_str());
              
              // Reset for next member
              current_pointer_level = 0;
              current_is_array = false;
              current_array_sizes.clear();
              free($1);
          }
      }
    | ':' constant_expression {
          // Bit-field (not fully implemented, just skip)
          if (debug) printf("[STRUCT] Bit-field (skipped)\n");
      }
    | declarator ':' constant_expression {
          // Bit-field with declarator (not fully implemented)
          if ($1) {
              Type* member_type = new Type(current_type);
              member_type->pointer_level = current_pointer_level;
              current_struct->add_member($1, member_type, @1.first_line);
              if (debug) printf("[STRUCT] Added bit-field member '%s' (simplified)\n", $1);
              current_pointer_level = 0;
              current_is_array = false;
              current_array_sizes.clear();
              free($1);
          }
      }
    ;

struct_or_union
    : STRUCT { current_is_parsing_union = false; }
    | UNION  { current_is_parsing_union = true; }
    ;

enum_specifier
	: ENUM '{' {
          // Anonymous enum - create temporary name
          std::string temp_name = "__anon_enum_" + std::to_string(next_scope_id);
          EnumType* et = new EnumType(temp_name);
          register_enum(temp_name, et);
          current_enum = et;
      } enumerator_list '}' {
          current_enum = nullptr;
          if (debug) printf("[ENUM] Created anonymous enum\n");
      }
	| ENUM IDENTIFIER '{' {
          // Named enum - create with identifier name
          EnumType* et = new EnumType($2);
          register_enum($2, et);
          current_enum = et;
          if (debug) printf("[ENUM] Created enum '%s'\n", $2);
      } enumerator_list '}' {
          current_enum = nullptr;
          free($2);
      }
	| ENUM IDENTIFIER {
          // Enum usage (not definition) - just reference existing enum
          if (lookup_enum($2)) {
              if (debug) printf("[ENUM] Using enum '%s'\n", $2);
          } else {
              SEM_ERROR(@2.first_line, "Enum '%s' not defined", $2);
              semantic_error_count++;
          }
          free($2);
      }
	;

enumerator_list
	: enumerator
	| enumerator_list ',' enumerator
	;

enumerator
	: IDENTIFIER {
          if (current_enum) {
              // Auto-increment value
              int value = current_enum->next_value;
              current_enum->add_member($1, value);
              
              if (debug) printf("[ENUM] Member '%s' = %d\n", $1, value);
              
              // Insert enum member as integer constant in symbol table
              SymbolTable* st = current_scope();
              if (st) {
                  Symbol* sym = st->insert($1, Type(TYPE_INT));
                  if (sym) {
                      sym->is_enum_constant = true;
                      sym->enum_value = value;
                  }
              }
          }
          free($1);
      }
	| IDENTIFIER '=' constant_expression {
          if (current_enum) {
              // Evaluate constant expression to get explicit value
              $3->generate_tac();
              int value = 0;
              
              // Extract integer value from constant expression
              PrimaryExpression* prim = dynamic_cast<PrimaryExpression*>($3);
              if (prim && prim->prim_type == PrimaryExpression::PRIM_INT_CONSTANT) {
                  value = prim->int_value;
              } else if ($3->result && $3->result->type == TACOperand::OPERAND_CONSTANT) {
                  value = atoi($3->result->name.c_str());
              } else {
                  SEM_ERROR(@3.first_line, "Enum value must be an integer constant");
                  semantic_error_count++;
              }
              
              current_enum->add_member($1, value);
              
              if (debug) printf("[ENUM] Member '%s' = %d (explicit)\n", $1, value);
              
              // Insert enum member as integer constant in symbol table
              SymbolTable* st = current_scope();
              if (st) {
                  Symbol* sym = st->insert($1, Type(TYPE_INT));
                  if (sym) {
                      sym->is_enum_constant = true;
                      sym->enum_value = value;
                  }
              }
              
              delete $3;
          }
          free($1);
      }
	;

specifier_qualifier_list
	: type_specifier specifier_qualifier_list
	| type_specifier
	| type_qualifier specifier_qualifier_list
	| type_qualifier
	;

type_qualifier
	: CONST
	| VOLATILE
	;

declarator
	: pointer direct_declarator        
        { 
            // Methods use their own separate tracking, so this is safe for functions
            pending_function_return_type = current_type;
            pending_function_return_type.pointer_level = $1;
            current_pointer_level = $1;
            $$ = $2; 
        }
	| direct_declarator               
        { 
            // Methods use their own separate tracking, so this is safe for functions
            pending_function_return_type = current_type;
            pending_function_return_type.pointer_level = 0;
            current_pointer_level = 0;
            $$ = $1; 
        }
	| '&' direct_declarator           
        { 
            // Methods use their own separate tracking, so this is safe for functions
            pending_function_return_type = current_type;
            pending_function_return_type.pointer_level = 0;
            current_pointer_level = 0;
            $$ = $2; 
        }
	;

direct_declarator
    : IDENTIFIER                      
        { 
            $$ = $1; 
        }
    | '(' declarator ')'              
        { 
            $$ = $2; 
        }
    | direct_declarator '[' expression ']'
        {
            // Array declarator: arr[expr]
            // Evaluate expression to perform type checking and possibly constant-fold
            $3->generate_tac();
            int array_size = 0;

            // Type check: array dimension must be integer type (not float)
            if (!$3->type) {
                SEM_ERROR(@3.first_line, "Array dimension has no type information");
                semantic_error_count++;
                array_size = 1; // Safe default
            } else if ($3->type->base_type == TYPE_FLOAT) {
                SEM_ERROR(@3.first_line, "Array dimension must be integer, not float");
                semantic_error_count++;
                array_size = 1; // Safe default
            } else {
                // If expression is a compile-time integer constant, use it as size
                PrimaryExpression* prim = dynamic_cast<PrimaryExpression*>($3);
                if (prim && prim->prim_type == PrimaryExpression::PRIM_INT_CONSTANT) {
                    array_size = prim->int_value;
                }
                else if (prim && prim->prim_type == PrimaryExpression::PRIM_CHAR_CONSTANT) {
                    array_size = (int)prim->char_value; // char constant allowed
                }
                else if ($3->result && $3->result->type == TACOperand::OPERAND_CONSTANT) {
                    // Fallback: try to parse as integer from TAC operand
                    array_size = atoi($3->result->name.c_str());
                }
                else {
                    // Non-constant but integer-typed expression -> Variable Length Array (VLA)
                    if ($3->type->is_integer()) {
                        array_size = -1; // Use -1 to mark runtime-sized dimension (VLA)
                    } else {
                        SEM_ERROR(@3.first_line, "Array size must be an integer expression");
                        semantic_error_count++;
                        array_size = 1; // Safe default
                    }
                }

                // If we have a compile-time size, validate positivity
                if (array_size > 0) {
                    // OK
                } else if (array_size == 0) {
                    SEM_ERROR(@3.first_line, "Array size must be positive (got %d)", array_size);
                    semantic_error_count++;
                    array_size = 1; // Safe default
                }
                // if array_size == -1 -> VLA, allowed
            }

            // Add to array sizes (in reverse order, will be reversed later)
            current_array_sizes.push_back(array_size);
            current_is_array = true;

            $$ = $1;  // Return the base identifier
        }
    | direct_declarator '[' ']'
        {
            // Unsized array: arr[]
            current_array_sizes.push_back(0);  // 0 means unspecified
            current_is_array = true;
            $$ = $1;
        }
        | direct_declarator '(' parameter_type_list ')' {
                    $$ = $1;
                    // Just mark that this is a function and save the name
                    pending_function_header = true;
                    if ($1) pending_function_name = std::string($1);
            }

        | direct_declarator '(' ')' {
                    $$ = $1;
                    // Just mark that this is a function and save the name
                    pending_function_header = true;
                    if ($1) pending_function_name = std::string($1);
            }
    ;


pointer
    : '*' type_qualifier_list_opt pointer_opt
        { $$ = 1 + $3; }
    ;

type_qualifier_list_opt
    : /* empty */
        { $$ = 0; }
    | type_qualifier_list
        { $$ = 0; }
    ;

pointer_opt
    : /* empty */
        { $$ = 0; }
    | pointer
        { $$ = $1; }
    ;

type_qualifier_list
	: type_qualifier
	| type_qualifier_list type_qualifier
	;

parameter_type_list
	: parameter_list
	| parameter_list ',' ELLIPSIS
	;

parameter_list
	: parameter_declaration
	| parameter_list ',' parameter_declaration
	;
    
parameter_declaration
    : declaration_specifiers declarator {
          // $1 sets current_type; $2 is the parameter name
          $$ = $2;
          if ($2) {
              Type param_type = current_type;
              param_type.pointer_level = current_pointer_level;
              param_type.is_array = current_is_array;
              param_type.array_dim = current_array_sizes.size();
              param_type.array_sizes = current_array_sizes;

              // Reset declarator modifiers for safety
              current_pointer_level = 0;
              current_is_array = false;
              current_array_sizes.clear();

              // Add to BOTH function and method parameter lists (they can share this)
              pending_function_params.emplace_back(std::string($2), param_type);
              pending_function_param_types.push_back(param_type);
              pending_method_param_types.push_back(param_type);  // Also add to method params
              
              // Methods have their own return type tracking that should NOT be touched here
              if (!current_class) {
                  current_type = pending_function_return_type;
              }
              // If we're in a class, DON'T restore - method return type is in pending_method_return_type
          }
      }
    | declaration_specifiers abstract_declarator
    | declaration_specifiers
    ;

identifier_list
    : IDENTIFIER {
          $$ = nullptr;
      }
    | identifier_list ',' IDENTIFIER {
          $$ = nullptr;
      }
    ;


type_name
	: specifier_qualifier_list
	| specifier_qualifier_list abstract_declarator
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
	: assignment_expression
        { $$ = $1; }
	| '{' initializer_list '}'
        { $$ = create_array_initializer_expression(*$2); delete $2; }
	| '{' initializer_list ',' '}'
        { $$ = create_array_initializer_expression(*$2); delete $2; }
	;

initializer_list
	: initializer
        { 
            $$ = new std::vector<Expression*>(); 
            $$->push_back($1); 
        }
	| initializer_list ',' initializer
        { 
            $1->push_back($3); 
            $$ = $1; 
        }
	;

statement
	: labeled_statement
        { $$ = $1; }
	| compound_statement
        { $$ = $1; }
	| expression_statement
        { $$ = $1; }
	| selection_statement
        { $$ = $1; }
	| iteration_statement
        { $$ = $1; }
	| jump_statement
        { $$ = $1; }
	| declaration
        { 
            /* Declarations used as statements should be added to the statement list */
            /* Insert into symbol table immediately (during parsing, while scope is active) */
            /* But defer TAC generation to preserve execution order */
            if ($1) {
                /* Insert symbol only if not already inserted (e.g., declarations with initializers) */
                VariableDeclaration* var_decl = dynamic_cast<VariableDeclaration*>($1);
                if (var_decl && !var_decl->inserted_symbol) {
                    $1->insert_symbol();
                }
                /* Wrap in DeclarationStatement for deferred TAC generation */
                $$ = create_declaration_statement($1);
            } else {
                $$ = nullptr;
            }
        }
	;

labeled_statement
    : IDENTIFIER ':' statement
        { $$ = create_label_statement(std::string($1), $3); }
    | CASE constant_expression ':' statement
        { $$ = create_case_label($2, $4); }
    | DEFAULT ':' statement
        { $$ = create_default_label($3); }
    ;


compound_statement
    : '{' 
        { 
            /* Enter new scope when opening brace */
            push_scope(); 
            /* If entering a function body, insert any pending parameters */
            if (!pending_function_params.empty()) {
                SymbolTable* st = current_scope();
                for (auto &pp : pending_function_params) {
                    st->insert(pp.first, pp.second);
                }
                pending_function_params.clear();
            }
        }
      '}'
        { 
            /* Exit scope when closing brace - but keep symbols for TAC generation */
            pop_scope();
            $$ = create_compound_statement(); 
        }
    | '{' 
        { 
            /* Enter new scope when opening brace */
            push_scope(); 
            /* If entering a function body, insert any pending parameters */
            if (!pending_function_params.empty()) {
                SymbolTable* st = current_scope();
                for (auto &pp : pending_function_params) {
                    st->insert(pp.first, pp.second);
                }
                pending_function_params.clear();
            }
        }
      statement_list '}'
        { 
            /* Exit scope when closing brace - but keep symbols for TAC generation */
            pop_scope();
            $$ = $3;  /* Note: shifted by mid-rule action */ 
        }
    ;


declaration_list
	: declaration 
	| declaration_list declaration 
	;

statement_list
	: statement
        {
            CompoundStatement* compound = create_compound_statement();
            if ($1) {
                compound->add_statement($1);
            }
            $$ = compound;
        }
	| statement_list statement
        {
            CompoundStatement* compound = dynamic_cast<CompoundStatement*>($1);
            if (compound && $2) {
                compound->add_statement($2);
            }
            $$ = $1;
        }
	;

expression_statement
	: ';'
        { $$ = create_expression_statement(nullptr); }
	| expression ';'
        { $$ = create_expression_statement($1); }
	;

selection_statement
    : IF '(' expression ')' statement %prec THEN
        { $$ = create_if_statement($3, $5, nullptr); }
	| IF '(' expression ')' statement ELSE statement
        { $$ = create_if_statement($3, $5, $7); }
	| SWITCH '(' expression ')' statement
        { $$ = create_switch_statement($3, $5); }
    ;

iteration_statement
	: WHILE '(' expression ')' statement
        { $$ = create_while_statement($3, $5); }
	| UNTIL '(' expression ')' statement
        { $$ = create_until_statement($3, $5); }
	| DO statement WHILE '(' expression ')' ';'
        { $$ = create_dowhile_statement($2, $5); }
	| FOR '(' expression_statement expression_statement ')' statement
        { 
            // Extract expression from expression_statement for condition
            Expression* cond = nullptr;
            if ($4) {
                ExpressionStatement* expr_stmt = dynamic_cast<ExpressionStatement*>($4);
                if (expr_stmt) cond = expr_stmt->expr;
            }
            $$ = create_for_statement($3, cond, nullptr, $6); 
        }
	| FOR '(' expression_statement expression_statement expression ')' statement
        { 
            Expression* cond = nullptr;
            if ($4) {
                ExpressionStatement* expr_stmt = dynamic_cast<ExpressionStatement*>($4);
                if (expr_stmt) cond = expr_stmt->expr;
            }
            $$ = create_for_statement($3, cond, $5, $7); 
        }
	| FOR '(' declaration expression_statement ')' statement
        { 
            // Wrap declaration in DeclarationStatement
            // (symbol already inserted during declaration parsing for declarations with initializers)
            Statement* init = $3 ? create_declaration_statement($3) : nullptr;
            
            Expression* cond = nullptr;
            if ($4) {
                ExpressionStatement* expr_stmt = dynamic_cast<ExpressionStatement*>($4);
                if (expr_stmt) cond = expr_stmt->expr;
            }
            $$ = create_for_statement(init, cond, nullptr, $6); 
        }
	| FOR '(' declaration expression_statement expression ')' statement
        { 
            // Wrap declaration in DeclarationStatement
            // (symbol already inserted during declaration parsing for declarations with initializers)
            Statement* init = $3 ? create_declaration_statement($3) : nullptr;
            
            Expression* cond = nullptr;
            if ($4) {
                ExpressionStatement* expr_stmt = dynamic_cast<ExpressionStatement*>($4);
                if (expr_stmt) cond = expr_stmt->expr;
            }
            $$ = create_for_statement(init, cond, $5, $7); 
        }
	;

jump_statement
    : GOTO IDENTIFIER ';'
        { $$ = create_goto_statement(std::string($2)); }
    | CONTINUE ';'
        { $$ = create_continue_statement(); }
    | BREAK ';'
        { $$ = create_break_statement(); }
    | RETURN ';'
        { $$ = create_return_statement(nullptr); }
    | RETURN expression ';'
        { $$ = create_return_statement($2); }
    ;

translation_unit
	: external_declaration
	| translation_unit external_declaration
	;

external_declaration
	: function_definition 
    | declaration
        {
            // Handle global declarations: insert into global scope and generate TAC
            // First, handle all pending declarators from comma-separated list
            for (Declaration* decl : pending_declarator_tac) {
                if (decl) {
                    decl->generate_tac();
                    delete decl;
                }
            }
            pending_declarator_tac.clear();
            
            // Then handle the final declarator
            if ($1) {
                /* Insert symbol only if not already inserted (e.g., declarations with initializers) */
                VariableDeclaration* var_decl = dynamic_cast<VariableDeclaration*>($1);
                if (var_decl && !var_decl->inserted_symbol) {
                    $1->insert_symbol();
                }
                $1->generate_tac();
                // Optional: clean up the declaration node since it's not wrapped in a statement
                delete $1;
            }
        }
	;
    
function_definition
    : declaration_specifiers declarator declaration_list
        {
            current_function_return_type = backup_current_type;
            current_function_return_type.pointer_level = current_pointer_level;
            
            if(debug) printf("\n[DEBUG] Function return type: %s (contaminated current_type: %s)\n", 
                current_function_return_type.to_string().c_str(), current_type.to_string().c_str());
                
            // Reset function declaration flag for next function
            in_function_declaration = false;
            // Register function signature with captured pending_function_params
            FunctionSignature *func_sig = register_function(std::string($2), pending_function_param_types, current_function_return_type);
            pending_function_param_types.clear();
            // Set current function for static variable handling BEFORE parsing compound statement
            set_current_function(func_sig);
            // Emit function entry label with function name
            tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL,mangle_function_for_tac(std::string($2), *func_sig)), TACOperand());
            
            // Reset return flag for new function
            current_function_has_return = false;
        }
        compound_statement
        {
            if ($5) {
                if(debug) printf("\n[Function] Generating TAC for function body\n");
                $5->generate_tac();
            }
            
            // Check for missing return statements
            if (!current_function_has_return) {
                if (current_function_return_type.base_type == TYPE_VOID) {
                    // Emit implicit return for void functions
                    tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
                    if(debug) printf("[Function] Added implicit return for void function '%s'\n", std::string($2).c_str());
                } else {
                    // Error for non-void functions without return
                    SEM_ERROR(yylloc.first_line, "Function '%s' with non-void return type must have a return statement",
                              std::string($2).c_str());
                    semantic_error_count++;
                }
            }
            // Always add a final return TAC as a safety net
            tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
            
            tacGen.finalize_labels();

            // Reset function return type (no active function)
            current_function_return_type = Type(TYPE_ERROR);
            // Clear current function context
            set_current_function(nullptr);
            // Clear header snapshot
            pending_function_header = false;
            pending_function_name.clear();
        }
    | declaration_specifiers declarator
        {
            current_function_return_type = backup_current_type;
            current_function_return_type.pointer_level = current_pointer_level;
            
            if(debug) printf("\n[DEBUG] Function return type: %s (contaminated current_type: %s)\n", 
                current_function_return_type.to_string().c_str(), current_type.to_string().c_str());
                
            // Reset function declaration flag for next function
            in_function_declaration = false;
            // Register function signature
            FunctionSignature *func_sig = register_function(std::string($2), pending_function_param_types, current_function_return_type);
            pending_function_param_types.clear();
            // Set current function for static variable handling BEFORE parsing compound statement
            set_current_function(func_sig);
            // Emit function entry label with function name
            tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL,mangle_function_for_tac(std::string($2), *func_sig)), TACOperand());
            
            // Reset return flag for new function
            current_function_has_return = false;
        }
        compound_statement
        {
            if ($4) {
                if(debug) printf("\n[Function] Generating TAC for function body\n");
                $4->generate_tac();
                // Backpatch any remaining nextlist to end of function
                backpatch($4->nextlist, tacGen.nextinstr());
            }
            
            // Check for missing return statements
            if (!current_function_has_return) {
                if (current_function_return_type.base_type == TYPE_VOID) {
                    // Emit implicit return for void functions
                    tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
                    if(debug) printf("[Function] Added implicit return for void function '%s'\n", std::string($2).c_str());
                } else {
                    // Error for non-void functions without return
                    SEM_ERROR(yylloc.first_line, "Function '%s' with non-void return type must have a return statement",
                              std::string($2).c_str());
                    semantic_error_count++;
                }
            }
            // Always add a final return TAC as a safety net
            tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
            
            tacGen.finalize_labels();

            // Reset function return type (no active function)
            current_function_return_type = Type(TYPE_ERROR);
            // Clear current function context
            set_current_function(nullptr);
            // Clear header snapshot
            pending_function_header = false;
            pending_function_name.clear();
        }
    | declarator declaration_list
        {
            // Old-style C function - assume int return type
            current_function_return_type = Type(TYPE_INT);
            current_function_return_type.pointer_level = current_pointer_level;
            // Register with assumed int return type
            FunctionSignature *func_sig = register_function(std::string($1), pending_function_param_types, current_function_return_type);
            // Set current function for static variable handling BEFORE parsing compound statement
            set_current_function(func_sig);
            // Emit function entry label with function name
            tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL,mangle_function_for_tac(std::string($1), *func_sig)), TACOperand());
            
            // Reset return flag for new function
            current_function_has_return = false;
        }
        compound_statement
        {
            if ($4) {
                if(debug) printf("\n[Function] Generating TAC for function body\n");
                $4->generate_tac();
                // Backpatch any remaining nextlist to end of function
                backpatch($4->nextlist, tacGen.nextinstr());
            }
            
            // Check for missing return statements
            if (!current_function_has_return) {
                if (current_function_return_type.base_type == TYPE_VOID) {
                    // Emit implicit return for void functions
                    tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
                    if(debug) printf("[Function] Added implicit return for void function '%s'\n", std::string($1).c_str());
                } else {
                    // Error for non-void functions without return
                    SEM_ERROR(yylloc.first_line, "Function '%s' with non-void return type must have a return statement",
                              std::string($1).c_str());
                    semantic_error_count++;
                }
            }
            // Always add a final return TAC as a safety net
            tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());

            tacGen.finalize_labels();
            
            // Reset function return type (no active function)
            current_function_return_type = Type(TYPE_ERROR);
            // Clear current function context
            set_current_function(nullptr);
            // Clear header snapshot (not used here, but reset for safety)
            pending_function_header = false;
            pending_function_name.clear();
        }
    | declarator
        {
            // Old-style C function - assume int return type
            current_function_return_type = Type(TYPE_INT);
            current_function_return_type.pointer_level = current_pointer_level;
            
            FunctionSignature *func_sig = register_function(std::string($1), pending_function_param_types, current_function_return_type);
            // Set current function for static variable handling BEFORE parsing compound statement
            set_current_function(func_sig);
            // Emit function entry label with function name
            tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL,mangle_function_for_tac(std::string($1), *func_sig)), TACOperand());
            
            // Reset return flag for new function
            current_function_has_return = false;
        }
        compound_statement
        {
            if ($3) {
                if(debug) printf("\n[Function] Generating TAC for function body\n");
                $3->generate_tac();
                // Backpatch any remaining nextlist to end of function
                backpatch($3->nextlist, tacGen.nextinstr());
            }
            
            // Check for missing return statements
            if (!current_function_has_return) {
                if (current_function_return_type.base_type == TYPE_VOID) {
                    // Emit implicit return for void functions
                    tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
                    if(debug) printf("[Function] Added implicit return for void function '%s'\n", std::string($1).c_str());
                } else {
                    // Error for non-void functions without return
                    SEM_ERROR(yylloc.first_line, "Function '%s' with non-void return type must have a return statement",
                              std::string($1).c_str());
                    semantic_error_count++;
                }
            }
            // Always add a final return TAC as a safety net
            tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
            
            tacGen.finalize_labels();

            // Reset function return type (no active function)
            current_function_return_type = Type(TYPE_ERROR);
            // Clear current function context
            set_current_function(nullptr);
            // Clear header snapshot (not used here, but reset for safety)
            pending_function_header = false;
            pending_function_name.clear();
        }
    ;

%%

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void yyerror(const char *s) {
    PARSE_ERROR(yylineno, "%s", s);
}

int main(int argc, char *argv[]) {

    // Initialize global symbol table for global variables
    init_global_symbol_table();

    // Don't push any scope initially - global variables go into globalSymbolTable
    // Local scopes will be pushed when entering functions/compound statements

    // Register built-in I/O functions in global scope
    register_builtin_io_functions();

    if(debug) {
        printf("\n========================================\n");
        printf("       HashPP - Just a Compiler           \n");
        printf("========================================\n\n");
    }
    
    if (argc > 1) {
        FILE *file = fopen(argv[1], "r");
        if (!file) {
            report_diagnostic(DiagnosticLevel::Error, DiagnosticStage::Parse, -1, "Cannot open file %s", argv[1]);
            return 1;
        }
        extern FILE *yyin;
        yyin = file;
    }
    
    int parse_ok = (yyparse() == 0);

    if (parse_ok && semantic_error_count == 0) {
        printf("\nâœ“ Parsing successful!\n");
        // Print global symbol table for debugging
        if (symbol_debug) {
            if (SymbolTable* gst = get_global_symbol_table()) {
                printf("\n[Global Symbol Table]\n");
                gst->print();
            }
        }

        if(function_debug) print_function_signatures();
        
        // Print method signatures for debugging
        if(method_debug) print_method_signatures();

        tacGen.print();
        
        return 0;
    } else {
        if (semantic_error_count > 0) {
            report_diagnostic(DiagnosticLevel::Error, DiagnosticStage::Semantic, -1, "Semantic errors: %d", semantic_error_count);
        }
        printf("\nâœ— Parsing failed!\n");
        return 1;
    }
}

// ============================================================================
// *** COMPREHENSIVE GUIDE: AST and TAC Integration ***
// ============================================================================
//
// This file has been annotated to show where AST/TAC code should be added.
// Look for comments marked with "*** TO USE AST: ***" throughout the file.
//
// QUICK START:
// ============
// 1. All necessary headers are already included at the top
// 2. %union has Expression* and Declaration* types added
// 3. Grammar rules have commented examples showing AST node creation
// 4. Just uncomment the marked sections to enable AST/TAC!
//
// WHAT'S AVAILABLE:
// ==================
// - Symbol Table: symbolTable (SymbolTable class)
// - TAC Generator: tacGen (TACGenerator class)
// - Current Type: current_type (Type object)
//
// AST NODE TYPES:
// ===============
// - Expression nodes: PrimaryExpression, BinaryExpression, UnaryExpression
// - Declaration nodes: VariableDeclaration
//
// TAC OPERATORS:
// ==============
// - Arithmetic: TAC_ADD, TAC_SUB, TAC_MUL, TAC_DIV, TAC_MOD
// - Unary: TAC_UMINUS, TAC_UPLUS
// - Assignment: TAC_ASSIGN
//
// See grammar rules above for detailed examples at each integration point.
// ============================================================================
