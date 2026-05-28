// текстовый эмиттер LLVM IR - codegen.md §6
module;

#include <cstdint>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

module mycc.codegen;

import mycc.lexer;
import mycc.parser;
import mycc.sema;
import mycc.ir;

namespace mycc::cg {

namespace {

using sema::TypeId;
using sema::TypeKind;
using sema::TypeInterner;

// утилиты

bool is_empty_module(const ir::Module& m) {
    return m.types == nullptr && m.functions.empty()
        && m.strings.empty() && m.globals.empty();
}

bool is_terminator(ir::Op op) {
    return op == ir::Op::Ret || op == ir::Op::Jmp || op == ir::Op::Br;
}

std::string hex_double(double v) {
    union { double d; uint64_t b; } u; u.d = v;
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%016llX",
                  static_cast<unsigned long long>(u.b));
    return buf;
}

//эмиссия типов

//если type == hollow, возвращает void - пригодно только в позиции возврата
std::string ll_type(const TypeInterner& ti, TypeId t, bool storage = false) {
    return lower_type(t, ti, storage);
}

bool is_void_ty(const TypeInterner& ti, TypeId t) {
    return t != sema::kInvalidTypeId && ti.get(t).kind == TypeKind::Hollow;
}

bool is_bool_ty(const TypeInterner& ti, TypeId t) {
    return t != sema::kInvalidTypeId && ti.get(t).kind == TypeKind::Bool;
}

bool is_string_ty(const TypeInterner& ti, TypeId t) {
    return t != sema::kInvalidTypeId && ti.get(t).kind == TypeKind::String;
}

bool is_struct_ty(const TypeInterner& ti, TypeId t) {
    return t != sema::kInvalidTypeId && ti.get(t).kind == TypeKind::Struct;
}

bool is_array_ty(const TypeInterner& ti, TypeId t) {
    return t != sema::kInvalidTypeId && ti.get(t).kind == TypeKind::Array;
}

// экранирование байта строкового литерала в синтаксис LLVM `c"..."`
std::string escape_byte(unsigned char b) {
    if (b == '\\' || b == '"' || b < 0x20 || b >= 0x7F) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\%02X", b);
        return buf;
    }
    return std::string(1, static_cast<char>(b));
}

// emitter

class Emitter {
public:
    explicit Emitter(const ir::Module& mod) : mod_(mod), ti_(mod.types) {}

    std::string run() {
        if (is_empty_module(mod_)) return {};

        prebuild_symbols();

        for (const auto& fp : mod_.functions) {
            emit_function(*fp);
            body_ << "\n";
        }

        std::ostringstream out;
        out << "; ModuleID = 'mycc'\n";
        out << "target triple = \"x86_64-unknown-linux-gnu\"\n";
        out << "\n";
        // именованные типы: %string + все struct-типы, упомянутые в модуле
        out << string_type_def() << "\n";
        emit_struct_type_defs(out);
        out << "\n";
        // глобальные строковые литералы
        for (const auto& sl : mod_.strings) {
            size_t n = sl.value.size() + 1; // включая trailing \0 для C ABI
            out << "@.str." << sl.id
                << " = private unnamed_addr constant ["
                << n << " x i8] c\"";
            for (unsigned char b : sl.value) out << escape_byte(b);
            out << "\\00\"\n";
        }
        if (!mod_.strings.empty()) out << "\n";

        // глобальные переменные/константы верхнего уровня и namespace
        for (const auto& g : mod_.globals) {
            const char* kind = g.is_const ? "constant" : "global";
            std::string ty = ll_type(*ti_, g.type, /*storage=*/true);
            std::string init = global_init_value(g);
            out << "@" << g.name << " = " << kind << " " << ty
                << " " << init << "\n";
        }
        if (!mod_.globals.empty()) out << "\n";

        std::string externs = preamble_.str();
        if (!externs.empty()) out << externs << "\n";
        out << body_.str();
        return out.str();
    }

