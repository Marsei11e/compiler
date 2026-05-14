/* текстовый принтер IR — codegen.md §2.3 */
#include "ir/dump.h"

#include "sema/type.h"

#include <ostream>
#include <string>

import mycc.diag;

namespace mycc::ir {

namespace {

std::string ty_str(const sema::TypeInterner& ti, sema::TypeId t) {
    if (t == sema::kInvalidTypeId) return "?";
    return std::string(ti.display_name(t));
}

const char* op_name(Op op) {
    switch (op) {
    case Op::Add:        return "add";
    case Op::Sub:        return "sub";
    case Op::Mul:        return "mul";
    case Op::SDiv:       return "sdiv";
    case Op::UDiv:       return "udiv";
    case Op::FDiv:       return "fdiv";
    case Op::SRem:       return "srem";
    case Op::URem:       return "urem";
    case Op::Neg:        return "neg";
    case Op::IEq:        return "eq";
    case Op::INe:        return "ne";
    case Op::SLt:        return "slt";
    case Op::SGt:        return "sgt";
    case Op::SLe:        return "sle";
    case Op::SGe:        return "sge";
    case Op::ULt:        return "ult";
    case Op::UGt:        return "ugt";
    case Op::ULe:        return "ule";
    case Op::UGe:        return "uge";
    case Op::FEq:        return "feq";
    case Op::FNe:        return "fne";
    case Op::FLt:        return "flt";
    case Op::FGt:        return "fgt";
    case Op::FLe:        return "fle";
    case Op::FGe:        return "fge";
    case Op::LAnd:       return "and";
    case Op::LOr:        return "or";
    case Op::LNot:       return "not";
    case Op::Cast:       return "cast";
    case Op::Alloca:     return "alloca";
    case Op::Load:       return "load";
    case Op::Store:      return "store";
    case Op::GetField:   return "getfield";
    case Op::GetElem:    return "getelem";
    case Op::Jmp:        return "jmp";
    case Op::Br:         return "br";
    case Op::Ret:        return "ret";
    case Op::Call:       return "call";
    case Op::StrLit:     return "strlit";
    case Op::RangeNew:   return "range_new";
    case Op::RangeNext:  return "range_next";
    case Op::DeferPush:  return "defer_push";
    case Op::DeferEmit:  return "defer_emit";
    }
    return "?op?";
}

const char* cast_name(CastKind k) {
    switch (k) {
    case CastKind::SExt:      return "sext";
    case CastKind::ZExt:      return "zext";
    case CastKind::Trunc:     return "trunc";
    case CastKind::FPExt:     return "fpext";
    case CastKind::FPTrunc:   return "fptrunc";
    case CastKind::SIToFP:    return "sitofp";
    case CastKind::UIToFP:    return "uitofp";
    case CastKind::FPToSI:    return "fptosi";
    case CastKind::FPToUI:    return "fptoui";
    case CastKind::BoolToInt: return "btoi";
    case CastKind::IntToBool: return "itob";
    case CastKind::Bitcast:   return "bitcast";
    }
    return "?cast?";
}

void write_operand(std::ostream& os, const Operand& o,
                   const sema::TypeInterner& ti) {
    switch (o.kind) {
    case Operand::Kind::None:
        os << "<none>"; break;
    case Operand::Kind::ConstInt:
        if (o.type != sema::kInvalidTypeId && ti.is_unsigned_int(o.type))
            os << o.cu << "u";
        else
            os << o.ci;
        break;
    case Operand::Kind::ConstFloat:
        os << o.cf; break;
    case Operand::Kind::ConstBool:
        os << (o.cb ? "true" : "false"); break;
    case Operand::Kind::ConstString:
        os << "@.str." << o.string_id; break;
    case Operand::Kind::Temp:
        os << "%t" << o.temp_id; break;
    case Operand::Kind::Named:
        os << "%" << o.name; break;
    case Operand::Kind::Global:
        os << "@" << o.name; break;
    case Operand::Kind::Label:
        os << "label %" << o.name; break;
    }
}

void dump_inst(std::ostream& os, const Inst& I, const sema::TypeInterner& ti) {
    os << "    ";

    auto write_args = [&](size_t skip = 0) {
        for (size_t i = skip; i < I.args.size(); ++i) {
            if (i > skip) os << ", ";
            write_operand(os, I.args[i], ti);
        }
    };

    switch (I.op) {
    case Op::Alloca: {
        write_operand(os, I.result, ti);
        os << " = alloca " << ty_str(ti, I.type);
        break;
    }
    case Op::Load: {
        write_operand(os, I.result, ti);
        os << " = load " << ty_str(ti, I.type) << ", ";
        write_operand(os, I.args[0], ti);
        break;
    }
    case Op::Store: {
        os << "store " << ty_str(ti, I.type) << " ";
        write_operand(os, I.args[0], ti);
        os << ", ";
        write_operand(os, I.args[1], ti);
        break;
    }
    case Op::GetField: {
        write_operand(os, I.result, ti);
        os << " = getfield " << ty_str(ti, I.type) << ", ";
        write_operand(os, I.args[0], ti);
        os << ", " << I.field_index;
        break;
    }
    case Op::GetElem: {
        write_operand(os, I.result, ti);
        os << " = getelem " << ty_str(ti, I.type) << ", ";
        write_operand(os, I.args[0], ti);
        os << ", ";
        write_operand(os, I.args[1], ti);
        break;
    }
    case Op::Jmp: {
        os << "jmp label %" << I.then_label;
        break;
    }
    case Op::Br: {
        os << "br ";
        write_operand(os, I.args[0], ti);
        os << ", label %" << I.then_label
           << ", label %" << I.else_label;
        break;
    }
    case Op::Ret: {
        os << "ret";
        if (!I.args.empty()) {
            os << " " << ty_str(ti, I.type) << " ";
            write_operand(os, I.args[0], ti);
        }
        break;
    }
    case Op::Call: {
        if (!I.result.is_none()) {
            write_operand(os, I.result, ti);
            os << " = ";
        }
        os << "call " << ty_str(ti, I.type) << " @" << I.callee << "(";
        write_args();
        os << ")";
        break;
    }
    case Op::Cast: {
        write_operand(os, I.result, ti);
        os << " = " << cast_name(I.cast_kind) << " ";
        write_operand(os, I.args[0], ti);
        os << " to " << ty_str(ti, I.type);
        break;
    }
    case Op::StrLit: {
        write_operand(os, I.result, ti);
        os << " = strlit ";
        write_operand(os, I.args[0], ti);
        break;
    }
    case Op::RangeNew: {
        write_operand(os, I.result, ti);
        os << " = range_new " << ty_str(ti, I.type) << " ";
        write_args();
        break;
    }
    case Op::RangeNext: {
        write_operand(os, I.result, ti);
        os << " = range_next " << ty_str(ti, I.type) << " ";
        write_args();
        break;
    }
    case Op::DeferPush: {
        os << "defer_push #" << I.defer_id
           << ", body label %" << I.defer_body_label;
        break;
    }
    case Op::DeferEmit: {
        os << "defer_emit";
        for (size_t i = 0; i < I.args.size(); ++i) {
            if (i == 0) os << " ";
            else        os << ", ";
            os << "#" << I.args[i].cu;
        }
        break;
    }
    case Op::Neg: {
        write_operand(os, I.result, ti);
        os << " = neg " << ty_str(ti, I.type) << " ";
        write_operand(os, I.args[0], ti);
        break;
    }
    case Op::LNot: {
        write_operand(os, I.result, ti);
        os << " = not ";
        write_operand(os, I.args[0], ti);
        break;
    }
    default: {
        // обобщенная бинарная форма: %r = <op> <ty> <a>, <b>
        if (!I.result.is_none()) {
            write_operand(os, I.result, ti);
            os << " = ";
        }
        os << op_name(I.op) << " " << ty_str(ti, I.type) << " ";
        write_args();
        break;
    }
    }
    os << "\n";
}

void dump_function(std::ostream& os, const Function& f,
                   const sema::TypeInterner& ti) {
    os << "function " << f.source_name << "(";
    for (size_t i = 0; i < f.params.size(); ++i) {
        if (i) os << ", ";
        os << ty_str(ti, f.params[i].type) << " %" << f.params[i].name;
    }
    os << ") -> " << ty_str(ti, f.return_ty) << " {\n";
    for (const auto& bb : f.blocks) {
        os << bb.label << ":\n";
        for (const auto& I : bb.insts) dump_inst(os, I, ti);
    }
    if (!f.defer_table.empty()) {
        os << "  ; defer table:\n";
        for (const auto& d : f.defer_table)
            os << "  ;   #" << d.id << " -> %" << d.body_label << "\n";
    }
    os << "}\n";
}

} // namespace

void dump_module(const Module& mod, std::ostream& os) {
    if (!mod.types) return;
    if (!mod.strings.empty()) {
        for (const auto& s : mod.strings) {
            os << "@.str." << s.id << " = constant string \"";
            for (char c : s.value) {
                switch (c) {
                case '\n': os << "\\n"; break;
                case '\t': os << "\\t"; break;
                case '\r': os << "\\r"; break;
                case '\\': os << "\\\\"; break;
                case '"':  os << "\\\""; break;
                default:
                    if (c >= 32 && c < 127) os << c;
                    else os << "\\x" << std::hex << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
                }
            }
            os << "\"\n";
        }
        os << "\n";
    }
    if (!mod.globals.empty()) {
        for (const auto& g : mod.globals) {
            os << (g.is_const ? "const @" : "global @") << g.name
               << " : " << ty_str(*mod.types, g.type) << "\n";
        }
        os << "\n";
    }
    bool first = true;
    for (const auto& fp : mod.functions) {
        if (!first) os << "\n";
        first = false;
        dump_function(os, *fp, *mod.types);
    }
}

} // namespace mycc::ir
