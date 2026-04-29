
#pragma once

#include "diag/diagnostic.h"
#include "parser/ast.h"

namespace mycc::sema {

void desugar_program(ast::Program& prog, diag::DiagnosticEngine& diag);

} 
