%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbol_table.h"
#include "tac.h"
#include "expression.h"
#include "statement.h"
#include "declaration.h"

int yylex(void);
void yyerror(const char *s);
extern int yylineno;
extern int semantic_error_count;

// Current type being declared
Type current_type;

// Current function return type (value). TYPE_ERROR sentinel means "no active function".
Type current_function_return_type;

// Track pointer levels and array dimensions for current declarator
int current_pointer_level = 0;
bool current_is_array = false;
std::vector<int> current_array_sizes;

// Track current enum being parsed
EnumType* current_enum = nullptr;

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
%type <expr> constant_expression
%type <expr_list> argument_expression_list

%type <type_ptr> type_specifier

%type <decl> declaration
%type <decl> init_declarator
%type <decl> init_declarator_list

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
    | BOOL_FALSE
    | NULL_CONSTANT
    | NULLPTR_CONSTANT
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
	| postfix_expression '.' IDENTIFIER
	| postfix_expression PTR_OP IDENTIFIER
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
        }
    | IDENTIFIER SUB_ASSIGN assignment_expression
        {
            Expression* lhs = create_primary_expression($1);
            Expression* rhs = create_binary_expression(lhs, TAC_SUB, $3);
            $$ = create_assignment_expression($1, rhs);
        }
    | IDENTIFIER MUL_ASSIGN assignment_expression
        {
            Expression* lhs = create_primary_expression($1);
            Expression* rhs = create_binary_expression(lhs, TAC_MUL, $3);
            $$ = create_assignment_expression($1, rhs);
        }
    | IDENTIFIER DIV_ASSIGN assignment_expression
        {
            Expression* lhs = create_primary_expression($1);
            Expression* rhs = create_binary_expression(lhs, TAC_DIV, $3);
            $$ = create_assignment_expression($1, rhs);
        }
    | IDENTIFIER MOD_ASSIGN assignment_expression
        {
            Expression* lhs = create_primary_expression($1);
            Expression* rhs = create_binary_expression(lhs, TAC_MOD, $3);
            $$ = create_assignment_expression($1, rhs);
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
	: declaration_specifiers ';'
        { $$ = nullptr; }
	| declaration_specifiers init_declarator_list ';'
        {
            if(debug) printf("\n--- Declaration complete ---\n");
            $$ = $2; /* Return the last declarator from the list (for use in for-loops) */
        }
	;

declaration_specifiers
	: storage_class_specifier
	| storage_class_specifier declaration_specifiers
	| type_specifier
	| type_specifier declaration_specifiers
	| type_qualifier
	| type_qualifier declaration_specifiers
	;

init_declarator_list
	: init_declarator
        { $$ = $1; }  /* Return the first (and possibly only) declarator */
	| init_declarator_list ',' init_declarator
        { 
            /* For multiple declarators, insert previous one into symbol table */
            /* and save it for deferred TAC generation */
            if ($1) {
                $1->insert_symbol();
                pending_declarator_tac.push_back($1);  // Defer TAC generation
            }
            $$ = $3;  /* Return the latest declarator */
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
                      fprintf(stderr, "[Error] Line %d: Array size not specified for '%s'\n", yylineno, $1);
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
              $$ = decl;
              
              // Reset for next declarator
              current_pointer_level = 0;
              current_is_array = false;
              current_array_sizes.clear();
          } else {
              $$ = nullptr;
          }
      }
	;


storage_class_specifier
	: TYPEDEF
	| STATIC
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
          current_type = Type(TYPE_INT);
          $$ = new Type(TYPE_INT);
      }
    | DOUBLE { 
          current_type = Type(TYPE_FLOAT);
          $$ = new Type(TYPE_FLOAT);
      }
    | SIGNED
    | UNSIGNED
    | struct_or_union_specifier
    | enum_specifier {
          // After parsing enum, set current_type to enum type
          current_type = Type(TYPE_ENUM);
          $$ = new Type(TYPE_ENUM);
      }
    | TYPE_NAME
    | class_specifier
    ;

/* Simplified stubs for advanced features - not used in basic compiler */
class_specifier
    : CLASS IDENTIFIER '{' class_member_declarations '}'
    | CLASS IDENTIFIER ':' inheritance '{' class_member_declarations '}'
    | CLASS IDENTIFIER '{' '}'
    | CLASS IDENTIFIER
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
    ;

access_specifier
    : PUBLIC
    | PRIVATE
    | PROTECTED
    ;

class_member
    : declaration
    | function_definition
    | '~' IDENTIFIER '(' ')' compound_statement
    ;

struct_or_union_specifier
    : struct_or_union IDENTIFIER '{' struct_declaration_list '}'
    | struct_or_union '{' struct_declaration_list '}'
    | struct_or_union IDENTIFIER '{' '}' 
    | struct_or_union '{' '}'
    | struct_or_union IDENTIFIER
    ;

