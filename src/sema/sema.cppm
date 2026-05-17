module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

export module mycc.sema;

import mycc.diag;
import mycc.lexer;
import mycc.parser;

export namespace mycc::sema {

// типы

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

} // export namespace mycc::sema

namespace mycc::sema::detail {

struct TypeData {
    TypeKind         kind;
    TypeId           elem{kInvalidTypeId};
    std::size_t      array_size{0};
    ast::StructDecl* struct_decl{nullptr};
    std::string      display;
};

} // namespace mycc::sema::detail

export namespace mycc::sema {

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

    TypeId intern_array(TypeId elem, std::size_t n) {
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

// символы

class Scope;

struct VarSymbol {
    std::string name;
    TypeId      type{};
    bool        is_const{false};
    bool        is_initialized{false};
    ast::Decl*  decl{nullptr};
};

struct FnSymbol {
    struct Param {
        TypeId      type{};
        std::string name;
    };
    std::string                  mangled_name;
    std::string                  name;
    std::vector<Param>           params;
    TypeId                       return_ty{};
    std::vector<ast::EffectKind> effects;
    ast::FnDecl*                 decl{nullptr};
};

struct OverloadSet {
    std::vector<FnSymbol*> overloads;
};

struct StructSymbol {
    struct Field {
        std::string name;
        TypeId      type{};
    };
    std::string        name;
    TypeId             type_id{kInvalidTypeId};
    std::vector<Field> fields;
    OverloadSet        methods;
    // владеет объектами FnSymbol, на которые ссылается methods.overloads
    std::vector<std::unique_ptr<FnSymbol>> method_storage;
};

struct NamespaceSymbol {
    std::string name;
    Scope*      scope{nullptr};
};

using ScopeEntry = std::variant<VarSymbol, OverloadSet, StructSymbol, NamespaceSymbol>;

class Scope {
public:
    explicit Scope(Scope* parent = nullptr) : parent_(parent) {}

    Scope* parent() const { return parent_; }

    bool declare(const std::string& name, VarSymbol sym) {
        if (entries_.count(name)) return false;
        entries_.emplace(name, std::move(sym));
        return true;
    }

    // сливает в существующий OverloadSet, возвращает false если имя занято не функцией
    bool declare(const std::string& name, FnSymbol sym) {
        auto it = entries_.find(name);
        if (it == entries_.end()) {
            fn_storage_.push_back(std::make_unique<FnSymbol>(std::move(sym)));
            OverloadSet os;
            os.overloads.push_back(fn_storage_.back().get());
            entries_.emplace(name, std::move(os));
            return true;
        }
        auto* os = std::get_if<OverloadSet>(&it->second);
        if (!os) return false;
        fn_storage_.push_back(std::make_unique<FnSymbol>(std::move(sym)));
        os->overloads.push_back(fn_storage_.back().get());
        return true;
    }

    bool declare(const std::string& name, StructSymbol sym) {
        if (entries_.count(name)) return false;
        entries_.emplace(name, std::move(sym));
        return true;
    }

    bool declare(const std::string& name, NamespaceSymbol sym) {
        if (entries_.count(name)) return false;
        entries_.emplace(name, std::move(sym));
        return true;
    }

    // обходит цепочку родителей, возвращает nullptr если не найдено
    ScopeEntry* lookup(const std::string& name) {
        auto it = entries_.find(name);
        if (it != entries_.end()) return &it->second;
        if (parent_) return parent_->lookup(name);
        return nullptr;
    }

    const ScopeEntry* lookup(const std::string& name) const {
        auto it = entries_.find(name);
        if (it != entries_.end()) return &it->second;
        if (parent_) return parent_->lookup(name);
        return nullptr;
    }

    ScopeEntry* lookup_local(const std::string& name) {
        auto it = entries_.find(name);
        return it != entries_.end() ? &it->second : nullptr;
    }

private:
    Scope*                                        parent_;
    std::unordered_map<std::string, ScopeEntry>   entries_;
    std::vector<std::unique_ptr<FnSymbol>>         fn_storage_;
};

// разрешение перегрузки -семантика §13.2

enum class ArgKind { Regular, UnsuffixedInt, UnsuffixedFloat };

enum class OverloadStatus { Resolved, NoMatch, Ambiguous };

struct ResolveResult {
    FnSymbol*      fn{nullptr};
    OverloadStatus status{OverloadStatus::NoMatch};
};

ArgKind arg_kind_of(const ast::Expr* expr);
OverloadSet filter_by_name(const OverloadSet& all, const std::string& name);
ResolveResult resolve_call(
    const OverloadSet&       candidates,
    std::span<const TypeId>  arg_types,
    std::span<const ArgKind> arg_kinds,
    const TypeInterner&      ti
);

void desugar_program(ast::Program& prog, diag::DiagnosticEngine& diag);

void check_effects(ast::Program& prog,
                   Scope& global_scope,
                   const std::unordered_map<uint32_t, StructSymbol*>& struct_type_map,
                   const TypeInterner& types,
                   diag::DiagnosticEngine& diag);

void check_moves(ast::Program& prog, const TypeInterner& ti,
                 diag::DiagnosticEngine& diag);

void check_control_flow(ast::Program& prog, diag::DiagnosticEngine& diag);

class Sema {
public:
    Sema(diag::DiagnosticEngine& diag, diag::SourceManager& sm);

    bool analyze_pass1(ast::Program& prog);
    bool analyze_pass2(ast::Program& prog);

    TypeInterner& types()        { return types_; }
    Scope&        global_scope() { return *global_; }

private:
    diag::DiagnosticEngine& diag_;
    diag::SourceManager&    sm_;
    TypeInterner             types_;
    std::unique_ptr<Scope>   global_;
    std::vector<std::unique_ptr<Scope>> ns_scopes_;

    TypeId current_fn_return_ty_{kInvalidTypeId};
    bool   current_in_loop_{false};
    std::unordered_map<uint32_t, StructSymbol*> struct_type_map_;

    void   init_builtins();
    TypeId resolve_type(const ast::TypeNode* node, Scope* scope);
    void   collect_decl(ast::Decl* decl, Scope* scope);
    void   collect_fn(ast::FnDecl* fn, Scope* scope);
    void   collect_struct(ast::StructDecl* s, Scope* scope);
    void   collect_impl(ast::ImplBlock* impl, Scope* scope);
    void   collect_namespace_decl(ast::NamespaceDecl* ns, Scope* scope);
    bool   signatures_match(const FnSymbol& a, const FnSymbol& b);
    void   check_body_redeclarations(ast::Expr* body_expr);

    void   analyze_fns_in_scope(const std::vector<ast::DeclPtr>& decls, Scope* scope);
    void   check_fn_body(ast::FnDecl* fn, Scope* parent);
    TypeId check_expr(ast::Expr* expr, TypeId expected, Scope* scope);
    TypeId check_binary_expr(ast::BinaryExpr* be, TypeId expected, Scope* scope);
    void   check_stmt(ast::Stmt* stmt, Scope* scope);
};

} // export namespace mycc::sema
