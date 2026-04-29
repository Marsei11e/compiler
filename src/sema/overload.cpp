
#include "sema/overload.h"
#include "lexer/token.h"

#include <cassert>

namespace mycc::sema {

using namespace ast;

ArgKind arg_kind_of(const ast::Expr* expr) {
    if (expr->kind == NodeKind::IntLit) {
        const auto* il = ast_cast<const IntLit>(expr);
        if (il->data.suffix == lex::IntSuffix::None)
            return ArgKind::UnsuffixedInt;
    }
    if (expr->kind == NodeKind::FloatLit) {
        const auto* fl = ast_cast<const FloatLit>(expr);
        if (!fl->data.is_f32)
            return ArgKind::UnsuffixedFloat;
    }
    return ArgKind::Regular;
}

OverloadSet filter_by_name(const OverloadSet& all, const std::string& name) {
    OverloadSet result;
    for (auto* fn : all.overloads)
        if (fn->name == name)
            result.overloads.push_back(fn);
    return result;
}




static std::pair<bool, bool> check_arg_compat(TypeId natural_ty, ArgKind kind,
                                               TypeId param_ty,
                                               const TypeInterner& ti) {
    if (kind == ArgKind::Regular) {
        bool exact = (natural_ty == param_ty);
        return {exact, exact};
    }
    
    if (kind == ArgKind::UnsuffixedInt) {
        bool adapt = ti.is_signed_int(param_ty) || ti.is_unsigned_int(param_ty);
        return {false, adapt};
    }
    
    bool adapt = ti.is_float(param_ty);
    return {false, adapt};
}

ResolveResult resolve_call(const OverloadSet&       candidates,
                            std::span<const TypeId>  arg_types,
                            std::span<const ArgKind> arg_kinds,
                            const TypeInterner&      ti) {
    assert(arg_types.size() == arg_kinds.size());
    const size_t nargs = arg_types.size();

    
    for (TypeId t : arg_types)
        if (t == kInvalidTypeId)
            return {nullptr, OverloadStatus::NoMatch};

    struct Candidate { FnSymbol* fn; bool all_exact; };
    std::vector<Candidate> applicable;

    for (auto* fn : candidates.overloads) {
        if (fn->params.size() != nargs) continue;

        bool ok        = true;
        bool all_exact = true;
        for (size_t i = 0; i < nargs; ++i) {
            auto [exact, fits] = check_arg_compat(arg_types[i], arg_kinds[i],
                                                   fn->params[i].type, ti);
            if (!fits) { ok = false; break; }
            if (!exact) all_exact = false;
        }
        if (ok) applicable.push_back({fn, all_exact});
    }

    if (applicable.empty())
        return {nullptr, OverloadStatus::NoMatch};

    if (applicable.size() == 1)
        return {applicable[0].fn, OverloadStatus::Resolved};

    
    FnSymbol* winner     = nullptr;
    int       exact_count = 0;
    for (auto& c : applicable) {
        if (c.all_exact) { ++exact_count; winner = c.fn; }
    }

    if (exact_count == 1)
        return {winner, OverloadStatus::Resolved};

    return {nullptr, OverloadStatus::Ambiguous};
}

} 
