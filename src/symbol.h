// symbol.h
#pragma once
#include <string>
#include <memory>
#include <vector>

struct Type;    // forward: semantic type (implement later)
struct AstNode; // forward: AST node (your AST header provides shared_ptr<AstNode>)

enum class SymbolKind {
    Variable,
    Function,
    Parameter,
    TypedefName,
    StructTag, // struct/union/enum tags
    EnumConstant,
    Label,
};

enum class MemberAccessLevel {
    Public,
    Private,
    Protected
};

enum class StorageClass {
    None,
    Auto_,
    Extern,
    Static,
    Register,
    Typedef_
};

struct Symbol {
    std::string name;
    SymbolKind kind = SymbolKind::Variable;
    std::shared_ptr<Type> type; // filled during semantic analysis
    StorageClass storage = StorageClass::None;
    int line_declared = 0;
    int scope_level = 0; // the scope level where declared (0 = global)
    bool is_defined = false;     // for functions, structs
    bool is_used = false;        // set when used
    bool is_initialized = false; // for variables with initializer
    bool is_parameter = false;   // convenience
    void *user_data = nullptr;   // for any backend or front-end extra info (offset, etc.)
    MemberAccessLevel access_level = MemberAccessLevel::Public; // Access level for class members

    // link back to AST declaration (weak to avoid cycles)
    std::weak_ptr<AstNode> decl_node;

    // Function overloading support
    std::vector<std::string> parameter_types; // For function overloading
    std::string function_signature; // Generated signature for overloading

    Symbol() = default;
    Symbol(std::string n, SymbolKind k, int line = 0)
        : name(std::move(n)), kind(k), line_declared(line) {}
};

using SymbolPtr = std::shared_ptr<Symbol>;
