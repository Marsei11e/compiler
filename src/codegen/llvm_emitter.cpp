// текстовый эмиттер LLVM IR - каркас
module;

#include <string>

module mycc.codegen;

import mycc.ir;

namespace mycc::cg {

std::string LlvmEmitter::emit(const ir::Module& /*mod*/) {
    return {};
}

} // namespace mycc::cg