    std::string global_init_value(const ir::GlobalVar& g) const {
        if (g.init.is_none()) return "zeroinitializer";
        switch (g.init.kind) {
        case ir::Operand::Kind::ConstInt:
            if (g.type != sema::kInvalidTypeId && ti_->is_unsigned_int(g.type))
                return std::to_string(g.init.cu);
            return std::to_string(g.init.ci);
        case ir::Operand::Kind::ConstFloat:  return hex_double(g.init.cf);
        case ir::Operand::Kind::ConstBool:   return g.init.cb ? "1" : "0";
        default: return "zeroinitializer";
        }
    }

private:
    const ir::Module& mod_;
    const TypeInterner* ti_;
    std::ostringstream preamble_;
    std::ostringstream body_;
    std::unordered_set<std::string> declared_externals_;
    std::unordered_set<uint32_t>    struct_types_seen_;
    std::vector<TypeId>             struct_types_order_;
    uint32_t synth_id_{0};
    // сопоставление source_name -> mangled-имя символа: позволяет правильно вызывать пользовательские функции/методы по callee из IR
    std::unordered_map<std::string, std::string> sym_by_source_;
    //cопоставление source_name -> return-тип, чтобы корректно эмитить вызовы
    std::unordered_map<std::string, TypeId> ret_by_source_;

    void prebuild_symbols() {
        for (const auto& fp : mod_.functions) {
            sym_by_source_[fp->source_name] = compute_symbol(*fp);
            ret_by_source_[fp->source_name] = fp->return_ty;
        }
    }

    std::string compute_symbol(const ir::Function& f) const {
        // §5.4: точка входа эмитится как @main без mangling
        if (f.is_main) return "main";
        std::vector<MangleScope> scopes;
        scopes.reserve(f.scopes.size());
        for (const auto& s : f.scopes) {
            scopes.push_back({s.is_impl ? MangleScope::Kind::Impl : MangleScope::Kind::Namespace, s.name});
        }
        sema::FnSymbol tmp;
        tmp.name = f.short_name.empty() ? f.source_name : f.short_name;
        tmp.params.reserve(f.params.size());
        for (const auto& p : f.params)
            tmp.params.push_back({p.type, p.name});
        return mangle(tmp, scopes, *ti_);
    }

    void ensure_external(const std::string& decl_line, const std::string& key) {
        if (!declared_externals_.insert(key).second) return;
        preamble_ << decl_line << "\n";
    }

    void note_struct_type(TypeId t) {
        if (t == sema::kInvalidTypeId) return;
        const auto& td = ti_->get(t);
        if (td.kind == TypeKind::Struct) {
            if (struct_types_seen_.insert(t.index).second) {
                struct_types_order_.push_back(t);
                if (td.struct_decl) {
                    for (const auto& f : td.struct_decl->fields) {
                    }
                }
            }
        } else if (td.kind == TypeKind::Array || td.kind == TypeKind::Range) {
            note_struct_type(td.elem);
        }
    }

    void emit_struct_type_defs(std::ostringstream& out) {
        // сначала пройдёмся по типам всех инструкций модуля, чтобы собрать все встречающиеся struct-типы.
        for (const auto& fp : mod_.functions) {
            for (const auto& p : fp->params) note_struct_type(p.type);
            note_struct_type(fp->return_ty);
            for (const auto& bb : fp->blocks) {
                for (const auto& I : bb.insts) {
                    note_struct_type(I.type);
                    for (const auto& a : I.args) note_struct_type(a.type);
                    if (!I.result.is_none()) note_struct_type(I.result.type);
                }
            }
        }
        for (const auto& g : mod_.globals) note_struct_type(g.type);

        for (TypeId t : struct_types_order_) {
            const auto& td = ti_->get(t);
            out << "%" << td.display << " = type { ";
            if (td.struct_decl) {
                size_t i = 0;
                for (const auto& f : td.struct_decl->fields) {
                    if (i++) out << ", ";
                    out << field_type_string(f.type.get());
                }
            }
            out << " }\n";
        }
    }

