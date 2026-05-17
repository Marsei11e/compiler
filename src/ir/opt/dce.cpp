// dead code elimination: см. semantics §15.2
module;

#include <algorithm>
#include <cstdint>
#include <queue>
#include <unordered_set>
#include <vector>

module mycc.ir;

import mycc.diag;
import mycc.sema;

namespace mycc::ir::opt {

namespace {

bool is_pure_for_dce(Op op) {
    switch (op) {
    case Op::Add: case Op::Sub: case Op::Mul:
    case Op::SDiv: case Op::UDiv: case Op::FDiv:
    case Op::SRem: case Op::URem:
    case Op::Neg:
    case Op::IEq: case Op::INe:
    case Op::SLt: case Op::SGt: case Op::SLe: case Op::SGe:
    case Op::ULt: case Op::UGt: case Op::ULe: case Op::UGe:
    case Op::FEq: case Op::FNe:
    case Op::FLt: case Op::FGt: case Op::FLe: case Op::FGe:
    case Op::LAnd: case Op::LOr: case Op::LNot:
    case Op::Cast:
    case Op::Load:
    case Op::GetField: case Op::GetElem:
    case Op::StrLit:
        return true;
    default:
        return false;
    }
}

// Br с константным условием -> Jmp по выбранной ветке
// возвращает true, если что-то поменялось. 
bool simplify_branches(Function& fn) {
    bool changed = false;
    for (auto& bb : fn.blocks) {
        for (auto& I : bb.insts) {
            if (I.op != Op::Br || I.args.empty()) continue;
            const Operand& c = I.args[0];
            if (c.kind == Operand::Kind::ConstBool) {
                const std::string& tgt = c.cb ? I.then_label : I.else_label;
                I.op = Op::Jmp;
                I.then_label = tgt;
                I.else_label.clear();
                I.args.clear();
                changed = true;
            }
        }
    }
    return changed;
}

// удаляет инструкции после терминатора в каждом блоке (defensive).
bool drop_after_terminator(Function& fn) {
    bool changed = false;
    for (auto& bb : fn.blocks) {
        for (size_t i = 0; i < bb.insts.size(); ++i) {
            Op op = bb.insts[i].op;
            if (op == Op::Ret || op == Op::Jmp || op == Op::Br) {
                if (i + 1 < bb.insts.size()) {
                    bb.insts.erase(bb.insts.begin() + static_cast<long>(i + 1), bb.insts.end());
                    changed = true;
                }
                break;
            }
        }
    }
    return changed;
}

// множество достижимых меток. Старт - entry-блок + defer-body-блоки
bool prune_unreachable(Function& fn) {
    if (fn.blocks.empty()) return false;
    std::unordered_set<std::string> reachable;
    std::queue<std::string> work;

    auto push = [&](const std::string& lbl) {
        if (lbl.empty()) return;
        if (reachable.insert(lbl).second) work.push(lbl);
    };

    push(fn.blocks.front().label);
    for (const auto& de : fn.defer_table) push(de.body_label);

    // быстрый доступ к блоку по метке
    std::unordered_set<std::string> exists;
    for (const auto& bb : fn.blocks) exists.insert(bb.label);

    while (!work.empty()) {
        std::string lbl = work.front(); work.pop();
        // ищем блок с такой меткой
        const BasicBlock* bb = nullptr;
        for (const auto& b : fn.blocks)
            if (b.label == lbl) { bb = &b; break; }
        if (!bb) continue;
        if (bb->insts.empty()) continue;
        const Inst& term = bb->insts.back();
        if (term.op == Op::Jmp) {
            if (exists.count(term.then_label)) push(term.then_label);
        } else if (term.op == Op::Br) {
            if (exists.count(term.then_label)) push(term.then_label);
            if (exists.count(term.else_label)) push(term.else_label);
        }
        // Ret - нет наследников
    }

    size_t before = fn.blocks.size();
    fn.blocks.erase(
        std::remove_if(fn.blocks.begin(), fn.blocks.end(),
            [&](const BasicBlock& b) { return !reachable.count(b.label); }),
        fn.blocks.end());
    return fn.blocks.size() != before;
}

bool remove_unused_temps(Function& fn) {
    // собрать все used temp_ids
    std::unordered_set<uint32_t> used;
    for (const auto& bb : fn.blocks)
        for (const auto& I : bb.insts)
            for (const auto& a : I.args)
                if (a.kind == Operand::Kind::Temp)
                    used.insert(a.temp_id);

    bool changed = false;
    for (auto& bb : fn.blocks) {
        for (size_t i = 0; i < bb.insts.size(); ) {
            const Inst& I = bb.insts[i];
            if (is_pure_for_dce(I.op) &&
                I.result.kind == Operand::Kind::Temp &&
                used.find(I.result.temp_id) == used.end()) {
                bb.insts.erase(bb.insts.begin() + static_cast<long>(i));
                changed = true;
                continue;
            }
            ++i;
        }
    }
    return changed;
}

bool dce_function(Function& fn) {
    bool any_changed = false;
    bool changed = true;
    while (changed) {
        changed = false;
        if (simplify_branches(fn))    changed = true;
        if (drop_after_terminator(fn)) changed = true;
        if (prune_unreachable(fn))    changed = true;
        if (remove_unused_temps(fn))  changed = true;
        if (changed) any_changed = true;
    }
    return any_changed;
}

} // namespace

bool dce(Module& mod) {
    bool changed = false;
    for (auto& fp : mod.functions)
        if (dce_function(*fp)) changed = true;
    return changed;
}

void optimize_module(Module& mod) {
    // const_fold и dce взаимно открывают возможности друг другу
    // напр. свёртка false в условии br позволяет dce убрать ветку
    // а удалённая ветка раскрывает мёртвые вычисления
    bool changed = true;
    while (changed) {
        changed = false;
        if (const_fold(mod)) changed = true;
        if (dce(mod))        changed = true;
    }
}

} // namespace mycc::ir::opt
