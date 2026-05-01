/* текстовый принтер IR, формат описан в codegen.md §2.3 */
#pragma once

#include "ir/ir.h"

#include <ostream>

namespace mycc::ir {

void dump_module(const Module& mod, std::ostream& os);

} // namespace mycc::ir
