/*проверка перемещений для некопируемых range[T]
обходит каждое тело функции со своим стеком скоупов. локальная переменная
типа range[T] начинает как доступная. любой IdentExpr, ссылающийся на неё,
считается потребляющим использованием (range не может стоять в не-потребляющих
позициях, так как не сравнимо и не числовое). при повторном использовании
выдается "use of moved value 'name'". ветки объединяются консервативно (§7.5):
если переменная потреблена хотя бы в одной ветке - считается потребленной*/
#pragma once

#include "diag/_pod.h"
#include "parser/ast.h"

namespace mycc::sema {

class TypeInterner;

void check_moves(ast::Program& prog, const TypeInterner& ti,
                 diag::DiagnosticEngine& diag);

} // namespace mycc::sema
