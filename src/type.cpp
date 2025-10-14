#include "type.h"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <utility>

static bool qualifiers_equal(const TypeQualifierSet &a, const TypeQualifierSet &b) {
    return a == b;
}

static std::string qualifiers_to_string(const TypeQualifierSet &q) {
    std::string out;
    if (q.is_const) out += "const ";
    if (q.is_volatile) out += "volatile ";
    if (q.is_restrict) out += "restrict ";
    if (q.is_atomic) out += "_Atomic ";
    return out;
}

bool Type::equals(const Type &other, bool ignore_qualifiers) const {
    if (category != other.category) return false;
    if (!ignore_qualifiers && !qualifiers_equal(qualifiers, other.qualifiers)) return false;

    if (payload.index() != other.payload.index()) return false;
    switch (category) {
        case TypeCategory::Builtin:
            return std::get<BuiltinTypeKind>(payload) == std::get<BuiltinTypeKind>(other.payload);
        case TypeCategory::Pointer: {
            const auto &lhs = std::get<PointerTypeInfo>(payload);
            const auto &rhs = std::get<PointerTypeInfo>(other.payload);
            return type_equals(lhs.pointee, rhs.pointee, ignore_qualifiers);
        }
        case TypeCategory::Array: {
            const auto &lhs = std::get<ArrayTypeInfo>(payload);
            const auto &rhs = std::get<ArrayTypeInfo>(other.payload);
            return lhs.size == rhs.size && type_equals(lhs.element, rhs.element, ignore_qualifiers);
        }
        case TypeCategory::Function: {
            const auto &lhs = std::get<FunctionTypeInfo>(payload);
            const auto &rhs = std::get<FunctionTypeInfo>(other.payload);
            if (!type_equals(lhs.return_type, rhs.return_type, ignore_qualifiers)) return false;
            if (lhs.is_variadic != rhs.is_variadic) return false;
            if (lhs.param_types.size() != rhs.param_types.size()) return false;
            for (size_t i = 0; i < lhs.param_types.size(); ++i) {
                if (!type_equals(lhs.param_types[i], rhs.param_types[i], ignore_qualifiers)) return false;
            }
            return true;
        }
        case TypeCategory::Struct:
        case TypeCategory::Union:
        case TypeCategory::Enum: {
            const auto &lhs = std::get<TaggedTypeInfo>(payload);
            const auto &rhs = std::get<TaggedTypeInfo>(other.payload);
            return lhs.name == rhs.name && lhs.is_complete == rhs.is_complete;
        }
        case TypeCategory::Typedef: {
            const auto &lhs = std::get<TypedefTypeInfo>(payload);
            const auto &rhs = std::get<TypedefTypeInfo>(other.payload);
            if (lhs.name != rhs.name) return false;
            if (!lhs.target || !rhs.target) return lhs.target == rhs.target;
            return type_equals(lhs.target, rhs.target, ignore_qualifiers);
        }
        case TypeCategory::Incomplete:
            return true;
    }
    return false;
}

bool Type::is_complete() const {
    switch (category) {
        case TypeCategory::Builtin:
        case TypeCategory::Pointer:
        case TypeCategory::Function:
            return true;
        case TypeCategory::Array: {
            const auto &arr = std::get<ArrayTypeInfo>(payload);
            return arr.size.has_value() && arr.element && arr.element->is_complete();
        }
        case TypeCategory::Struct:
        case TypeCategory::Union:
        case TypeCategory::Enum:
            return std::get<TaggedTypeInfo>(payload).is_complete;
        case TypeCategory::Typedef: {
            const auto &info = std::get<TypedefTypeInfo>(payload);
            return info.target ? info.target->is_complete() : false;
        }
        case TypeCategory::Incomplete:
            return false;
    }
    return false;
}

bool Type::is_scalar() const {
    return category == TypeCategory::Builtin ||
           category == TypeCategory::Pointer ||
           category == TypeCategory::Typedef; // depends on target but we assume typedef to scalar is allowed
}

bool Type::is_integer() const {
    if (category != TypeCategory::Builtin) return false;
    switch (std::get<BuiltinTypeKind>(payload)) {
        case BuiltinTypeKind::Bool:
        case BuiltinTypeKind::Char:
        case BuiltinTypeKind::SignedChar:
        case BuiltinTypeKind::UnsignedChar:
        case BuiltinTypeKind::Short:
        case BuiltinTypeKind::UnsignedShort:
        case BuiltinTypeKind::Int:
        case BuiltinTypeKind::UnsignedInt:
        case BuiltinTypeKind::Long:
        case BuiltinTypeKind::UnsignedLong:
        case BuiltinTypeKind::LongLong:
        case BuiltinTypeKind::UnsignedLongLong:
            return true;
        default:
            return false;
    }
}

