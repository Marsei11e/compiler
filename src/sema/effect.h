/*валидация системы эффектов
обходит каждое аннотированное тело функции и проверяет, что каждая
эффектная операция (целочисленные / и %, индексирование массива, вызовы
функций с @io/@panics, вызовы неаннотированных функций) разрешена
объявленным набором эффектов функции
@pure  - нет i/o, нет паник, нет вызовов неаннотированных функций
@io / @panics - эти эффекты разрешены; вызовы неаннотированных предупреждают (§12.5)
без аннотаций - тело функции не проверяется*/
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

} // namespace mycc::sema
