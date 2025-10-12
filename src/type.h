#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct TypeQualifierSet {
    bool is_const = false;
    bool is_volatile = false;
    bool is_restrict = false;
    bool is_atomic = false;

    bool operator==(const TypeQualifierSet &other) const {
        return is_const == other.is_const &&
               is_volatile == other.is_volatile &&
               is_restrict == other.is_restrict &&
               is_atomic == other.is_atomic;
    }
    bool operator!=(const TypeQualifierSet &other) const { return !(*this == other); }
};

enum class TypeCategory {
    Builtin,
    Pointer,
    Array,
    Function,
    Struct,
    Union,
    Enum,
    Typedef,
    Incomplete
};

enum class BuiltinTypeKind {
    Void,
    Bool,
    Char,
    SignedChar,
    UnsignedChar,
    Short,
    UnsignedShort,
    Int,
    UnsignedInt,
    Long,
    UnsignedLong,
    LongLong,
    UnsignedLongLong,
    Float,
    Double,
    LongDouble
};

struct PointerTypeInfo {
    TypePtr pointee;
};

struct ArrayTypeInfo {
    TypePtr element;
    std::optional<size_t> size; // nullopt -> unsized (e.g., int a[])
};

struct FunctionTypeInfo {
    TypePtr return_type;
    std::vector<TypePtr> param_types;
    bool is_variadic = false;
};

struct TaggedTypeInfo {
    std::string name;
    bool is_complete = false;
};

struct TypedefTypeInfo {
    std::string name;
    TypePtr target; // may be null until resolved
};

using TypePayload = std::variant<
    std::monostate,
    BuiltinTypeKind,
    PointerTypeInfo,
    ArrayTypeInfo,
    FunctionTypeInfo,
    TaggedTypeInfo,
    TypedefTypeInfo
>;

struct Type {
    TypeCategory category = TypeCategory::Incomplete;
    TypeQualifierSet qualifiers;
    TypePayload payload;

    Type() = default;
    explicit Type(TypeCategory cat) : category(cat) {}

    bool equals(const Type &other, bool ignore_qualifiers = false) const;
    bool is_complete() const;
    bool is_scalar() const;
    bool is_integer() const;
    bool is_floating() const;
    bool is_pointer() const { return category == TypeCategory::Pointer; }
    bool is_function() const { return category == TypeCategory::Function; }
    bool is_array() const { return category == TypeCategory::Array; }

    TypePtr unqualified_clone() const;
    std::string to_string() const;
};

TypePtr make_builtin_type(BuiltinTypeKind kind, const TypeQualifierSet &qualifiers = {});
TypePtr make_pointer_type(TypePtr pointee, const TypeQualifierSet &qualifiers = {});
TypePtr make_array_type(TypePtr element, std::optional<size_t> size, const TypeQualifierSet &qualifiers = {});
TypePtr make_function_type(TypePtr return_type, std::vector<TypePtr> params, bool is_variadic = false, const TypeQualifierSet &qualifiers = {});
TypePtr make_struct_type(std::string name, bool is_complete = false, const TypeQualifierSet &qualifiers = {});
TypePtr make_union_type(std::string name, bool is_complete = false, const TypeQualifierSet &qualifiers = {});
TypePtr make_enum_type(std::string name, bool is_complete = false, const TypeQualifierSet &qualifiers = {});
TypePtr make_typedef_type(std::string name, TypePtr target, const TypeQualifierSet &qualifiers = {});
TypePtr make_incomplete_type(std::string name = {});

bool type_equals(const TypePtr &a, const TypePtr &b, bool ignore_qualifiers = false);
std::string type_to_string(const TypePtr &type);

BuiltinTypeKind builtin_from_specifiers(bool is_signed, bool is_unsigned, int long_count, bool is_short, const std::string &base);
