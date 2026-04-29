
#pragma once

#include "diag/diagnostic.h"
#include "parser/ast.h"

namespace mycc::sema {

class TypeInterner;

void check_moves(ast::Program& prog, const TypeInterner& ti,
                 diag::DiagnosticEngine& diag);

} 
