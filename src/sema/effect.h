
#pragma once

#include "diag/diagnostic.h"
#include "parser/ast.h"
#include "sema/scope.h"
#include "sema/symbol.h"
#include "sema/type.h"

#include <unordered_map>

namespace mycc::sema {

void check_effects(ast::Program& prog,
                   Scope& global_scope,
                   const std::unordered_map<uint32_t, StructSymbol*>& struct_type_map,
                   const TypeInterner& types,
                   diag::DiagnosticEngine& diag);

} 
