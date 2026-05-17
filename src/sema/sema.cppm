module;
#include "sema/_pod.h"
export module mycc.sema;

export namespace mycc::sema {

using ::mycc::sema::TypeKind;
using ::mycc::sema::TypeId;
using ::mycc::sema::kInvalidTypeId;
using ::mycc::sema::TypeInterner;

using ::mycc::sema::VarSymbol;
using ::mycc::sema::FnSymbol;
using ::mycc::sema::OverloadSet;
using ::mycc::sema::StructSymbol;
using ::mycc::sema::NamespaceSymbol;
using ::mycc::sema::ScopeEntry;
using ::mycc::sema::Scope;

using ::mycc::sema::ArgKind;
using ::mycc::sema::OverloadStatus;
using ::mycc::sema::ResolveResult;
using ::mycc::sema::arg_kind_of;
using ::mycc::sema::filter_by_name;
using ::mycc::sema::resolve_call;

using ::mycc::sema::desugar_program;
using ::mycc::sema::check_effects;
using ::mycc::sema::check_moves;
using ::mycc::sema::check_control_flow;

using ::mycc::sema::Sema;

} // namespace mycc::sema
