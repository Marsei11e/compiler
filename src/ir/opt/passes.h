/* IR-оптимизации: constant folding и dead code elimination (semantics §15)
optimize_module запускает обе фазы до фикспоинта. Каждая возвращает true,
если что-то изменлось, чтобы драйвер мог итерироваться.*/
#pragma once

#include "ir/ir.h"

namespace mycc::ir::opt {

bool const_fold(Module& mod);
bool dce(Module& mod);
void optimize_module(Module& mod);

} // namespace mycc::ir::opt
