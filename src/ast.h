#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "type.h"

struct Symbol;

// Shared pointer aliases to keep ownership semantics explicit
using AstNodePtr = std::shared_ptr<struct AstNode>;
using SymbolWeakPtr = std::weak_ptr<Symbol>;

struct SourceLocation {
    int line = 0;
    int column = 0;
};

struct SourceRange {
    SourceLocation begin;
    SourceLocation end;
};

enum class AstNodeKind {
    TranslationUnit,
    FunctionDecl,
    VariableDecl,
    ParameterDecl,
    CompoundStmt,
    IfStmt,
    WhileStmt,
    ForStmt,
    BreakStmt,
    ContinueStmt,
    ReturnStmt,
    GotoStmt,
    LabelStmt,
    ExpressionStmt,
    BinaryExpr,
    UnaryExpr,
    CallExpr,
    IdentifierExpr,
    LiteralExpr,
    AssignmentExpr,
    ConditionalExpr,
    CastExpr,
    SubscriptExpr,
    InitializerList,
    TypeSpecifier,
    Declarator,
    StructDecl,
    UnionDecl,
    EnumDecl,
    MemberDecl,
    EnumeratorDecl,
    MemberAccessExpr,
};

enum class LiteralKind {
    Integer,
    Double,
    Character,
    Boolean,
    String,
    Null,
    Nullptr,
};

enum class AccessSpecifier {
    Public,
    Private,
    Protected
};

struct TranslationUnitNodeData {
    std::vector<AstNodePtr> declarations;
};

struct FunctionDeclNodeData {
    std::string name;
    AstNodePtr return_type; // may be null for implicit int during parsing
    std::vector<AstNodePtr> parameters;
    AstNodePtr body;
    bool is_definition = false;
    bool is_variadic = false;
    SymbolWeakPtr symbol;
};

struct VariableDeclNodeData {
    std::string name;
    AstNodePtr type_expr;
    AstNodePtr initializer;
    bool is_typedef = false;
    bool is_static = false;
    bool is_extern = false;
    int pointer_levels = 0; // 0=not pointer, 1=*, 2=**, etc.
    bool is_array = false;
    std::vector<int> array_dimensions; // Multiple dimensions, 0 means unsized
    SymbolWeakPtr symbol;
};

struct ParameterDeclNodeData {
    std::string name;
    AstNodePtr type_expr;
    bool is_variadic = false;
    int pointer_levels = 0; // Track pointer levels like in variables
    bool is_array = false;
    std::vector<int> array_dimensions; // Track array dimensions
    SymbolWeakPtr symbol;
};

struct CompoundStmtNodeData {
    std::vector<AstNodePtr> statements;
};

struct IfStmtNodeData {
    AstNodePtr condition;
    AstNodePtr then_branch;
    AstNodePtr else_branch;
};

struct WhileStmtNodeData {
    AstNodePtr condition;
    AstNodePtr body;
    bool is_do_while = false;
};

struct ForStmtNodeData {
    AstNodePtr init;
    AstNodePtr condition;
    AstNodePtr increment;
    AstNodePtr body;
};

struct ReturnStmtNodeData {
    AstNodePtr expression; // may be null for void return
};

struct BreakStmtNodeData {
    // No additional data needed
};

struct ContinueStmtNodeData {
    // No additional data needed  
};

struct GotoStmtNodeData {
    std::string label;
};

struct LabelStmtNodeData {
    std::string label;
    AstNodePtr statement;
};

struct ExpressionStmtNodeData {
    AstNodePtr expression;
};

struct BinaryExprNodeData {
    std::string op;
    AstNodePtr lhs;
    AstNodePtr rhs;
};

struct UnaryExprNodeData {
    std::string op;
    AstNodePtr operand;
    bool is_prefix = true;
};

struct CallExprNodeData {
    AstNodePtr callee;
    std::vector<AstNodePtr> arguments;
};

struct IdentifierExprNodeData {
    std::string name;
    SymbolWeakPtr symbol; // resolves during semantic analysis
};

struct LiteralExprNodeData {
    LiteralKind literal_kind = LiteralKind::Integer;
    std::string lexeme;
};

struct AssignmentExprNodeData {
    std::string op; // '=', "+=", etc.
    AstNodePtr lhs;
    AstNodePtr rhs;
};

struct ConditionalExprNodeData {
    AstNodePtr condition;
    AstNodePtr then_expr;
    AstNodePtr else_expr;
};

struct CastExprNodeData {
    AstNodePtr target_type;
    AstNodePtr expression;
};

struct SubscriptExprNodeData {
    AstNodePtr array;
    AstNodePtr index;
};

struct InitializerListNodeData {
    std::vector<AstNodePtr> elements;
};

