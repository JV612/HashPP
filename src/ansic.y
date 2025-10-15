%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbol_table.h"
#include "tac.h"
#include "ast.h"

int yylex(void);
void yyerror(const char *s);
extern int yylineno;

// Current type being declared
Type current_type;

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

%type <type_ptr> type_specifier

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
	| postfix_expression '(' ')'
	| postfix_expression '(' argument_expression_list ')'
	| postfix_expression '.' IDENTIFIER
	| postfix_expression PTR_OP IDENTIFIER
	| postfix_expression INC_OP
	| postfix_expression DEC_OP
	;

argument_expression_list
	: assignment_expression
	| argument_expression_list ',' assignment_expression
	;

unary_expression
    : postfix_expression
        { $$ = $1; }
| INC_OP unary_expression
        { $$ = create_unary_expression(TAC_PRE_INC, $2); }
| DEC_OP unary_expression
        { $$ = create_unary_expression(TAC_PRE_DEC, $2); }
| '&' cast_expression
| '*' cast_expression
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
            // For now, fallback - this shouldn't happen in simple cases
            fprintf(stderr, "Warning: Complex assignment not fully supported yet\n");
            $$ = $3;
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
	| declaration_specifiers init_declarator_list ';'
        {
            printf("\n--- Declaration complete ---\n");
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
	| init_declarator_list ',' init_declarator
	;

init_declarator
	: declarator {
          if ($1) {
              printf("[Parser] Variable: %s\n", $1);
              VariableDeclaration* decl = create_variable_declaration(
                  new Type(current_type),
                  $1,
                  nullptr
              );
              decl->generate_tac();
          }
      }
	| declarator '=' initializer {
          if ($1) {
              printf("[Parser] Variable with initializer: %s\n", $1);
              VariableDeclaration* decl = create_variable_declaration(
                  new Type(current_type),
                  $1,
                  $3
              );
              decl->generate_tac();
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
          printf("[Parser] Type: void\n");
      }
    | CHAR { 
          current_type = Type(TYPE_CHAR);
          $$ = new Type(TYPE_CHAR);
          printf("[Parser] Type: char\n");
      }
    | INT { 
          current_type = Type(TYPE_INT);
          $$ = new Type(TYPE_INT);
          printf("[Parser] Type: int\n");
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
    | enum_specifier
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
	: ENUM '{' enumerator_list '}'
	| ENUM IDENTIFIER '{' enumerator_list '}'
	| ENUM IDENTIFIER
	;

enumerator_list
	: enumerator
	| enumerator_list ',' enumerator
	;

enumerator
	: IDENTIFIER
	| IDENTIFIER '=' constant_expression
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
	: pointer direct_declarator        { $$ = $2; }
	| direct_declarator               { $$ = $1; }
	| '&' direct_declarator           { $$ = $2; }
	;

direct_declarator
    : IDENTIFIER                      { $$ = $1; }
    | '(' declarator ')'              { $$ = $2; }
    | direct_declarator '[' constant_expression ']' 
    | direct_declarator '[' ']' 
    | direct_declarator '(' parameter_type_list ')' {
          $$ = $1;
      }
    | direct_declarator '(' identifier_list ')' {
          $$ = $1;
      }
    | direct_declarator '(' ')' {
          $$ = $1;
      }
    ;


pointer
	: '*'
	| '*' type_qualifier_list
	| '*' pointer
	| '*' type_qualifier_list pointer
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
          $$ = $2;
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
        { $$ = nullptr; } /* Declarations don't return statements */
	;

labeled_statement
    : IDENTIFIER ':' statement
        { $$ = $3; /* For now, ignore labels */ }
    | CASE constant_expression ':' statement
        { $$ = $4; }
    | DEFAULT ':' statement
        { $$ = $3; }
    ;


compound_statement
    : '{' '}'
        { $$ = create_compound_statement(); }
    | '{' statement_list '}'
        { $$ = $2; }
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
        { $$ = nullptr; /* Not implemented yet */ }
    ;

iteration_statement
	: WHILE '(' expression ')' statement
        { $$ = create_while_statement($3, $5); }
	| UNTIL '(' expression ')' statement
        { $$ = nullptr; /* Not implemented */ }
	| DO statement WHILE '(' expression ')' ';'
        { $$ = nullptr; /* Not implemented yet */ }
	| FOR '(' expression_statement expression_statement ')' statement
        { $$ = nullptr; /* Not implemented yet */ }
	| FOR '(' expression_statement expression_statement expression ')' statement
        { $$ = nullptr; /* Not implemented yet */ }
	| FOR '(' declaration expression_statement ')' statement
        { $$ = nullptr; /* Not implemented yet */ }
	| FOR '(' declaration expression_statement expression ')' statement
        { $$ = nullptr; /* Not implemented yet */ }
	;

jump_statement
    : GOTO IDENTIFIER ';'
        { $$ = nullptr; /* Not implemented */ }
    | CONTINUE ';'
        { $$ = nullptr; /* Not implemented */ }
    | BREAK ';'
        { $$ = nullptr; /* Not implemented */ }
    | RETURN ';'
        { $$ = nullptr; /* Not implemented */ }
    | RETURN expression ';'
        { $$ = nullptr; /* Not implemented */ }
    ;

translation_unit
	: external_declaration
	| translation_unit external_declaration
	;

external_declaration
	: function_definition 
	| declaration
	;

function_definition
	: declaration_specifiers declarator declaration_list compound_statement
        {
            if ($4) {
                printf("\n[Function] Generating TAC for function body\n");
                $4->generate_tac();
            }
        }
	| declaration_specifiers declarator compound_statement
        {
            if ($3) {
                printf("\n[Function] Generating TAC for function body\n");
                $3->generate_tac();
                // Backpatch any remaining nextlist to end of function
                backpatch($3->nextlist, tacGen.nextinstr());
            }
        }
	| declarator declaration_list compound_statement
        {
            if ($3) {
                printf("\n[Function] Generating TAC for function body\n");
                $3->generate_tac();
                // Backpatch any remaining nextlist to end of function
                backpatch($3->nextlist, tacGen.nextinstr());
            }
        }
	| declarator compound_statement
        {
            if ($2) {
                printf("\n[Function] Generating TAC for function body\n");
                $2->generate_tac();
                // Backpatch any remaining nextlist to end of function
                backpatch($2->nextlist, tacGen.nextinstr());
            }
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
    printf("\n========================================\n");
    printf("   Phase 1 Compiler - Basic Declarations\n");
    printf("========================================\n\n");
    
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

    if (parse_ok) {
        printf("\n✓ Parsing successful!\n");
        
        symbolTable.print();
        
        tacGen.print();
        
        return 0;
    } else {
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