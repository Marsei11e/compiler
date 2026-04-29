
#pragma once

#include "diag/diagnostic.h"
#include "parser/ast.h"

namespace mycc::sema {

void check_control_flow(ast::Program& prog, diag::DiagnosticEngine& diag);

} 
