/* раскрытие оператора |>
переписывает каждый PipeExpr в АСТ в эквивалентный CallExpr, чтобы последующие фазы (проверка типов, IR, кодогенерация) не знали про |>:
a |> f          ==>  f(a)
a |> f(b, c)    ==>  f(a, b, c)
неподдерживаемые правые части сообщаются через движок диагностики,
PipeExpr остается на месте, но проход 2 его пропустит*/
#pragma once

#include "diag/diagnostic.h"
#include "parser/ast.h"

namespace mycc::sema {

void desugar_program(ast::Program& prog, diag::DiagnosticEngine& diag);

} // namespace mycc::sema