    std::string field_type_string(const ast::TypeNode* tn) const {
        if (!tn) return "i32";
        using NK = ast::NodeKind;
        switch (tn->kind) {
        case NK::BuiltinTypeRef: {
            auto* b = static_cast<const ast::BuiltinTypeRef*>(tn);
            using TK = lex::TokenKind;
            switch (b->builtin) {
            case TK::Int8: case TK::Uint8: return "i8";
            case TK::Int16: case TK::Uint16: return "i16";
            case TK::Int32: case TK::Uint32: return "i32";
            case TK::Int64: case TK::Uint64: return "i64";
            case TK::Float32: return "float";
            case TK::Float64: return "double";
            case TK::KwBool: return "i8";
            case TK::KwString: return "%string";
            case TK::Hollow: return "void";
            default: return "i32";
            }
        }
        case NK::ArrayTypeRef: {
            auto* a = static_cast<const ast::ArrayTypeRef*>(tn);
            return "[" + std::to_string(a->size) + " x "
                 + field_type_string(a->elem_type.get()) + "]";
        }
        case NK::RangeTypeRef: {
            auto* r = static_cast<const ast::RangeTypeRef*>(tn);
            std::string e = field_type_string(r->elem_type.get());
            return "{ " + e + ", " + e + " }";
        }
        case NK::NamedTypeRef: {
            auto* n = static_cast<const ast::NamedTypeRef*>(tn);
            return "%" + n->name;
        }
        case NK::NamespacedTypeRef: {
            auto* nt = static_cast<const ast::NamespacedTypeRef*>(tn);
            return "%" + nt->name;
        }
        default: return "i32";
        }
    }

    std::string fresh_synth() {
        return "%s" + std::to_string(synth_id_++);
    }

    // операнды

    std::string operand_value(const ir::Operand& o) const {
        switch (o.kind) {
        case ir::Operand::Kind::None:        return "<none>";
        case ir::Operand::Kind::ConstInt: {
            if (o.type != sema::kInvalidTypeId && ti_->is_unsigned_int(o.type))
                return std::to_string(o.cu);
            return std::to_string(o.ci);
        }
        case ir::Operand::Kind::ConstFloat:  return hex_double(o.cf);
        case ir::Operand::Kind::ConstBool:   return o.cb ? "true" : "false";
        case ir::Operand::Kind::ConstString: return "@.str." + std::to_string(o.string_id);
        case ir::Operand::Kind::Temp:        return "%t" + std::to_string(o.temp_id);
        case ir::Operand::Kind::Named:       return "%" + o.name;
        case ir::Operand::Kind::Global:      return "@" + o.name;
        case ir::Operand::Kind::Label:       return "label %" + o.name;
        }
        return "<?>";
    }

    // эмиссия функций / блоков

    void emit_function(const ir::Function& f) {
        std::string sym = sym_by_source_[f.source_name];
        if (sym.empty()) sym = compute_symbol(f);

        //сигнатура: для main всегда i32, иначе по return_ty.
        std::string ret_ty = f.is_main
            ? std::string("i32")
            : ll_type(*ti_, f.return_ty, /*storage=*/false);
        body_ << "define " << ret_ty << " @" << sym << "(";
        for (size_t i = 0; i < f.params.size(); ++i) {
            if (i) body_ << ", ";
            body_ << ll_type(*ti_, f.params[i].type, /*storage=*/false)
                  << " %" << f.params[i].name;
        }
        body_ << ") {\n";

        for (const auto& bb : f.blocks) {
            if (bb.label.starts_with("defer.body.")) continue;
            if (bb.label.starts_with("defer.resume")) continue;

            body_ << bb.label << ":\n";
            for (const auto& ins : bb.insts)
                emit_inst(ins, f);

            // гарантия терминатора: LLVM требует, чтобы каждый блок завершался
            bool terminated = !bb.insts.empty() && is_terminator(bb.insts.back().op);
            if (!terminated) {
                if (!f.is_main && is_void_ty(*ti_, f.return_ty))
                    body_ << "    ret void\n";
                else
                    body_ << "    unreachable\n";
            }
        }
        body_ << "}\n";
    }

