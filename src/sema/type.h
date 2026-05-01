#pragma once

#include "parser/ast.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mycc::sema {

enum class TypeKind : uint8_t {
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    Bool, String, Hollow,
    Array, Range, Struct,
};

struct TypeId {
    uint32_t index;
    bool operator==(const TypeId&) const = default;
};

inline constexpr TypeId kInvalidTypeId{UINT32_MAX};

namespace detail {

struct TypeData {
    TypeKind         kind;
    TypeId           elem{kInvalidTypeId};
    size_t           array_size{0};
    ast::StructDecl* struct_decl{nullptr};
    std::string      display;
};

} // namespace detail

class TypeInterner {
public:
    TypeInterner() {
        static constexpr TypeKind kBuiltins[] = {
            TypeKind::I8,  TypeKind::I16,  TypeKind::I32,    TypeKind::I64,
            TypeKind::U8,  TypeKind::U16,  TypeKind::U32,    TypeKind::U64,
            TypeKind::F32, TypeKind::F64,
            TypeKind::Bool, TypeKind::String, TypeKind::Hollow,
        };
        for (auto k : kBuiltins) {
            detail::TypeData d;
            d.kind    = k;
            d.display = builtin_display(k);
            types_.push_back(std::move(d));
        }
        // проверяем контракт enum → индекс
        assert(types_[static_cast<uint32_t>(TypeKind::I32)].kind == TypeKind::I32);
        assert(types_[static_cast<uint32_t>(TypeKind::Hollow)].kind == TypeKind::Hollow);
    }

    TypeId intern_builtin(TypeKind k) {
        assert(k != TypeKind::Array && k != TypeKind::Range && k != TypeKind::Struct);
        return TypeId{static_cast<uint32_t>(k)};
    }

    TypeId intern_array(TypeId elem, size_t n) {
        uint64_t key = (static_cast<uint64_t>(elem.index) << 32) | n;
        auto [it, inserted] = array_cache_.try_emplace(key, kInvalidTypeId);
        if (!inserted) return it->second;

        detail::TypeData d;
        d.kind       = TypeKind::Array;
        d.elem       = elem;
        d.array_size = n;
        d.display    = "array[" + std::string(display_name(elem)) + ", " + std::to_string(n) + "]";
        it->second   = alloc(std::move(d));
        return it->second;
    }

    TypeId intern_range(TypeId elem) {
        auto [it, inserted] = range_cache_.try_emplace(elem.index, kInvalidTypeId);
        if (!inserted) return it->second;

        detail::TypeData d;
        d.kind    = TypeKind::Range;
        d.elem    = elem;
        d.display = "range[" + std::string(display_name(elem)) + "]";
        it->second = alloc(std::move(d));
        return it->second;
    }

    // номинальный: каждый указатель StructDecl получает свой TypeId
    TypeId intern_struct(ast::StructDecl* decl) {
        detail::TypeData d;
        d.kind        = TypeKind::Struct;
        d.struct_decl = decl;
        d.display     = decl->name;
        return alloc(std::move(d));
    }

    const detail::TypeData& get(TypeId id) const {
        assert(id.index < types_.size());
        return types_[id.index];
    }

    bool is_signed_int(TypeId id) const {
        auto k = get(id).kind;
        return k == TypeKind::I8 || k == TypeKind::I16 ||
               k == TypeKind::I32 || k == TypeKind::I64;
    }

    bool is_unsigned_int(TypeId id) const {
        auto k = get(id).kind;
        return k == TypeKind::U8 || k == TypeKind::U16 ||
               k == TypeKind::U32 || k == TypeKind::U64;
    }

    bool is_float(TypeId id) const {
        auto k = get(id).kind;
        return k == TypeKind::F32 || k == TypeKind::F64;
    }

    bool is_numeric(TypeId id) const {
        return is_signed_int(id) || is_unsigned_int(id) || is_float(id);
    }

    uint32_t bit_width(TypeId id) const {
        switch (get(id).kind) {
            case TypeKind::I8:  case TypeKind::U8:  return 8;
            case TypeKind::I16: case TypeKind::U16: return 16;
            case TypeKind::I32: case TypeKind::U32: return 32;
            case TypeKind::I64: case TypeKind::U64: return 64;
            case TypeKind::F32: return 32;
            case TypeKind::F64: return 64;
            case TypeKind::Bool: return 1;
            default: return 0;
        }
    }

    bool is_copyable(TypeId id) const {
        return get(id).kind != TypeKind::Range;
    }

    std::string_view display_name(TypeId id) const {
        assert(id.index < types_.size());
        return types_[id.index].display;
    }

private:
    TypeId alloc(detail::TypeData d) {
        TypeId id{static_cast<uint32_t>(types_.size())};
        types_.push_back(std::move(d));
        return id;
    }

    static std::string builtin_display(TypeKind k) {
        switch (k) {
            case TypeKind::I8:     return "int8";
            case TypeKind::I16:    return "int16";
            case TypeKind::I32:    return "int32";
            case TypeKind::I64:    return "int64";
            case TypeKind::U8:     return "uint8";
            case TypeKind::U16:    return "uint16";
            case TypeKind::U32:    return "uint32";
            case TypeKind::U64:    return "uint64";
            case TypeKind::F32:    return "float32";
            case TypeKind::F64:    return "float64";
            case TypeKind::Bool:   return "bool";
            case TypeKind::String: return "string";
            case TypeKind::Hollow: return "hollow";
            default:               return "?";
        }
    }

    std::vector<detail::TypeData>         types_;
    std::unordered_map<uint64_t, TypeId>  array_cache_;
    std::unordered_map<uint32_t, TypeId>  range_cache_;
};

} // namespace mycc::sema