bool Type::is_floating() const {
    if (category != TypeCategory::Builtin) return false;
    switch (std::get<BuiltinTypeKind>(payload)) {
        case BuiltinTypeKind::Double:
        case BuiltinTypeKind::LongDouble:
            return true;
        default:
            return false;
    }
}

TypePtr Type::unqualified_clone() const {
    auto copy = std::make_shared<Type>(*this);
    copy->qualifiers = {};
    return copy;
}

std::string Type::to_string() const {
    std::ostringstream oss;
    oss << qualifiers_to_string(qualifiers);
    switch (category) {
        case TypeCategory::Builtin: {
            switch (std::get<BuiltinTypeKind>(payload)) {
                case BuiltinTypeKind::Void: oss << "void"; break;
                case BuiltinTypeKind::Bool: oss << "bool"; break;
                case BuiltinTypeKind::Char: oss << "char"; break;
                case BuiltinTypeKind::SignedChar: oss << "signed char"; break;
                case BuiltinTypeKind::UnsignedChar: oss << "unsigned char"; break;
                case BuiltinTypeKind::Short: oss << "short"; break;
                case BuiltinTypeKind::UnsignedShort: oss << "unsigned short"; break;
                case BuiltinTypeKind::Int: oss << "int"; break;
                case BuiltinTypeKind::UnsignedInt: oss << "unsigned int"; break;
                case BuiltinTypeKind::Long: oss << "long"; break;
                case BuiltinTypeKind::UnsignedLong: oss << "unsigned long"; break;
                case BuiltinTypeKind::LongLong: oss << "long long"; break;
                case BuiltinTypeKind::UnsignedLongLong: oss << "unsigned long long"; break;
                case BuiltinTypeKind::Double: oss << "double"; break;
                case BuiltinTypeKind::LongDouble: oss << "long double"; break;
            }
            break;
        }
        case TypeCategory::Pointer: {
            const auto &ptr = std::get<PointerTypeInfo>(payload);
            oss << (ptr.pointee ? ptr.pointee->to_string() : "void") << "*";
            break;
        }
        case TypeCategory::Array: {
            const auto &arr = std::get<ArrayTypeInfo>(payload);
            oss << (arr.element ? arr.element->to_string() : "void") << "[";
            if (arr.size) oss << *arr.size;
            oss << "]";
            break;
        }
        case TypeCategory::Function: {
            const auto &fn = std::get<FunctionTypeInfo>(payload);
            oss << (fn.return_type ? fn.return_type->to_string() : "void") << " (";
            for (size_t i = 0; i < fn.param_types.size(); ++i) {
                if (i) oss << ", ";
                oss << (fn.param_types[i] ? fn.param_types[i]->to_string() : "void");
            }
            if (fn.is_variadic) {
                if (!fn.param_types.empty()) oss << ", ";
                oss << "...";
            }
            oss << ")";
            break;
        }
        case TypeCategory::Struct:
            oss << "struct " << std::get<TaggedTypeInfo>(payload).name;
            break;
        case TypeCategory::Union:
            oss << "union " << std::get<TaggedTypeInfo>(payload).name;
            break;
        case TypeCategory::Enum:
            oss << "enum " << std::get<TaggedTypeInfo>(payload).name;
            break;
        case TypeCategory::Typedef: {
            const auto &info = std::get<TypedefTypeInfo>(payload);
            if (!info.name.empty()) oss << info.name;
            else if (info.target) oss << info.target->to_string();
            else oss << "typedef";
            break;
        }
        case TypeCategory::Incomplete:
            oss << "<incomplete>";
            break;
    }
    return oss.str();
}

TypePtr make_builtin_type(BuiltinTypeKind kind, const TypeQualifierSet &qualifiers) {
    auto type = std::make_shared<Type>(TypeCategory::Builtin);
    type->payload = kind;
    type->qualifiers = qualifiers;
    return type;
}

TypePtr make_pointer_type(TypePtr pointee, const TypeQualifierSet &qualifiers) {
    auto type = std::make_shared<Type>(TypeCategory::Pointer);
    type->payload = PointerTypeInfo{std::move(pointee)};
    type->qualifiers = qualifiers;
    return type;
}

