/* принимает аннотированный АСТ от Sema (в каждом Expr заполнен resolved_type_id,
 * поток управления и эффекты проверены) и эмитирует линейный трехадресный
 * Module, готовый для оптимизаций и эмиссии LLVM IR.
 *
 * инварианты Sema (семантика §16.1) считаются выполненными; lowerer их не перепроверяет.
 */
#pragma once

#include "diag/diagnostic.h"
#include "ir/ir.h"
#include "parser/ast.h"
#include "sema/sema.h"

#include <memory>

namespace mycc::ir {

std::unique_ptr<Module> lower_program(ast::Program& prog,
                                      sema::Sema& sema,
                                      diag::DiagnosticEngine& diag);

} // namespace mycc::ir