    void emit_inst(const ir::Inst& I, const ir::Function& f) {
        switch (I.op) { 
        case ir::Op::Alloca:    emit_alloca(I); break;
        case ir::Op::Load:      emit_load(I); break;
        case ir::Op::Store:     emit_store(I); break;
        case ir::Op::Jmp:       emit_jmp(I); break;
        case ir::Op::Br:        emit_br(I); break;
        case ir::Op::Ret:       emit_ret(I, f); break;
        case ir::Op::Cast:      emit_cast(I); break;
        case ir::Op::Neg:       emit_neg(I); break;
        case ir::Op::LNot:      emit_lnot(I); break;
        case ir::Op::Call:      emit_call(I); break;
        case ir::Op::StrLit:    emit_strlit(I); break;
        case ir::Op::GetField:  emit_getfield(I); break;
        case ir::Op::GetElem:   emit_getelem(I); break;
        case ir::Op::Add: case ir::Op::Sub: case ir::Op::Mul:
        case ir::Op::SDiv: case ir::Op::UDiv: case ir::Op::FDiv:
        case ir::Op::SRem: case ir::Op::URem:
            emit_arith(I); break;
        case ir::Op::IEq: case ir::Op::INe:
        case ir::Op::SLt: case ir::Op::SGt: case ir::Op::SLe: case ir::Op::SGe:
        case ir::Op::ULt: case ir::Op::UGt: case ir::Op::ULe: case ir::Op::UGe:
        case ir::Op::FEq: case ir::Op::FNe:
        case ir::Op::FLt: case ir::Op::FGt: case ir::Op::FLe: case ir::Op::FGe:
            emit_cmp(I); break;
        case ir::Op::LAnd: case ir::Op::LOr:
            emit_logic(I); break;

        case ir::Op::RangeNew:
        case ir::Op::RangeNext: body_ << "    ; unsupported: range\n";     break;
        case ir::Op::DeferPush:
        case ir::Op::DeferEmit: body_ << "    ; unsupported: defer\n";     break;
        }
    }

    // инструкции

    void emit_alloca(const ir::Inst& I) {
        body_ << "    " << operand_value(I.result)
              << " = alloca " << ll_type(*ti_, I.type, /*storage=*/true)
              << "\n";
    }

    void emit_load(const ir::Inst& I) {
        TypeId t = I.type;
        std::string ptr = operand_value(I.args[0]);
        if (is_bool_ty(*ti_, t)) {
            // bool: хранится как i8, в SSA-операнде - i1.
            std::string raw = fresh_synth();
            body_ << "    " << raw << " = load i8, ptr " << ptr << "\n";
            body_ << "    " << operand_value(I.result)
                  << " = trunc i8 " << raw << " to i1\n";
        } else {
            body_ << "    " << operand_value(I.result)
                  << " = load " << ll_type(*ti_, t) << ", ptr " << ptr << "\n";
        }
    }

    void emit_store(const ir::Inst& I) {
        TypeId t = I.type;
        std::string val = operand_value(I.args[0]);
        std::string ptr = operand_value(I.args[1]);
        if (is_bool_ty(*ti_, t)) {
            std::string ext = fresh_synth();
            body_ << "    " << ext << " = zext i1 " << val << " to i8\n";
            body_ << "    store i8 " << ext << ", ptr " << ptr << "\n";
        } else {
            body_ << "    store " << ll_type(*ti_, t) << " " << val
                  << ", ptr " << ptr << "\n";
        }
    }

    void emit_jmp(const ir::Inst& I) {
        body_ << "    br label %" << I.then_label << "\n";
    }

    void emit_br(const ir::Inst& I) {
        body_ << "    br i1 " << operand_value(I.args[0])
              << ", label %" << I.then_label
              << ", label %" << I.else_label << "\n";
    }