TypePtr make_array_type(TypePtr element, std::optional<size_t> size, const TypeQualifierSet &qualifiers) {
    auto type = std::make_shared<Type>(TypeCategory::Array);
    type->payload = ArrayTypeInfo{std::move(element), size};
    type->qualifiers = qualifiers;
    return type;
}

TypePtr make_function_type(TypePtr return_type, std::vector<TypePtr> params, bool is_variadic, const TypeQualifierSet &qualifiers) {
    auto type = std::make_shared<Type>(TypeCategory::Function);
    type->payload = FunctionTypeInfo{std::move(return_type), std::move(params), is_variadic};
    type->qualifiers = qualifiers;
    return type;
}

static TypePtr make_tagged_type(TypeCategory category, std::string name, bool is_complete, const TypeQualifierSet &qualifiers) {
    auto type = std::make_shared<Type>(category);
    type->payload = TaggedTypeInfo{std::move(name), is_complete};
    type->qualifiers = qualifiers;
    return type;
}

TypePtr make_struct_type(std::string name, bool is_complete, const TypeQualifierSet &qualifiers) {
    return make_tagged_type(TypeCategory::Struct, std::move(name), is_complete, qualifiers);
}

TypePtr make_union_type(std::string name, bool is_complete, const TypeQualifierSet &qualifiers) {
    return make_tagged_type(TypeCategory::Union, std::move(name), is_complete, qualifiers);
}

TypePtr make_enum_type(std::string name, bool is_complete, const TypeQualifierSet &qualifiers) {
    return make_tagged_type(TypeCategory::Enum, std::move(name), is_complete, qualifiers);
}

TypePtr make_typedef_type(std::string name, TypePtr target, const TypeQualifierSet &qualifiers) {
    auto type = std::make_shared<Type>(TypeCategory::Typedef);
    type->payload = TypedefTypeInfo{std::move(name), std::move(target)};
    type->qualifiers = qualifiers;
    return type;
}

TypePtr make_incomplete_type(std::string name) {
    auto type = std::make_shared<Type>(TypeCategory::Incomplete);
    if (!name.empty()) {
        type->payload = TaggedTypeInfo{std::move(name), false};
    }
    return type;
}

bool type_equals(const TypePtr &a, const TypePtr &b, bool ignore_qualifiers) {
    if (a == b) return true;
    if (!a || !b) return false;
    return a->equals(*b, ignore_qualifiers);
}

std::string type_to_string(const TypePtr &type) {
    if (!type) return "<null-type>";
    return type->to_string();
}

BuiltinTypeKind builtin_from_specifiers(bool is_signed, bool is_unsigned, int long_count, bool is_short, const std::string &base) {
    std::string lower_base;
    lower_base.reserve(base.size());
    for (char c : base) lower_base.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (lower_base.empty() || lower_base == "int") {
        if (is_unsigned) {
            if (long_count >= 2) return BuiltinTypeKind::UnsignedLongLong;
            if (long_count == 1) return BuiltinTypeKind::UnsignedLong;
            if (is_short) return BuiltinTypeKind::UnsignedShort;
            return BuiltinTypeKind::UnsignedInt;
        }
        if (is_signed) {
            if (long_count >= 2) return BuiltinTypeKind::LongLong;
            if (long_count == 1) return BuiltinTypeKind::Long;
            if (is_short) return BuiltinTypeKind::Short;
            return BuiltinTypeKind::Int;
        }
        if (long_count >= 2) return BuiltinTypeKind::LongLong;
        if (long_count == 1) return BuiltinTypeKind::Long;
        if (is_short) return BuiltinTypeKind::Short;
        return BuiltinTypeKind::Int;
    }

    if (lower_base == "char") {
        if (is_unsigned) return BuiltinTypeKind::UnsignedChar;
        if (is_signed) return BuiltinTypeKind::SignedChar;
        return BuiltinTypeKind::Char;
    }

    if (lower_base == "bool") return BuiltinTypeKind::Bool;
    if (lower_base == "void") return BuiltinTypeKind::Void;

    if (lower_base == "double") {
        if (long_count >= 1) return BuiltinTypeKind::LongDouble;
        return BuiltinTypeKind::Double;
    }

    throw std::runtime_error("Unsupported builtin type combination: " + base);
}