struct_declaration_list
    : struct_declaration
    | struct_declaration_list struct_declaration
    ;

struct_declaration
    : specifier_qualifier_list struct_declarator_list ';'
    ;

struct_declarator_list
    : struct_declarator
    | struct_declarator_list ',' struct_declarator
    ;

struct_declarator
    : declarator
    | ':' constant_expression
    | declarator ':' constant_expression
    ;

struct_or_union
    : STRUCT
    | UNION
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
              fprintf(stderr, "[Error] Line %d: enum '%s' not defined\n", yylineno, $2);
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
                  fprintf(stderr, "[Error] Line %d: Enum value must be an integer constant\n", yylineno);
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
            current_pointer_level = $1;
            $$ = $2; 
        }
	| direct_declarator               
        { 
            current_pointer_level = 0;
            $$ = $1; 
        }
	| '&' direct_declarator           
        { 
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
                fprintf(stderr, "[Error] Line %d: Array dimension has no type information\n", yylineno);
                semantic_error_count++;
                array_size = 1; // Safe default
            } else if ($3->type->base_type == TYPE_FLOAT) {
                fprintf(stderr, "[Error] Line %d: Array dimension must be integer, not float\n", yylineno);
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
                        fprintf(stderr, "[Error] Line %d: Array size must be an integer expression\n", yylineno);
                        semantic_error_count++;
                        array_size = 1; // Safe default
                    }
                }

                // If we have a compile-time size, validate positivity
                if (array_size > 0) {
                    // OK
                } else if (array_size == 0) {
                    fprintf(stderr, "[Error] Line %d: Array size must be positive (got %d)\n", yylineno, array_size);
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
                    // Snapshot function header: name and return type (base + pointer level)
                    pending_function_header = true;
                    pending_function_return_type = current_type;
                    pending_function_return_type.pointer_level = current_pointer_level;
                    if ($1) pending_function_name = std::string($1);
            }

        | direct_declarator '(' ')' {
                    $$ = $1;
                    pending_function_header = true;
                    pending_function_return_type = current_type;
                    pending_function_return_type.pointer_level = current_pointer_level;
                    if ($1) pending_function_name = std::string($1);
            }
    ;


pointer
	: '*'
        { $$ = 1; }
	| '*' type_qualifier_list
        { $$ = 1; }
	| '*' pointer
        { $$ = 1 + $2; }
	| '*' type_qualifier_list pointer
        { $$ = 1 + $3; }
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

              pending_function_params.emplace_back(std::string($2), param_type);
              pending_function_param_types.push_back(param_type);
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
	| '{' initializer_list ',' '}'
	;

initializer_list
	: initializer
	| initializer_list ',' initializer
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
                /* Insert symbol immediately but don't generate TAC yet */
                $1->insert_symbol();
                /* Wrap in DeclarationStatement for deferred TAC generation */
                $$ = create_declaration_statement($1);
            } else {
                $$ = nullptr;
            }
        }
	;

labeled_statement
    : IDENTIFIER ':' statement
        { $$ = $3; /* For now, ignore labels */ }
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
        { $$ = nullptr; /* Not implemented */ }
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
                $1->insert_symbol();
                $1->generate_tac();
                // Optional: clean up the declaration node since it's not wrapped in a statement
                delete $1;
            }
        }
	;
    