    void emit_ret(const ir::Inst& I, const ir::Function& f) {
        if (I.args.empty()) {
            if (f.is_main) {
                body_ << "    ret i32 0\n";
            } else if (is_void_ty(*ti_, f.return_ty)) {
                body_ << "    ret void\n";
            } else {
                body_ << "    unreachable\n";
            }
            return;
        }
        body_ << "    ret " << ll_type(*ti_, I.type) << " "
              << operand_value(I.args[0]) << "\n";
    }

    void emit_cast(const ir::Inst& I) {
        TypeId to_ty   = I.type;
        TypeId from_ty = I.args[0].type;
        std::string from = ll_type(*ti_, from_ty);
        std::string to   = ll_type(*ti_, to_ty);
        std::string val  = operand_value(I.args[0]);
        std::string res  = operand_value(I.result);

        switch (I.cast_kind) {
        case ir::CastKind::SExt:
            body_ << "    " << res << " = sext " << from << " " << val << " to " << to << "\n";
            break;
        case ir::CastKind::ZExt:
            body_ << "    " << res << " = zext " << from << " " << val << " to " << to << "\n";
            break;
        case ir::CastKind::Trunc:
            body_ << "    " << res << " = trunc " << from << " " << val << " to " << to << "\n";
            break;
        case ir::CastKind::FPExt:
            body_ << "    " << res << " = fpext " << from << " " << val << " to " << to << "\n";
            break;
        case ir::CastKind::FPTrunc:
            body_ << "    " << res << " = fptrunc " << from << " " << val  << " to " << to << "\n";
            break;
        case ir::CastKind::SIToFP:
            body_ << "    " << res << " = sitofp " << from << " " << val << " to " << to << "\n";
            break;
        case ir::CastKind::UIToFP:
            body_ << "    " << res << " = uitofp " << from << " " << val << " to " << to << "\n";
            break;
        case ir::CastKind::FPToSI:
            body_ << "    " << res << " = fptosi " << from << " " << val << " to " << to << "\n";
            break;
        case ir::CastKind::FPToUI:
            body_ << "    " << res << " = fptoui " << from << " " << val << " to " << to << "\n";
            break;
        case ir::CastKind::BoolToInt:
            body_ << "    " << res << " = zext i1 " << val << " to " << to << "\n";
            break;
        case ir::CastKind::IntToBool:
            body_ << "    " << res << " = icmp ne " << from << " " << val << ", 0\n";
            break;
        case ir::CastKind::Bitcast:
            body_ << "    " << res << " = bitcast " << from << " " << val << " to " << to << "\n";
            break;
        }
    }

    void emit_neg(const ir::Inst& I) {
        std::string ty = ll_type(*ti_, I.type);
        std::string v  = operand_value(I.args[0]);
        std::string r  = operand_value(I.result);
        if (ti_->is_float(I.type))
            body_ << "    " << r << " = fneg " << ty << " " << v << "\n";
        else
            body_ << "    " << r << " = sub " << ty << " 0, " << v << "\n";
    }

    void emit_lnot(const ir::Inst& I) {
        std::string v = operand_value(I.args[0]);
        body_ << "    " << operand_value(I.result)  << " = xor i1 " << v << ", true\n";
        (void)is_bool_ty;
    }

    void emit_arith(const ir::Inst& I) {
        const char* opcode = nullptr;
        bool is_fp = ti_->is_float(I.type);
        switch (I.op) {
        case ir::Op::Add:  opcode = is_fp ? "fadd" : "add"; break;
        case ir::Op::Sub:  opcode = is_fp ? "fsub" : "sub"; break;
        case ir::Op::Mul:  opcode = is_fp ? "fmul" : "mul"; break;
        case ir::Op::SDiv: opcode = "sdiv"; break;
        case ir::Op::UDiv: opcode = "udiv"; break;
        case ir::Op::FDiv: opcode = "fdiv"; break;
        case ir::Op::SRem: opcode = "srem"; break;
        case ir::Op::URem: opcode = "urem"; break;
        default: opcode = "<?>"; break;
        }
        body_ << "    " << operand_value(I.result)
              << " = " << opcode << " " << ll_type(*ti_, I.type) << " "
              << operand_value(I.args[0]) << ", "
              << operand_value(I.args[1]) << "\n";
    }

