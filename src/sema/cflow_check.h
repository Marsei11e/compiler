/*проверка потока управления
 проверяет: break/continue внутри цикла (§7.6),
отсутствующий return на непустом пути (§7.7),
ограничения тела defer (§7.8)*/
#pragma once

#include "diag/diagnostic.h"
#include "parser/ast.h"

namespace mycc::sema {

void check_control_flow(ast::Program& prog, diag::DiagnosticEngine& diag);

} // namespace mycc::sema