function_definition
    : declaration_specifiers declarator declaration_list compound_statement
        {
            // Set current function return type using header snapshot to avoid later contamination
            if (pending_function_header) {
                current_function_return_type = pending_function_return_type;
            } else {
                current_function_return_type = current_type;
                current_function_return_type.pointer_level = current_pointer_level;
            }
            // Register function signature with captured pending_function_params
                FunctionSignature *func_sig = register_function(std::string($2), pending_function_param_types, current_function_return_type);
                pending_function_param_types.clear();
            // Emit function entry label with function name
                tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL,mangle_function_for_tac(std::string($2), *func_sig)), TACOperand());
            
            // Reset return flag for new function
            current_function_has_return = false;
            
            if ($4) {
                if(debug) printf("\n[Function] Generating TAC for function body\n");
                $4->generate_tac();
            }
            
            // Check for missing return statements
            if (!current_function_has_return) {
                if (current_function_return_type.base_type == TYPE_VOID) {
                    // Emit implicit return for void functions
                    tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
                    if(debug) printf("[Function] Added implicit return for void function '%s'\n", std::string($2).c_str());
                } else {
                    // Error for non-void functions without return
                    fprintf(stderr, "[Semantic Error] Line %d: Function '%s' with non-void return type must have a return statement\n", 
                            yylloc.first_line, std::string($2).c_str());
                    semantic_error_count++;
                }
            }
            
            // Reset function return type (no active function)
            current_function_return_type = Type(TYPE_ERROR);
            // Clear header snapshot
            pending_function_header = false;
            pending_function_name.clear();
        }
    | declaration_specifiers declarator compound_statement
        {
            // Set current function return type using header snapshot to avoid later contamination
            if (pending_function_header) {
                current_function_return_type = pending_function_return_type;
            } else {
                current_function_return_type = current_type;
                current_function_return_type.pointer_level = current_pointer_level;
            }
            // Register function signature
                FunctionSignature *func_sig = register_function(std::string($2), pending_function_param_types, current_function_return_type);
                pending_function_param_types.clear();
            // Emit function entry label with function name
            tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL,mangle_function_for_tac(std::string($2), *func_sig)), TACOperand());
            
            // Reset return flag for new function
            current_function_has_return = false;
            
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
                    if(debug) printf("[Function] Added implicit return for void function '%s'\n", std::string($2).c_str());
                } else {
                    // Error for non-void functions without return
                    fprintf(stderr, "[Semantic Error] Line %d: Function '%s' with non-void return type must have a return statement\n", 
                            yylloc.first_line, std::string($2).c_str());
                    semantic_error_count++;
                }
            }
            
            // Reset function return type (no active function)
            current_function_return_type = Type(TYPE_ERROR);
            // Clear header snapshot
            pending_function_header = false;
            pending_function_name.clear();
        }
    | declarator declaration_list compound_statement
        {
            // Old-style C function - assume int return type
            current_function_return_type = Type(TYPE_INT);
            current_function_return_type.pointer_level = current_pointer_level;
            // Register with assumed int return type
            FunctionSignature *func_sig = register_function(std::string($1), pending_function_param_types, current_function_return_type);
            // Emit function entry label with function name
            tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL,mangle_function_for_tac(std::string($1), *func_sig)), TACOperand());
            
            // Reset return flag for new function
            current_function_has_return = false;
            
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
                    fprintf(stderr, "[Semantic Error] Line %d: Function '%s' with non-void return type must have a return statement\n", 
                            yylloc.first_line, std::string($1).c_str());
                    semantic_error_count++;
                }
            }
            
            // Reset function return type (no active function)
            current_function_return_type = Type(TYPE_ERROR);
            // Clear header snapshot (not used here, but reset for safety)
            pending_function_header = false;
            pending_function_name.clear();
        }
    | declarator compound_statement
        {
            // Old-style C function - assume int return type
            current_function_return_type = Type(TYPE_INT);
            current_function_return_type.pointer_level = current_pointer_level;
            
            FunctionSignature *func_sig = register_function(std::string($1), pending_function_param_types, current_function_return_type);
            // Emit function entry label with function name
            tacGen.emit(TAC_LABEL, TACOperand(TACOperand::OPERAND_LABEL,mangle_function_for_tac(std::string($1), *func_sig)), TACOperand());
            
            // Reset return flag for new function
            current_function_has_return = false;
            
            if ($2) {
                if(debug) printf("\n[Function] Generating TAC for function body\n");
                $2->generate_tac();
                // Backpatch any remaining nextlist to end of function
                backpatch($2->nextlist, tacGen.nextinstr());
            }
            
            // Check for missing return statements
            if (!current_function_has_return) {
                if (current_function_return_type.base_type == TYPE_VOID) {
                    // Emit implicit return for void functions
                    tacGen.emit(TAC_RETURN, TACOperand(), TACOperand());
                    if(debug) printf("[Function] Added implicit return for void function '%s'\n", std::string($1).c_str());
                } else {
                    // Error for non-void functions without return
                    fprintf(stderr, "[Semantic Error] Line %d: Function '%s' with non-void return type must have a return statement\n", 
                            yylloc.first_line, std::string($1).c_str());
                    semantic_error_count++;
                }
            }
            
            // Reset function return type (no active function)
            current_function_return_type = Type(TYPE_ERROR);
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
    fprintf(stderr, "Error at line %d: %s\n", yylineno, s);
}

int main(int argc, char *argv[]) {

    // Create Symbol table for Global scope on the stack and push as current scope
    push_scope("Global");

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
            fprintf(stderr, "Error: Cannot open file %s\n", argv[1]);
            return 1;
        }
        extern FILE *yyin;
        yyin = file;
    }
    
    int parse_ok = (yyparse() == 0);

    if (parse_ok && semantic_error_count == 0) {
        printf("\n✓ Parsing successful!\n");
        // Always print global symbol table before TAC generation
        if (SymbolTable* gst = global_scope()) {
            if(debug) printf("\n[Global Symbol Table]\n");
            gst->print();
        }

        print_function_signatures();

        tacGen.print();
        
        return 0;
    } else {
        if (semantic_error_count > 0) {
            fprintf(stderr, "\n✗ Semantic errors: %d\n", semantic_error_count);
        }
        printf("\n✗ Parsing failed!\n");
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
