/* реализация разрешения перегрузки - семантика §13.2 */
module;

#include <algorithm>
#include <cassert>
#include <string>
#include <span>
#include <utility>
#include <vector>

module mycc.sema;

import mycc.diag;
import mycc.lexer;
import mycc.parser;

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

// допустимо ли неявное расширяющее (value-preserving) приведение from -> to.
// узких/лоссовых приведений нет - только в одну сторону, чтобы не плодить
// двусторонние неоднозначности (A.3.1).
static bool is_widening(TypeId from, TypeId to, const TypeInterner& ti) {
    if (from == to) return false;
    bool fs = ti.is_signed_int(from),   fu = ti.is_unsigned_int(from), ff = ti.is_float(from);
    bool ts = ti.is_signed_int(to),     tu = ti.is_unsigned_int(to),   tf = ti.is_float(to);
    if ((fs || fu) && tf) return true;                                  // целое -> вещественное
    if (ff && tf) return ti.bit_width(to) > ti.bit_width(from);         // float32 -> float64
    if (fs && ts) return ti.bit_width(to) > ti.bit_width(from);         // signed -> wider signed
    if (fu && tu) return ti.bit_width(to) > ti.bit_width(from);         // unsigned -> wider unsigned
    if (fu && ts) return ti.bit_width(to) > ti.bit_width(from);         // unsigned -> wider signed
    return false;
}

// ранг совместимости одного аргумента с параметром: меньше = лучше, -1 = нет.
//   0 — точное совпадение
//   1 — подстройка безсуффиксного литерала (§13.2)
//   2 — неявное расширяющее приведение (A.3.1)
static int arg_rank(TypeId natural_ty, ArgKind kind, TypeId param_ty,
                    const TypeInterner& ti) {
    // безсуффиксный литерал — всегда подстройка, точным совпадением не считается
    if (kind == ArgKind::UnsuffixedInt)
        return (ti.is_signed_int(param_ty) || ti.is_unsigned_int(param_ty)) ? 1 : -1;
    if (kind == ArgKind::UnsuffixedFloat)
        return ti.is_float(param_ty) ? 1 : -1;
    // обычный аргумент: точное совпадение либо неявное расширение
    if (natural_ty == param_ty) return 0;
    if (is_widening(natural_ty, param_ty, ti)) return 2;
    return -1;
}

ResolveResult resolve_call(const OverloadSet&       candidates,
                            std::span<const TypeId>  arg_types,
                            std::span<const ArgKind> arg_kinds,
                            const TypeInterner&      ti) {
    assert(arg_types.size() == arg_kinds.size());
    const size_t nargs = arg_types.size();

    // любой невалидный арг означает ошибку выше, тихо выходим
    for (TypeId t : arg_types)
        if (t == kInvalidTypeId)
            return {nullptr, OverloadStatus::NoMatch};

    // суммарная "цена" приведений кандидата: чем меньше, тем точнее вызов.
    struct Candidate { FnSymbol* fn; int cost; };
    std::vector<Candidate> applicable;

    for (auto* fn : candidates.overloads) {
        if (fn->params.size() != nargs) continue;

        bool ok   = true;
        int  cost = 0;
        for (size_t i = 0; i < nargs; ++i) {
            int r = arg_rank(arg_types[i], arg_kinds[i], fn->params[i].type, ti);
            if (r < 0) { ok = false; break; }
            cost += r;
        }
        if (ok) applicable.push_back({fn, cost});
    }

    if (applicable.empty())
        return {nullptr, OverloadStatus::NoMatch};

    // выбираем кандидата с минимальной ценой; если их несколько — ambiguous.
    // exact(0) < подстройка литерала(1) < неявное расширение(2) (§13.2 шаг 7, A.3.1)
    int best = applicable[0].cost;
    for (auto& c : applicable) best = std::min(best, c.cost);

    FnSymbol* winner    = nullptr;
    int       win_count = 0;
    for (auto& c : applicable)
        if (c.cost == best) { ++win_count; winner = c.fn; }

    if (win_count == 1)
        return {winner, OverloadStatus::Resolved};

    return {nullptr, OverloadStatus::Ambiguous};
}

} // namespace mycc::sema