enum class TypeSpecifierKind {
    Builtin,
    Identifier,
    Struct,
    Union,
    Enum,
    Pointer,
    Array,
    Function,
};

struct TypeSpecifierNodeData {
    TypeSpecifierKind kind = TypeSpecifierKind::Builtin;
    std::string name; // for builtin/identifier tags
    std::vector<AstNodePtr> children; // element type, parameters, etc.
    TypeQualifierSet qualifiers;
    bool is_signed = false;
    bool is_unsigned = false;
    bool is_short = false;
    bool is_long = false;
    bool is_long_long = false;
};

enum class DeclaratorKind {
    Identifier,
    Pointer,
    Array,
    Function,
};

struct DeclaratorNodeData {
    DeclaratorKind kind = DeclaratorKind::Identifier;
    std::string name; // present for identifier declarators
    std::vector<TypeQualifierSet> pointer_qualifiers; // for pointer chains
    std::vector<AstNodePtr> parameters; // for function declarators
    AstNodePtr return_type; // for function declarators (pointing back to declaration)
    AstNodePtr element_type; // for arrays/pointers chains
    std::optional<size_t> array_size; // nullopt for unsized arrays
};

struct StructDeclNodeData {
    std::string name; // may be empty for anonymous structs
    std::vector<AstNodePtr> members; // MemberDecl nodes
    bool is_definition = false; // true if has members, false if just forward declaration
    SymbolWeakPtr symbol;
};

struct UnionDeclNodeData {
    std::string name; // may be empty for anonymous unions
    std::vector<AstNodePtr> members; // MemberDecl nodes
    bool is_definition = false;
    SymbolWeakPtr symbol;
};

struct EnumDeclNodeData {
    std::string name; // may be empty for anonymous enums
    std::vector<AstNodePtr> enumerators; // EnumeratorDecl nodes
    bool is_definition = false;
    SymbolWeakPtr symbol;
};

struct MemberDeclNodeData {
    std::string name;
    AstNodePtr type_expr;
    int pointer_levels = 0;
    bool is_array = false;
    std::vector<int> array_dimensions;
    int bit_field_width = -1; // -1 means not a bit field
    AccessSpecifier access = AccessSpecifier::Public; // Default to public for structs, private for classes
    SymbolWeakPtr symbol;
};

struct EnumeratorDeclNodeData {
    std::string name;
    AstNodePtr value_expr; // may be null for auto-increment
    SymbolWeakPtr symbol;
};

struct MemberAccessExprNodeData {
    AstNodePtr object; // the struct/union instance
    std::string member_name;
    bool is_arrow = false; // true for ->, false for .
    SymbolWeakPtr member_symbol; // resolves to the member declaration
};

using AstNodePayload = std::variant<
    std::monostate,
    TranslationUnitNodeData,
    FunctionDeclNodeData,
    VariableDeclNodeData,
    ParameterDeclNodeData,
    CompoundStmtNodeData,
    IfStmtNodeData,
    WhileStmtNodeData,
    ForStmtNodeData,
    ReturnStmtNodeData,
    BreakStmtNodeData,
    ContinueStmtNodeData,
    GotoStmtNodeData,
    LabelStmtNodeData,
    ExpressionStmtNodeData,
    BinaryExprNodeData,
    UnaryExprNodeData,
    CallExprNodeData,
    IdentifierExprNodeData,
    LiteralExprNodeData,
    AssignmentExprNodeData,
    ConditionalExprNodeData,
    CastExprNodeData,
    SubscriptExprNodeData,
    InitializerListNodeData,
    TypeSpecifierNodeData,
    DeclaratorNodeData,
    StructDeclNodeData,
    UnionDeclNodeData,
    EnumDeclNodeData,
    MemberDeclNodeData,
    EnumeratorDeclNodeData,
    MemberAccessExprNodeData
>;

struct AstNode {
    AstNodeKind kind = AstNodeKind::TranslationUnit;
    SourceRange range;
    TypePtr inferred_type; // Filled during semantic/type analysis
    int scope_level = -1;   // set by the parser/semantic passes
    AstNodePayload payload;

    AstNode() = default;
    explicit AstNode(AstNodeKind k) : kind(k) {}

    template <typename T>
    T& as() {
        return std::get<T>(payload);
    }

    template <typename T>
    const T& as() const {
        return std::get<T>(payload);
    }
};

inline AstNodePtr make_node(AstNodeKind kind) {
    return std::make_shared<AstNode>(kind);
}

inline AstNodePtr make_node(AstNodeKind kind, AstNodePayload payload) {
    auto node = std::make_shared<AstNode>(kind);
    node->payload = std::move(payload);
    return node;
}