    void emit_cmp(const ir::Inst& I) {
        const char* tool = nullptr;
        const char* pred = nullptr;
        switch (I.op) {
        case ir::Op::IEq: tool = "icmp"; pred = "eq"; break;
        case ir::Op::INe: tool = "icmp"; pred = "ne"; break;
        case ir::Op::SLt: tool = "icmp"; pred = "slt"; break;
        case ir::Op::SGt: tool = "icmp"; pred = "sgt"; break;
        case ir::Op::SLe: tool = "icmp"; pred = "sle"; break;
        case ir::Op::SGe: tool = "icmp"; pred = "sge"; break;
        case ir::Op::ULt: tool = "icmp"; pred = "ult"; break;
        case ir::Op::UGt: tool = "icmp"; pred = "ugt"; break;
        case ir::Op::ULe: tool = "icmp"; pred = "ule"; break;
        case ir::Op::UGe: tool = "icmp"; pred = "uge"; break;
        case ir::Op::FEq: tool = "fcmp"; pred = "oeq"; break;
        case ir::Op::FNe: tool = "fcmp"; pred = "one"; break;
        case ir::Op::FLt: tool = "fcmp"; pred = "olt"; break;
        case ir::Op::FGt: tool = "fcmp"; pred = "ogt"; break;
        case ir::Op::FLe: tool = "fcmp"; pred = "ole"; break;
        case ir::Op::FGe: tool = "fcmp"; pred = "oge"; break;
        default: tool = "icmp"; pred = "eq"; break;
        }
        body_ << "    " << operand_value(I.result)
              << " = " << tool << " " << pred << " "
              << ll_type(*ti_, I.type) << " "
              << operand_value(I.args[0]) << ", "
              << operand_value(I.args[1]) << "\n";
    }

    void emit_logic(const ir::Inst& I) {
        const char* op = (I.op == ir::Op::LAnd) ? "and" : "or";
        body_ << "    " << operand_value(I.result) << " = " << op << " i1 "
              << operand_value(I.args[0]) << ", "
              << operand_value(I.args[1]) << "\n";
    }

    // строковый литерал: собираем %string-агрегат через insertvalue
    // %t.0 = insertvalue %string undef, ptr @.str.N, 0
    // %t   = insertvalue %string %t.0, i64 <len>, 1
    void emit_strlit(const ir::Inst& I) {
        if (I.args.empty()) return;
        const auto& a = I.args[0];
        uint32_t sid = a.string_id;
        size_t len = 0;
        for (const auto& sl : mod_.strings)
            if (sl.id == sid) { len = sl.value.size(); break; }

        std::string mid = fresh_synth();
        body_ << "    " << mid
              << " = insertvalue %string undef, ptr @.str." << sid << ", 0\n";
        body_ << "    " << operand_value(I.result)
              << " = insertvalue %string " << mid
              << ", i64 " << len << ", 1\n";
    }

    // адрес поля структуры: getelementptr %S, ptr <recv>, i32 0, i32 <idx>
    void emit_getfield(const ir::Inst& I) {
        TypeId recv_ty = I.args[0].type;
        std::string struct_ll = ll_type(*ti_, recv_ty, /*storage=*/true);
        body_ << "    " << operand_value(I.result)
              << " = getelementptr " << struct_ll
              << ", ptr " << operand_value(I.args[0])
              << ", i32 0, i32 " << I.field_index << "\n";
    }

