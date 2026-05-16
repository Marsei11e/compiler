/* разрешение перегрузки -семантика §13.2
 * реализует алгоритм: точное совпадение / адаптация литерала / неоднозначность */
#pragma once

#include "parser/_pod.h"
#include "sema/symbol.h"
#include "sema/type.h"

#include <span>
#include <string>

namespace mycc::sema {

// классификация аргумента вызова для разрешения перегрузки
// несуффиксированные литералы могут адаптироваться к любому совместимому числовому типу (§13.2)
// литералы никогда не считаются "точными" для tie-breaking между кандидатами
enum class ArgKind { Regular, UnsuffixedInt, UnsuffixedFloat };

enum class OverloadStatus { Resolved, NoMatch, Ambiguous };

struct ResolveResult {
    FnSymbol*      fn{nullptr};
    OverloadStatus status{OverloadStatus::NoMatch};
};

// определяет вид аргумента для узла выражения
ArgKind arg_kind_of(const ast::Expr* expr);

// строит подмножество из all, содержащее только перегрузки с данным именем
OverloadSet filter_by_name(const OverloadSet& all, const std::string& name);

// разрешает перегрузку по §13.2
// arg_types и arg_kinds должны быть одинакового размера
// если любой arg_type == kInvalidTypeId, сразу возвращает NoMatch
ResolveResult resolve_call(
    const OverloadSet&       candidates,
    std::span<const TypeId>  arg_types,
    std::span<const ArgKind> arg_kinds,
    const TypeInterner&      ti
);

} // namespace mycc::sema
