
#pragma once

#include "parser/ast.h"
#include "sema/symbol.h"
#include "sema/type.h"

#include <span>
#include <string>

namespace mycc::sema {




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

} 
