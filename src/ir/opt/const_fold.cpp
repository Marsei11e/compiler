// constant folding: см. semantics §15.1
#include "ir/opt/passes.h"

#include "sema/type.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

import mycc.diag;

namespace mycc::ir::opt {

namespace {

using sema::TypeId;
using sema::TypeKind;

Operand mk_const_int(int64_t s, uint64_t u, TypeId t) {
    Operand o;
    o.kind = Operand::Kind::ConstInt;
    o.ci = s;
    o.cu = u;
    o.type = t;
    return o;
}
Operand mk_const_float(double v, TypeId t) {
    Operand o;
    o.kind = Operand::Kind::ConstFloat;
    o.cf = v;
    o.type = t;
    return o;
}
Operand mk_const_bool(bool v) {
    Operand o;
    o.kind = Operand::Kind::ConstBool;
    o.cb = v;
    o.type = TypeId{static_cast<uint32_t>(TypeKind::Bool)};
    return o;
}

// маска младших bit_width бит
uint64_t mask_to_width(uint64_t v, uint32_t bw) {
    if (bw == 0 || bw >= 64) return v;
    return v & ((uint64_t{1} << bw) - 1);
}

// знаковое расширение по bit_width до int64
int64_t sext_to_i64(uint64_t v, uint32_t bw) {
    if (bw == 0 || bw >= 64) return static_cast<int64_t>(v);
    uint64_t m = uint64_t{1} << (bw - 1);
    v &= (uint64_t{1} << bw) - 1;
    return static_cast<int64_t>((v ^ m) - m);
}

Operand fold_int_binop(Op op, const Operand& a, const Operand& b,
                       TypeId opnd_ty, TypeId res_ty,
                       const sema::TypeInterner& ti, bool& ok) {
    ok = false;
    if (a.kind != Operand::Kind::ConstInt || b.kind != Operand::Kind::ConstInt)
        return {};

    uint32_t bw  = ti.bit_width(opnd_ty);
    bool     sgn = ti.is_signed_int(opnd_ty);

    // нормализуем операнды
    int64_t  sa = sgn ? sext_to_i64(a.cu, bw) : static_cast<int64_t>(mask_to_width(a.cu, bw));
    int64_t  sb = sgn ? sext_to_i64(b.cu, bw) : static_cast<int64_t>(mask_to_width(b.cu, bw));
    uint64_t ua = mask_to_width(a.cu, bw);
    uint64_t ub = mask_to_width(b.cu, bw);

    auto mk = [&](uint64_t u) -> Operand {
        uint64_t mu = mask_to_width(u, bw);
        int64_t  si = sgn ? sext_to_i64(mu, bw) : static_cast<int64_t>(mu);
        return mk_const_int(si, mu, res_ty);
    };
    auto mk_b = [&](bool v) -> Operand { return mk_const_bool(v); };

    switch (op) {
    case Op::Add: ok = true; return mk(static_cast<uint64_t>(sa + sb));
    case Op::Sub: ok = true; return mk(static_cast<uint64_t>(sa - sb));
    case Op::Mul: ok = true; return mk(static_cast<uint64_t>(sa * sb));
    case Op::SDiv:
        if (sb == 0) return {};  
        if (sa == INT64_MIN && sb == -1) return {}; // overflow - пусть упадёт в рантайме
        ok = true; return mk(static_cast<uint64_t>(sa / sb));
    case Op::UDiv:
        if (ub == 0) return {};
        ok = true; return mk(ua / ub);
    case Op::SRem:
        if (sb == 0) return {};
        if (sa == INT64_MIN && sb == -1) return {};
        ok = true; return mk(static_cast<uint64_t>(sa % sb));
    case Op::URem:
        if (ub == 0) return {};
        ok = true; return mk(ua % ub);
    case Op::IEq: ok = true; return mk_b(ua == ub);
    case Op::INe: ok = true; return mk_b(ua != ub);
    case Op::SLt: ok = true; return mk_b(sa < sb);
    case Op::SGt: ok = true; return mk_b(sa > sb);
    case Op::SLe: ok = true; return mk_b(sa <= sb);
    case Op::SGe: ok = true; return mk_b(sa >= sb);
    case Op::ULt: ok = true; return mk_b(ua < ub);
    case Op::UGt: ok = true; return mk_b(ua > ub);
    case Op::ULe: ok = true; return mk_b(ua <= ub);
    case Op::UGe: ok = true; return mk_b(ua >= ub);
    default: return {};
    }
}

Operand fold_float_binop(Op op, const Operand& a, const Operand& b,
                         TypeId res_ty, bool& ok) {
    ok = false;
    if (a.kind != Operand::Kind::ConstFloat ||
        b.kind != Operand::Kind::ConstFloat) return {};
    double x = a.cf, y = b.cf;
    switch (op) {
    case Op::Add: ok = true; return mk_const_float(x + y, res_ty);
    case Op::Sub: ok = true; return mk_const_float(x - y, res_ty);
    case Op::Mul: ok = true; return mk_const_float(x * y, res_ty);
    case Op::FDiv: ok = true; return mk_const_float(x / y, res_ty);
    case Op::FEq: ok = true; return mk_const_bool(x == y);
    case Op::FNe: ok = true; return mk_const_bool(x != y);
    case Op::FLt: ok = true; return mk_const_bool(x < y);
    case Op::FGt: ok = true; return mk_const_bool(x > y);
    case Op::FLe: ok = true; return mk_const_bool(x <= y);
    case Op::FGe: ok = true; return mk_const_bool(x >= y);
    default: return {};
    }
}

Operand fold_bool_binop(Op op, const Operand& a, const Operand& b, bool& ok) {
    ok = false;
    if (a.kind != Operand::Kind::ConstBool ||
        b.kind != Operand::Kind::ConstBool) return {};
    switch (op) {
    case Op::LAnd: ok = true; return mk_const_bool(a.cb && b.cb);
    case Op::LOr:  ok = true; return mk_const_bool(a.cb || b.cb);
    case Op::IEq:  ok = true; return mk_const_bool(a.cb == b.cb);
    case Op::INe:  ok = true; return mk_const_bool(a.cb != b.cb);
    default: return {};
    }
}

Operand fold_unary(Op op, const Operand& a, TypeId res_ty,
                   const sema::TypeInterner& ti, bool& ok) {
    ok = false;
    if (op == Op::LNot) {
        if (a.kind == Operand::Kind::ConstBool) {
            ok = true; return mk_const_bool(!a.cb);
        }
        return {};
    }
    if (op == Op::Neg) {
        if (a.kind == Operand::Kind::ConstInt) {
            uint32_t bw = ti.bit_width(res_ty);
            bool sgn = ti.is_signed_int(res_ty);
            int64_t s = sgn ? sext_to_i64(a.cu, bw)
                            : static_cast<int64_t>(mask_to_width(a.cu, bw));
            uint64_t r = mask_to_width(static_cast<uint64_t>(-s), bw);
            int64_t  rs = sgn ? sext_to_i64(r, bw) : static_cast<int64_t>(r);
            ok = true;
            return mk_const_int(rs, r, res_ty);
        }
        if (a.kind == Operand::Kind::ConstFloat) {
            ok = true;
            return mk_const_float(-a.cf, res_ty);
        }
    }
    return {};
}

Operand fold_cast(CastKind ck, const Operand& a, TypeId to_ty,
                  const sema::TypeInterner& ti, bool& ok) {
    ok = false;
    auto to_int = [&](uint64_t u) {
        uint32_t bw = ti.bit_width(to_ty);
        bool sgn = ti.is_signed_int(to_ty);
        uint64_t mu = mask_to_width(u, bw);
        int64_t si = sgn ? sext_to_i64(mu, bw) : static_cast<int64_t>(mu);
        return mk_const_int(si, mu, to_ty);
    };
    switch (ck) {
    case CastKind::SExt:
    case CastKind::ZExt:
    case CastKind::Trunc:
    case CastKind::Bitcast:
        if (a.kind == Operand::Kind::ConstInt) {
            ok = true;
            return to_int(a.cu);
        }
        if (a.kind == Operand::Kind::ConstBool && ck == CastKind::Bitcast) {
            ok = true; return to_int(a.cb ? 1 : 0);
        }
        return {};
    case CastKind::BoolToInt:
        if (a.kind == Operand::Kind::ConstBool) {
            ok = true; return to_int(a.cb ? 1 : 0);
        }
        return {};
    case CastKind::IntToBool:
        if (a.kind == Operand::Kind::ConstInt) {
            ok = true; return mk_const_bool(a.cu != 0);
        }
        return {};
    case CastKind::FPExt:
    case CastKind::FPTrunc:
        if (a.kind == Operand::Kind::ConstFloat) {
            ok = true; return mk_const_float(a.cf, to_ty);
        }
        return {};
    case CastKind::SIToFP:
        if (a.kind == Operand::Kind::ConstInt) {
            ok = true;
            // знаковое значение
            return mk_const_float(static_cast<double>(a.ci), to_ty);
        }
        return {};
    case CastKind::UIToFP:
        if (a.kind == Operand::Kind::ConstInt) {
            ok = true;
            return mk_const_float(static_cast<double>(a.cu), to_ty);
        }
        return {};
    case CastKind::FPToSI:
        if (a.kind == Operand::Kind::ConstFloat) {
            ok = true;
            int64_t v = static_cast<int64_t>(a.cf);
            return mk_const_int(v, static_cast<uint64_t>(v), to_ty);
        }
        return {};
    case CastKind::FPToUI:
        if (a.kind == Operand::Kind::ConstFloat) {
            ok = true;
            uint64_t u = static_cast<uint64_t>(a.cf);
            return mk_const_int(static_cast<int64_t>(u), u, to_ty);
        }
        return {};
    }
    return {};
}

bool fold_function(Function& fn, const sema::TypeInterner& ti) {
    bool changed_any = false;
    bool changed = true;
    while (changed) {
        changed = false;
        std::unordered_map<uint32_t, Operand> const_map;

        for (auto& bb : fn.blocks) {
            for (size_t i = 0; i < bb.insts.size(); ) {
                Inst& I = bb.insts[i];

                // rewrite-операнды через карту: temp -> const
                for (auto& a : I.args) {
                    if (a.kind == Operand::Kind::Temp) {
                        auto it = const_map.find(a.temp_id);
                        if (it != const_map.end()) {
                            a = it->second;
                            changed = true;
                        }
                    }
                }

                bool   ok  = false;
                Operand v;
                TypeId opty = I.type;
                TypeId rty  = !I.result.is_none() ? I.result.type : I.type;

                switch (I.op) {
                case Op::Add: case Op::Sub: case Op::Mul:
                case Op::SDiv: case Op::UDiv: case Op::SRem: case Op::URem:
                case Op::IEq: case Op::INe:
                case Op::SLt: case Op::SGt: case Op::SLe: case Op::SGe:
                case Op::ULt: case Op::UGt: case Op::ULe: case Op::UGe: {
                    if (I.args.size() == 2) {
                        // bool eq/ne для bool-операндов уходят сюда же
                        if (I.args[0].kind == Operand::Kind::ConstBool &&
                            I.args[1].kind == Operand::Kind::ConstBool) {
                            v = fold_bool_binop(I.op, I.args[0], I.args[1], ok);
                        } else {
                            v = fold_int_binop(I.op, I.args[0], I.args[1],
                                               opty, rty, ti, ok);
                        }
                    }
                    break;
                }
                case Op::FDiv:
                case Op::FEq: case Op::FNe:
                case Op::FLt: case Op::FGt: case Op::FLe: case Op::FGe: {
                    if (I.args.size() == 2)
                        v = fold_float_binop(I.op, I.args[0], I.args[1], rty, ok);
                    break;
                }
                case Op::LAnd: case Op::LOr: {
                    if (I.args.size() == 2)
                        v = fold_bool_binop(I.op, I.args[0], I.args[1], ok);
                    break;
                }
                case Op::LNot: case Op::Neg: {
                    if (I.args.size() == 1)
                        v = fold_unary(I.op, I.args[0], rty, ti, ok);
                    break;
                }
                case Op::Cast: {
                    if (I.args.size() == 1)
                        v = fold_cast(I.cast_kind, I.args[0], rty, ti, ok);
                    break;
                }
                default: break;
                }

                if (ok && I.result.kind == Operand::Kind::Temp) {
                    const_map[I.result.temp_id] = v;
                    bb.insts.erase(bb.insts.begin() + static_cast<long>(i));
                    changed = true;
                    changed_any = true;
                    continue;
                }
                ++i;
            }
        }
    }
    return changed_any;
}

} // namespace

bool const_fold(Module& mod) {
    if (!mod.types) return false;
    bool changed = false;
    for (auto& fp : mod.functions)
        if (fold_function(*fp, *mod.types)) changed = true;
    return changed;
}

} // namespace mycc::ir::opt