    // адрес элемента: getelementptr [N x T], ptr <base>, i32 0, i64 <idx>
    void emit_getelem(const ir::Inst& I) {
        TypeId base_ty = I.args[0].type;
        std::string base_ll = ll_type(*ti_, base_ty, /*storage=*/true);
        // ширина индекса - для уверенности sext до i64
        std::string idx = operand_value(I.args[1]);
        TypeId idx_ty = I.args[1].type;
        if (idx_ty != sema::kInvalidTypeId && ti_->bit_width(idx_ty) < 64
            && ti_->is_signed_int(idx_ty)) {
            std::string ext = fresh_synth();
            body_ << "    " << ext << " = sext "
                  << ll_type(*ti_, idx_ty) << " " << idx << " to i64\n";
            idx = ext;
        } else if (idx_ty != sema::kInvalidTypeId && ti_->bit_width(idx_ty) < 64
                   && ti_->is_unsigned_int(idx_ty)) {
            std::string ext = fresh_synth();
            body_ << "    " << ext << " = zext "
                  << ll_type(*ti_, idx_ty) << " " << idx << " to i64\n";
            idx = ext;
        }
        body_ << "    " << operand_value(I.result)
              << " = getelementptr " << base_ll
              << ", ptr " << operand_value(I.args[0])
              << ", i32 0, i64 " << idx << "\n";
    }

    //вызовы

    void emit_call(const ir::Inst& I) {
        switch (I.call_kind) {
        case ir::CallKind::User:   emit_user_call(I); break;
        case ir::CallKind::Method: emit_user_call(I); break;
        case ir::CallKind::Runtime: emit_runtime_call(I); break;
        case ir::CallKind::Builtin: emit_builtin_call(I); break;
        }
    }

    void emit_user_call(const ir::Inst& I) {
        std::string sym;
        auto it = sym_by_source_.find(I.callee);
        if (it != sym_by_source_.end()) {
            sym = it->second;
        } else {
            // внешний/незарегистрированный символ - заявим декларацию
            sym = I.callee;
            for (auto& c : sym) if (c == ':') c = '_';
            std::ostringstream decl;
            decl << "declare " << ll_type(*ti_, I.type) << " @" << sym << "(";
            for (size_t k = 0; k < I.args.size(); ++k) {
                if (k) decl << ", ";
                decl << ll_type(*ti_, I.args[k].type);
            }
            decl << ")";
            ensure_external(decl.str(), "u:" + sym);
        }
        emit_raw_call(I, sym);
    }

    void emit_runtime_call(const ir::Inst& I) {
        std::ostringstream decl;
        decl << "declare " << ll_type(*ti_, I.type) << " @" << I.callee << "(";
        for (size_t k = 0; k < I.args.size(); ++k) {
            if (k) decl << ", ";
            decl << ll_type(*ti_, I.args[k].type);
        }
        decl << ")";
        ensure_external(decl.str(), "rt:" + I.callee);
        emit_raw_call(I, I.callee);
    }

    void emit_raw_call(const ir::Inst& I, const std::string& sym) {
        std::ostringstream args;
        for (size_t k = 0; k < I.args.size(); ++k) {
            if (k) args << ", ";
            args << ll_type(*ti_, I.args[k].type) << " "
                 << operand_value(I.args[k]);
        }
        if (!I.result.is_none()) {
            body_ << "    " << operand_value(I.result) << " = call "
                  << ll_type(*ti_, I.type) << " @" << sym << "(" << args.str() << ")\n";
        } else {
            body_ << "    call " << ll_type(*ti_, I.type) << " @" << sym
                  << "(" << args.str() << ")\n";
        }
    }

    // маршрутизация встроенных print/println/exit/panic/input/len
    // codegen.md §5.5
    void emit_builtin_call(const ir::Inst& I) {
        if (I.callee == "print" || I.callee == "println") {
            emit_print(I, /*newline=*/I.callee == "println");
            return;
        }
        if (I.callee == "exit") {
            emit_rt_decl("rt_exit", "void", {"i32"});
            // exit(int32) - аргумент уже i32
            body_ << "    call void @rt_exit(i32 "
                  << operand_value(I.args[0]) << ")\n";
            return;
        }
        if (I.callee == "panic") {
            // panic(string) - аргумент %string, второй -- номер строки.
            // пока вызываем напрямую с line = 0 чтобы llc принял IR.
            emit_rt_decl("rt_panic", "void", {"%string", "i32"});
            body_ << "    call void @rt_panic(%string "
                  << operand_value(I.args[0]) << ", i32 0)\n"
                  << "    unreachable\n";
            return;
        }
        if (I.callee == "input") {
            emit_rt_decl("rt_input", "%string", {});
            body_ << "    " << operand_value(I.result)
                  << " = call %string @rt_input()\n";
            return;
        }
        if (I.callee == "len") {
            // len(string) -> int32 (sema/init_builtins)
            //Для массивов используется константа array_size, но текущая sema не  регистрирует len для массивов - этот случай не встречается.
            emit_rt_decl("rt_strlen", "i32", {"%string"});
            body_ << "    " << operand_value(I.result)
                  << " = call i32 @rt_strlen(%string "
                  << operand_value(I.args[0]) << ")\n";
            return;
        }
        body_ << "    ; unsupported: builtin '" << I.callee << "'\n";
    }

