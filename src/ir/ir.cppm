module;
#include "ir/_pod.h"
export module mycc.ir;

export namespace mycc::ir {

using ::mycc::ir::Op;
using ::mycc::ir::CastKind;
using ::mycc::ir::CallKind;
using ::mycc::ir::Operand;
using ::mycc::ir::Inst;
using ::mycc::ir::BasicBlock;
using ::mycc::ir::FnParam;
using ::mycc::ir::DeferEntry;
using ::mycc::ir::Function;
using ::mycc::ir::StringLiteral;
using ::mycc::ir::GlobalVar;
using ::mycc::ir::Module;

using ::mycc::ir::none_op;
using ::mycc::ir::temp;
using ::mycc::ir::named;
using ::mycc::ir::global;
using ::mycc::ir::label;
using ::mycc::ir::const_int;
using ::mycc::ir::const_uint;
using ::mycc::ir::const_float;
using ::mycc::ir::const_bool;
using ::mycc::ir::const_string;

using ::mycc::ir::lower_program;
using ::mycc::ir::dump_module;

} // namespace mycc::ir

export namespace mycc::ir::opt {

using ::mycc::ir::opt::const_fold;
using ::mycc::ir::opt::dce;
using ::mycc::ir::opt::optimize_module;

} // namespace mycc::ir::opt