    void emit_print(const ir::Inst& I, bool newline) {
        const char* prefix = newline ? "rt_println_" : "rt_print_";

        if (I.args.empty()) {
            std::string sym = std::string("rt_println_empty");
            emit_rt_decl(sym, "void", {});
            body_ << "    call void @" << sym << "()\n";
            return;
        }

        TypeId at = I.args[0].type;
        std::string val = operand_value(I.args[0]);

        if (ti_->is_signed_int(at)) {
            std::string sym = prefix + std::string("i64");
            emit_rt_decl(sym, "void", {"i64"});
            std::string arg = val;
            if (ti_->bit_width(at) < 64) {
                std::string tmp = fresh_synth();
                body_ << "    " << tmp << " = sext "
                      << ll_type(*ti_, at) << " " << val << " to i64\n";
                arg = tmp;
            }
            body_ << "    call void @" << sym << "(i64 " << arg << ")\n";
            return;
        }
        if (ti_->is_unsigned_int(at)) {
            std::string sym = prefix + std::string("u64");
            emit_rt_decl(sym, "void", {"i64"});
            std::string arg = val;
            if (ti_->bit_width(at) < 64) {
                std::string tmp = fresh_synth();
                body_ << "    " << tmp << " = zext "
                      << ll_type(*ti_, at) << " " << val << " to i64\n";
                arg = tmp;
            }
            body_ << "    call void @" << sym << "(i64 " << arg << ")\n";
            return;
        }
        if (ti_->is_float(at)) {
            std::string sym = prefix + std::string("f64");
            emit_rt_decl(sym, "void", {"double"});
            std::string arg = val;
            if (ti_->bit_width(at) < 64) {
                std::string tmp = fresh_synth();
                body_ << "    " << tmp << " = fpext float " << val
                      << " to double\n";
                arg = tmp;
            }
            body_ << "    call void @" << sym << "(double " << arg << ")\n";
            return;
        }
        if (is_bool_ty(*ti_, at)) {
            std::string sym = prefix + std::string("bool");
            emit_rt_decl(sym, "void", {"i32"});
            std::string tmp = fresh_synth();
            body_ << "    " << tmp << " = zext i1 " << val << " to i32\n";
            body_ << "    call void @" << sym << "(i32 " << tmp << ")\n";
            return;
        }
        if (is_string_ty(*ti_, at)) {
            std::string sym = prefix + std::string("string");
            emit_rt_decl(sym, "void", {"%string"});
            body_ << "    call void @" << sym << "(%string " << val << ")\n";
            return;
        }
        body_ << "    ; unsupported: "
              << (newline ? "println" : "print") << " of non-scalar value\n";
    }

    void emit_rt_decl(const std::string& sym, const std::string& ret,
                      std::initializer_list<const char*> args) {
        std::ostringstream decl;
        decl << "declare " << ret << " @" << sym << "(";
        bool first = true;
        for (auto* a : args) {
            if (!first) decl << ", ";
            decl << a;
            first = false;
        }
        decl << ")";
        ensure_external(decl.str(), "rt:" + sym);
    }
};

} // namespace

std::string LlvmEmitter::emit(const ir::Module& mod) {
    Emitter e(mod);
    return e.run();
}

} // namespace mycc::cg
