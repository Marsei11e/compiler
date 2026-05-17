/* АСТ -> IR */
#include "ir/_pod.h"

#include <cassert>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

import mycc.diag;
import mycc.lexer;
import mycc.parser;
import mycc.sema;

namespace mycc::ir {

using namespace ast;
using sema::TypeId;
using sema::TypeKind;
using sema::kInvalidTypeId;

namespace {

// Lowerer

class Lowerer {
public:
    Lowerer(sema::Sema& sema, diag::DiagnosticEngine& diag)
        : sema_(sema), diag_(diag) {}

    std::unique_ptr<Module> run(Program& prog) {
        auto mod = std::make_unique<Module>();
        mod->types = &sema_.types();
        mod_ = mod.get();
        for (auto& d : prog.decls) lower_top_decl(d.get(), /*ns_prefix=*/"");
        return mod;
    }

private:
    // типы
    TypeId tid(uint32_t idx) const { return TypeId{idx}; }
    TypeId tid_of(const Expr* e)   const { return tid(e->resolved_type_id); }
    bool   is_signed_int(TypeId t) const { return sema_.types().is_signed_int(t); }
    bool   is_unsigned_int(TypeId t) const { return sema_.types().is_unsigned_int(t); }
    bool   is_float(TypeId t)      const { return sema_.types().is_float(t); }
    bool   is_numeric(TypeId t)    const { return sema_.types().is_numeric(t); }

    // эмиссия
    BasicBlock& cur_block() { return fn_->blocks.back(); }
    void emit(Inst i)       { cur_block().insts.push_back(std::move(i)); }

    Operand new_temp(TypeId t) {
        return temp(next_temp_id_++, t);
    }
    std::string fresh_label(std::string prefix) {
        return prefix + "." + std::to_string(next_label_id_++);
    }
    void start_block(const std::string& lbl) {
        BasicBlock bb; bb.label = lbl;
        fn_->blocks.push_back(std::move(bb));
    }
    bool block_terminated() const {
        if (fn_->blocks.empty()) return false;
        const auto& bb = fn_->blocks.back();
        if (bb.insts.empty()) return false;
        Op op = bb.insts.back().op;
        return op == Op::Ret || op == Op::Jmp || op == Op::Br;
    }

    // верхний уровень

    void lower_top_decl(Decl* d, const std::string& ns_prefix) {
        switch (d->kind) {
        case NodeKind::FnDecl:
            lower_fn(ast_cast<FnDecl>(d), ns_prefix, /*self_ty=*/kInvalidTypeId);
            break;
        case NodeKind::ImplBlock: {
            auto* impl = ast_cast<ImplBlock>(d);
            // ищем тип структуры по имени, чтобы методы знали тип self
            TypeId self_ty = kInvalidTypeId;
            if (auto* ent = sema_.global_scope().lookup(impl->type_name))
                if (auto* ss = std::get_if<sema::StructSymbol>(ent))
                    self_ty = ss->type_id;
            std::string prefix = ns_prefix + impl->type_name + "::";
            for (auto& m : impl->methods)
                lower_fn(const_cast<FnDecl*>(&m), prefix, self_ty);
            break;
        }
        case NodeKind::NamespaceDecl: {
            auto* ns = ast_cast<NamespaceDecl>(d);
            std::string prefix = ns_prefix + ns->name + "::";
            for (auto& nd : ns->decls)
                lower_top_decl(nd.get(), prefix);
            break;
        }
        case NodeKind::VarDecl:
        case NodeKind::ConstDecl: {
            const std::string& name =
                d->kind == NodeKind::VarDecl
                    ? ast_cast<VarDecl>(d)->name
                    : ast_cast<ConstDecl>(d)->name;
            if (auto* ent = sema_.global_scope().lookup(name))
                if (auto* vs = std::get_if<sema::VarSymbol>(ent)) {
                    GlobalVar gv;
                    gv.name = ns_prefix + name;
                    gv.type = vs->type;
                    gv.is_const = vs->is_const;
                    mod_->globals.push_back(std::move(gv));
                }
            break;
        }
        default: break; // StructDecl, TypeAliasDecl: только тип, без IR
        }
    }

    void lower_fn(FnDecl* fn, const std::string& prefix, TypeId self_ty) {
        if (!fn->body) return;

        auto func = std::make_unique<Function>();
        func->source_name = prefix + fn->name;
        func->is_main     = (fn->name == "main" && prefix.empty());
        func->loc         = fn->loc;
        func->return_ty   = fn->return_type
            ? sema_for_type(fn->return_type.get())
            : tid(static_cast<uint32_t>(TypeKind::Hollow));

        // сбрасываем состояние функции
        fn_ = func.get();
        named_slots_.clear();
        defer_scopes_.clear();
        loop_stack_.clear();
        next_temp_id_  = 0;
        next_label_id_= 0;
        next_defer_id_ = 0;

        for (auto& p : fn->params) {
            FnParam fp;
            fp.name = p.name;
            fp.type = p.is_self ? self_ty : sema_for_type(p.type.get());
            func->params.push_back(std::move(fp));
        }

        start_block("entry");

        // выделяем слоты под параметры и сразу сохраняем в них
        for (auto& p : func->params) {
            Inst alloc;
            alloc.op = Op::Alloca;
            alloc.type = p.type;
            alloc.result = named(p.name + ".slot", p.type);
            alloc.loc = fn->loc;
            emit(std::move(alloc));

            Inst st;
            st.op = Op::Store;
            st.type = p.type;
            st.args.push_back(named(p.name, p.type)); // параметр в SSA-стиле
            st.args.push_back(named(p.name + ".slot", p.type));
            st.loc = fn->loc;
            emit(std::move(st));

            named_slots_[p.name] = named(p.name + ".slot", p.type);
        }

        defer_scopes_.emplace_back();    // function-body scope

        // тело функции всегда BlockExpr (инвариант парсера B.5)
        if (fn->body && fn->body->kind == NodeKind::BlockExpr) {
            auto* be = ast_cast<BlockExpr>(fn->body.get());
            for (auto& s : be->stmts) lower_stmt(s.get());
            if (be->final_expr) lower_expr(be->final_expr.get());
        }

        // если последний блок не имеет терминатора - добавляем
        if (!block_terminated()) {
            // выдаем отложенные для скопа тела функции
            emit_defer_scope_(defer_scopes_.size() - 1);
            Inst ret;
            ret.op  = Op::Ret;
            ret.loc = fn->loc;
            // полый возврат - без значения; не-полый был бы ошибкой семантики,
            // cflow-check гарантирует return; Ret всё равно нужен чтобы закрыть IR
            emit(std::move(ret));
        }
        defer_scopes_.pop_back();

        mod_->functions.push_back(std::move(func));
        fn_ = nullptr;
    }

    TypeId sema_for_type(const TypeNode* node) {
        // нужен TypeId без повторного прохода через Sema::pass1;
        //делаем локальное разрешение, достаточное для  возвращаемых типов и параметров (уже провалидированы)
        if (!node) return kInvalidTypeId;
        switch (node->kind) {
        case NodeKind::BuiltinTypeRef: {
            auto* b = static_cast<const BuiltinTypeRef*>(node);
            return builtin_token_to_type(b->builtin);
        }
        case NodeKind::ArrayTypeRef: {
            auto* a = static_cast<const ArrayTypeRef*>(node);
            return sema_.types().intern_array(sema_for_type(a->elem_type.get()),
                                              a->size);
        }
        case NodeKind::RangeTypeRef: {
            auto* r = static_cast<const RangeTypeRef*>(node);
            return sema_.types().intern_range(sema_for_type(r->elem_type.get()));
        }
        case NodeKind::NamedTypeRef: {
            auto* n = static_cast<const NamedTypeRef*>(node);
            if (auto* ent = sema_.global_scope().lookup(n->name))
                if (auto* ss = std::get_if<sema::StructSymbol>(ent))
                    return ss->type_id;
            return kInvalidTypeId;
        }
        case NodeKind::NamespacedTypeRef: {
            auto* nt = static_cast<const NamespacedTypeRef*>(node);
            if (auto* ent = sema_.global_scope().lookup(nt->ns))
                if (auto* ns_sym = std::get_if<sema::NamespaceSymbol>(ent))
                    if (ns_sym->scope)
                        if (auto* mem = ns_sym->scope->lookup_local(nt->name))
                            if (auto* ss = std::get_if<sema::StructSymbol>(mem))
                                return ss->type_id;
            return kInvalidTypeId;
        }
        default: return kInvalidTypeId;
        }
    }

    static TypeId builtin_token_to_type(lex::TokenKind k) {
        using TK = lex::TokenKind;
        switch (k) {
        case TK::Int8:    return TypeId{static_cast<uint32_t>(TypeKind::I8)};
        case TK::Int16:   return TypeId{static_cast<uint32_t>(TypeKind::I16)};
        case TK::Int32:   return TypeId{static_cast<uint32_t>(TypeKind::I32)};
        case TK::Int64:   return TypeId{static_cast<uint32_t>(TypeKind::I64)};
        case TK::Uint8:   return TypeId{static_cast<uint32_t>(TypeKind::U8)};
        case TK::Uint16:  return TypeId{static_cast<uint32_t>(TypeKind::U16)};
        case TK::Uint32:  return TypeId{static_cast<uint32_t>(TypeKind::U32)};
        case TK::Uint64:  return TypeId{static_cast<uint32_t>(TypeKind::U64)};
        case TK::Float32: return TypeId{static_cast<uint32_t>(TypeKind::F32)};
        case TK::Float64: return TypeId{static_cast<uint32_t>(TypeKind::F64)};
        case TK::KwBool:  return TypeId{static_cast<uint32_t>(TypeKind::Bool)};
        case TK::KwString:return TypeId{static_cast<uint32_t>(TypeKind::String)};
        case TK::Hollow:  return TypeId{static_cast<uint32_t>(TypeKind::Hollow)};
        default:          return kInvalidTypeId;
        }
    }

    // defers

    // выдает DeferEmit со всеми defer-ами в скопах [from..top]
    // в LIFO-порядке (последний зарегистрированный выполняется первым)
    void emit_defer_emit_for_scopes(size_t from_scope) {
        std::vector<uint32_t> ids;
        for (size_t i = defer_scopes_.size(); i-- > from_scope; ) {
            for (auto it = defer_scopes_[i].rbegin();
                 it != defer_scopes_[i].rend(); ++it) {
                ids.push_back(*it);
            }
        }
        if (ids.empty()) return;
        Inst e;
        e.op  = Op::DeferEmit;
        e.loc = {};
        // по одному операнду на каждый defer-id
        for (uint32_t id : ids) {
            Operand o;
            o.kind = Operand::Kind::ConstInt;
            o.cu = id; o.ci = static_cast<int64_t>(id);
            o.type = tid(static_cast<uint32_t>(TypeKind::I32));
            e.args.push_back(std::move(o));
        }
        emit(std::move(e));
    }

    void emit_defer_scope_(size_t scope_idx) {
        emit_defer_emit_for_scopes(scope_idx);
    }

    // инструкции

    void lower_stmt(Stmt* s) {
        if (!s) return;
        switch (s->kind) {
        case NodeKind::DeclStmt:    lower_decl_stmt(ast_cast<DeclStmt>(s));       break;
        case NodeKind::AssignStmt:  lower_assign(ast_cast<AssignStmt>(s));        break;
        case NodeKind::ExprStmt:    lower_expr(ast_cast<ExprStmt>(s)->expr.get()); break;
        case NodeKind::IfStmt:      lower_if_stmt(ast_cast<IfStmt>(s));           break;
        case NodeKind::WhileStmt:   lower_while(ast_cast<WhileStmt>(s));          break;
        case NodeKind::ForStmt:     lower_for(ast_cast<ForStmt>(s));              break;
        case NodeKind::ReturnStmt:  lower_return(ast_cast<ReturnStmt>(s));        break;
        case NodeKind::BreakStmt:   lower_break(ast_cast<BreakStmt>(s));          break;
        case NodeKind::ContinueStmt:lower_continue(ast_cast<ContinueStmt>(s));    break;
        case NodeKind::DeferStmt:   lower_defer(ast_cast<DeferStmt>(s));          break;
        case NodeKind::BlockStmt:   lower_expr(ast_cast<BlockStmt>(s)->block.get()); break;
        case NodeKind::EmptyStmt:   break;
        default: break;
        }
    }

    void lower_decl_stmt(DeclStmt* ds) {
        if (ds->decl->kind == NodeKind::VarDecl) {
            auto* vd = ast_cast<VarDecl>(ds->decl.get());
            TypeId t = vd->init ? tid_of(vd->init.get()) : kInvalidTypeId;
            // слот alloca
            Inst a;
            a.op = Op::Alloca;
            a.type = t;
            a.result = named(vd->name + ".slot", t);
            a.loc = vd->loc;
            emit(std::move(a));
            named_slots_[vd->name] = named(vd->name + ".slot", t);
            // сохраняем инициализатор
            if (vd->init) {
                Operand v = lower_expr(vd->init.get());
                Inst st;
                st.op = Op::Store;
                st.type = t;
                st.args.push_back(std::move(v));
                st.args.push_back(named(vd->name + ".slot", t));
                st.loc = vd->loc;
                emit(std::move(st));
            }
        } else if (ds->decl->kind == NodeKind::ConstDecl) {
            auto* cd = ast_cast<ConstDecl>(ds->decl.get());
            TypeId t = cd->init ? tid_of(cd->init.get()) : kInvalidTypeId;
            Inst a;
            a.op = Op::Alloca;
            a.type = t;
            a.result = named(cd->name + ".slot", t);
            a.loc = cd->loc;
            emit(std::move(a));
            named_slots_[cd->name] = named(cd->name + ".slot", t);
            if (cd->init) {
                Operand v = lower_expr(cd->init.get());
                Inst st;
                st.op = Op::Store;
                st.type = t;
                st.args.push_back(std::move(v));
                st.args.push_back(named(cd->name + ".slot", t));
                st.loc = cd->loc;
                emit(std::move(st));
            }
        }
    }

    void lower_assign(AssignStmt* as) {
        // сначала вычисляем значение
        Operand val = lower_expr(as->value.get());
        TypeId  vt  = tid_of(as->value.get());

        // определяем форму цели
        Expr* tgt = as->target.get();
        switch (tgt->kind) {
        case NodeKind::IdentExpr: {
            auto* ie = ast_cast<IdentExpr>(tgt);
            auto it = named_slots_.find(ie->name);
            if (it == named_slots_.end()) {
                // глобальная переменная верхнего уровня - Store в глобал
                Inst st;
                st.op = Op::Store;
                st.type = vt;
                st.args.push_back(std::move(val));
                st.args.push_back(global(ie->name, vt));
                st.loc = as->loc;
                emit(std::move(st));
            } else {
                Inst st;
                st.op = Op::Store;
                st.type = vt;
                st.args.push_back(std::move(val));
                st.args.push_back(it->second);
                st.loc = as->loc;
                emit(std::move(st));
            }
            break;
        }
        case NodeKind::FieldAccess: {
            // вычисляем адрес поля, потом сохраняем
            auto* fa = ast_cast<FieldAccess>(tgt);
            Operand recv_ptr = lower_lvalue(fa->receiver.get());
            uint32_t fi = struct_field_index(tid_of(fa->receiver.get()),  fa->field_name);
            Operand fld = new_temp(vt);
            Inst gf;
            gf.op = Op::GetField;
            gf.type = vt;
            gf.result = fld;
            gf.field_index = fi;
            gf.args.push_back(std::move(recv_ptr));
            gf.loc = as->loc;
            emit(std::move(gf));

            Inst st;
            st.op = Op::Store;
            st.type = vt;
            st.args.push_back(std::move(val));
            st.args.push_back(fld);
            st.loc = as->loc;
            emit(std::move(st));
            break;
        }
        case NodeKind::IndexExpr: {
            auto* ix = ast_cast<IndexExpr>(tgt);
            Operand base = lower_lvalue(ix->base.get());
            Operand idx  = lower_expr(ix->index.get());
            Operand elem = new_temp(vt);
            Inst ge;
            ge.op = Op::GetElem;
            ge.type = vt;
            ge.result = elem;
            ge.args.push_back(std::move(base));
            ge.args.push_back(std::move(idx));
            ge.loc = as->loc;
            emit(std::move(ge));

            Inst st;
            st.op = Op::Store;
            st.type = vt;
            st.args.push_back(std::move(val));
            st.args.push_back(elem);
            st.loc = as->loc;
            emit(std::move(st));
            break;
        }
        default: break;
        }
    }

    // возвращает операнд-указатель на адрес lvalue
    Operand lower_lvalue(Expr* e) {
        switch (e->kind) {
        case NodeKind::IdentExpr: {
            auto* ie = ast_cast<IdentExpr>(e);
            auto it = named_slots_.find(ie->name);
            if (it != named_slots_.end()) return it->second;
            return global(ie->name, tid_of(e));
        }
        case NodeKind::FieldAccess: {
            auto* fa = ast_cast<FieldAccess>(e);
            Operand recv = lower_lvalue(fa->receiver.get());
            uint32_t fi = struct_field_index(tid_of(fa->receiver.get()), fa->field_name);
            Operand res = new_temp(tid_of(e));
            Inst gf;
            gf.op = Op::GetField;
            gf.type = tid_of(e);
            gf.result = res;
            gf.field_index = fi;
            gf.args.push_back(std::move(recv));
            gf.loc = e->loc;
            emit(std::move(gf));
            return res;
        }
        case NodeKind::IndexExpr: {
            auto* ix = ast_cast<IndexExpr>(e);
            Operand base = lower_lvalue(ix->base.get());
            Operand idx  = lower_expr(ix->index.get());
            Operand res = new_temp(tid_of(e));
            Inst ge;
            ge.op = Op::GetElem;
            ge.type = tid_of(e);
            ge.result = res;
            ge.args.push_back(std::move(base));
            ge.args.push_back(std::move(idx));
            ge.loc = e->loc;
            emit(std::move(ge));
            return res;
        }
        default:
            // не должно случиться для проверенного lvalue
            return none_op();
        }
    }

    uint32_t struct_field_index(TypeId t, const std::string& field) {
        const auto& td = sema_.types().get(t);
        if (td.kind != TypeKind::Struct || !td.struct_decl) return 0;
        const auto& fields = td.struct_decl->fields;
        for (uint32_t i = 0; i < fields.size(); ++i)
            if (fields[i].name == field) return i;
        return 0;
    }

    void lower_if_stmt(IfStmt* is) {
        std::string then_l = fresh_label("then");
        std::string else_l = fresh_label("else");
        std::string cont_l = fresh_label("ifcont");
        bool has_else = is->else_branch != nullptr;

        Operand cond = lower_expr(is->condition.get());
        Inst br;
        br.op = Op::Br;
        br.loc = is->loc;
        br.args.push_back(std::move(cond));
        br.then_label = then_l;
        br.else_label = has_else ? else_l : cont_l;
        emit(std::move(br));

        start_block(then_l);
        lower_expr(is->then_body.get());
        if (!block_terminated()) {
            Inst j; j.op = Op::Jmp; j.then_label = cont_l; j.loc = is->loc;
            emit(std::move(j));
        }

        if (has_else) {
            start_block(else_l);
            lower_stmt(is->else_branch.get());
            if (!block_terminated()) {
                Inst j; j.op = Op::Jmp; j.then_label = cont_l; j.loc = is->loc;
                emit(std::move(j));
            }
        }

        start_block(cont_l);
    }

    void lower_while(WhileStmt* ws) {
        std::string cond_l = fresh_label("while.cond");
        std::string body_l = fresh_label("while.body");
        std::string end_l  = fresh_label("while.end");

        Inst j; j.op = Op::Jmp; j.then_label = cond_l; j.loc = ws->loc;
        emit(std::move(j));

        start_block(cond_l);
        Operand cond = lower_expr(ws->condition.get());
        Inst br;
        br.op = Op::Br;
        br.loc = ws->loc;
        br.args.push_back(std::move(cond));
        br.then_label = body_l;
        br.else_label = end_l;
        emit(std::move(br));

        start_block(body_l);
        loop_stack_.push_back({cond_l, end_l, defer_scopes_.size()});
        lower_expr(ws->body.get());
        loop_stack_.pop_back();
        if (!block_terminated()) {
            Inst jb; jb.op = Op::Jmp; jb.then_label = cond_l; jb.loc = ws->loc;
            emit(std::move(jb));
        }
        start_block(end_l);
    }

    void lower_for(ForStmt* fs) {
        // хранилище переменной цикла - .slot ниже.
        // для range: пара cur/end как в codegen.md §13.2
        TypeId iter_ty = tid_of(fs->range_expr.get());
        if (iter_ty == kInvalidTypeId) return;

        const auto& itd = sema_.types().get(iter_ty);
        if (itd.kind == TypeKind::Range) {
            lower_for_range(fs);
        } else if (itd.kind == TypeKind::Array) {
            lower_for_array(fs);
        }
    }

    void lower_for_range(ForStmt* fs) {
        TypeId range_ty = tid_of(fs->range_expr.get());
        TypeId elem_ty  = sema_.types().get(range_ty).elem;

        std::string cond_l = fresh_label("for.cond");
        std::string body_l = fresh_label("for.body");
        std::string end_l  = fresh_label("for.end");

        // материализуем диапазон в слоты cur/end
        Operand cur_slot = named("for.cur." + std::to_string(next_label_id_), elem_ty);
        Operand end_slot = named("for.end." + std::to_string(next_label_id_), elem_ty);
        next_label_id_++;
        {
            Inst a;
            a.op = Op::Alloca; a.type = elem_ty; a.result = cur_slot;
            a.loc = fs->loc; emit(std::move(a));
        }
        {
            Inst a;
            a.op = Op::Alloca; a.type = elem_ty; a.result = end_slot;
            a.loc = fs->loc; emit(std::move(a));
        }

        // если выражение диапазона - литерал from..to, берем границы напрямую;
        // иначе читаем cur/end через GetField из значения range
        if (fs->range_expr->kind == NodeKind::RangeExpr) {
            auto* re = ast_cast<RangeExpr>(fs->range_expr.get());
            Operand from = lower_expr(re->from.get());
            Operand to   = lower_expr(re->to.get());
            store_at(cur_slot, std::move(from), elem_ty, fs->loc);
            store_at(end_slot, std::move(to),   elem_ty, fs->loc);
        } else {
            // диапазон в некотором lvalue (напр. локальный range[i32]).
            // читаем два поля через GetField и копируем в слоты итерации
            Operand range_addr = lower_lvalue(fs->range_expr.get());
            Operand cur_field = new_temp(elem_ty);
            Inst gf1; gf1.op = Op::GetField; gf1.type = elem_ty;
            gf1.result = cur_field; gf1.field_index = 0;
            gf1.args.push_back(range_addr); gf1.loc = fs->loc;
            emit(std::move(gf1));
            Operand cv = new_temp(elem_ty);
            Inst l1; l1.op = Op::Load; l1.type = elem_ty;
            l1.result = cv; l1.args.push_back(cur_field); l1.loc = fs->loc;
            emit(std::move(l1));
            store_at(cur_slot, cv, elem_ty, fs->loc);

            Operand end_field = new_temp(elem_ty);
            Inst gf2; gf2.op = Op::GetField; gf2.type = elem_ty;
            gf2.result = end_field; gf2.field_index = 1;
            gf2.args.push_back(range_addr); gf2.loc = fs->loc;
            emit(std::move(gf2));
            Operand ev = new_temp(elem_ty);
            Inst l2; l2.op = Op::Load; l2.type = elem_ty;
            l2.result = ev; l2.args.push_back(end_field); l2.loc = fs->loc;
            emit(std::move(l2));
            store_at(end_slot, ev, elem_ty, fs->loc);
        }

        Inst j; j.op = Op::Jmp; j.then_label = cond_l; j.loc = fs->loc;
        emit(std::move(j));

        // условие: cur < end ?
        start_block(cond_l);
        Operand cur_v = load_from(cur_slot, elem_ty, fs->loc);
        Operand end_v = load_from(end_slot, elem_ty, fs->loc);
        Operand cmp = new_temp(tid(static_cast<uint32_t>(TypeKind::Bool)));
        Inst lt;
        lt.op   = is_signed_int(elem_ty) ? Op::SLt : Op::ULt;
        lt.type = elem_ty;
        lt.result = cmp;
        lt.args.push_back(cur_v); lt.args.push_back(end_v);
        lt.loc  = fs->loc;
        emit(std::move(lt));

        Inst br;
        br.op = Op::Br; br.loc = fs->loc;
        br.args.push_back(cmp);
        br.then_label = body_l; br.else_label = end_l;
        emit(std::move(br));

        // тело: привязываем переменную цикла к cur, выполняем тело, инкрементируем cur
        start_block(body_l);
        Operand var_slot = named(fs->var_name + ".slot", elem_ty);
        {
            Inst a; a.op = Op::Alloca; a.type = elem_ty; a.result = var_slot;
            a.loc = fs->var_loc; emit(std::move(a));
        }
        named_slots_[fs->var_name] = var_slot;

        Operand cur_v2 = load_from(cur_slot, elem_ty, fs->loc);
        store_at(var_slot, cur_v2, elem_ty, fs->loc);

        loop_stack_.push_back({cond_l, end_l, defer_scopes_.size()});
        lower_expr(fs->body.get());
        loop_stack_.pop_back();

        named_slots_.erase(fs->var_name);

        if (!block_terminated()) {
            // cur += 1
            Operand cur_v3 = load_from(cur_slot, elem_ty, fs->loc);
            Operand one = const_int(1, elem_ty);
            Operand inc = new_temp(elem_ty);
            Inst add;
            add.op = Op::Add; add.type = elem_ty;
            add.result = inc;
            add.args.push_back(cur_v3); add.args.push_back(one);
            add.loc = fs->loc;
            emit(std::move(add));
            store_at(cur_slot, inc, elem_ty, fs->loc);
            Inst jb; jb.op = Op::Jmp; jb.then_label = cond_l; jb.loc = fs->loc;
            emit(std::move(jb));
        }

        start_block(end_l);
    }

    void lower_for_array(ForStmt* fs) {
        TypeId arr_ty  = tid_of(fs->range_expr.get());
        TypeId elem_ty = sema_.types().get(arr_ty).elem;
        size_t n       = sema_.types().get(arr_ty).array_size;

        std::string cond_l = fresh_label("for.cond");
        std::string body_l = fresh_label("for.body");
        std::string end_l  = fresh_label("for.end");

        TypeId i64_ty = tid(static_cast<uint32_t>(TypeKind::I64));
        Operand idx_slot = named("for.idx." + std::to_string(next_label_id_++), i64_ty);
        {
            Inst a; a.op = Op::Alloca; a.type = i64_ty; a.result = idx_slot;
            a.loc = fs->loc; emit(std::move(a));
        }
        store_at(idx_slot, const_int(0, i64_ty), i64_ty, fs->loc);

        // материализуем операнд массива в слот чтобы взять адрес
        Operand arr_addr = lower_lvalue(fs->range_expr.get());

        Inst j; j.op = Op::Jmp; j.then_label = cond_l; j.loc = fs->loc;
        emit(std::move(j));

        start_block(cond_l);
        Operand i_v = load_from(idx_slot, i64_ty, fs->loc);
        Operand n_v = const_int(static_cast<int64_t>(n), i64_ty);
        Operand cmp = new_temp(tid(static_cast<uint32_t>(TypeKind::Bool)));
        {
            Inst lt;
            lt.op = Op::SLt; lt.type = i64_ty; lt.result = cmp;
            lt.args.push_back(i_v); lt.args.push_back(n_v);
            lt.loc = fs->loc; emit(std::move(lt));
        }
        Inst br;
        br.op = Op::Br; br.args.push_back(cmp);
        br.then_label = body_l; br.else_label = end_l; br.loc = fs->loc;
        emit(std::move(br));

        start_block(body_l);
        // адрес элемента: getelem array_slot, idx
        Operand elem_addr = new_temp(elem_ty);
        Inst ge; ge.op = Op::GetElem; ge.type = elem_ty; ge.result = elem_addr;
        ge.args.push_back(arr_addr);
        ge.args.push_back(load_from(idx_slot, i64_ty, fs->loc));
        ge.loc = fs->loc;
        emit(std::move(ge));
        Operand elem_v = new_temp(elem_ty);
        {
            Inst ld; ld.op = Op::Load; ld.type = elem_ty;
            ld.result = elem_v; ld.args.push_back(elem_addr); ld.loc = fs->loc;
            emit(std::move(ld));
        }
        Operand var_slot = named(fs->var_name + ".slot", elem_ty);
        {
            Inst a; a.op = Op::Alloca; a.type = elem_ty; a.result = var_slot;
            a.loc = fs->var_loc; emit(std::move(a));
        }
        named_slots_[fs->var_name] = var_slot;
        store_at(var_slot, elem_v, elem_ty, fs->loc);

        loop_stack_.push_back({cond_l, end_l, defer_scopes_.size()});
        lower_expr(fs->body.get());
        loop_stack_.pop_back();

        named_slots_.erase(fs->var_name);

        if (!block_terminated()) {
            Operand i_v2 = load_from(idx_slot, i64_ty, fs->loc);
            Operand inc = new_temp(i64_ty);
            Inst add; add.op = Op::Add; add.type = i64_ty;
            add.result = inc;
            add.args.push_back(i_v2); add.args.push_back(const_int(1, i64_ty));
            add.loc = fs->loc;
            emit(std::move(add));
            store_at(idx_slot, inc, i64_ty, fs->loc);
            Inst jb; jb.op = Op::Jmp; jb.then_label = cond_l; jb.loc = fs->loc;
            emit(std::move(jb));
        }
        start_block(end_l);
    }

    void lower_return(ReturnStmt* rs) {
        Operand v = none_op();
        TypeId  vt = kInvalidTypeId;
        if (rs->value) {
            v  = lower_expr(rs->value.get());
            vt = tid_of(rs->value.get());
        }
        // выдаем defers для всех активных скопов (LIFO по всему фрейму)
        emit_defer_emit_for_scopes(0);
        Inst ret;
        ret.op = Op::Ret;
        ret.type = vt;
        ret.loc = rs->loc;
        if (!v.is_none()) ret.args.push_back(std::move(v));
        emit(std::move(ret));
    }

    void lower_break(BreakStmt* bs) {
        if (loop_stack_.empty()) return;
        const auto& l = loop_stack_.back();
        emit_defer_emit_for_scopes(l.defer_depth_at_entry);
        Inst j; j.op = Op::Jmp; j.then_label = l.break_label; j.loc = bs->loc;
        emit(std::move(j));
    }

    void lower_continue(ContinueStmt* cs) {
        if (loop_stack_.empty()) return;
        const auto& l = loop_stack_.back();
        emit_defer_emit_for_scopes(l.defer_depth_at_entry);
        Inst j; j.op = Op::Jmp; j.then_label = l.continue_label; j.loc = cs->loc;
        emit(std::move(j));
    }

    void lower_defer(DeferStmt* ds) {
        // опускаем тело defer в новый базовый блок; записываем в таблицу
        // defer функции и добавляем id в активный defer-скоп. места
        // вызовов эмитируются как DeferEmit на выходах из скопов/петель/функций
        uint32_t id = next_defer_id_++;
        std::string body_l = fresh_label("defer.body." + std::to_string(id));

        // сохраняем текущую точку вставки
        size_t saved_block = fn_->blocks.size() - 1;
        // создаем тело defer в отдельном блоке, продолжим после
        start_block(body_l);
        // опускаем отложенные инструкции в этот блок
        lower_stmt(ds->body.get());
        // defer выполняется до конца - закрываем синтетическим Jmp на маркер продолжения (codegen заменит его при раскрытии DeferEmit)
        if (!block_terminated()) {
            Inst j; j.op = Op::Jmp; j.then_label = "defer.return." + std::to_string(id);
            j.loc = ds->loc;
            emit(std::move(j));
        }

        // возобновляем эмиссию в предыдущем блоке.
        // для этого добавляем блок-продолжение и делаем его текущим через start_block
        std::string resume_l = fresh_label("defer.resume");
        // блок с самой defer-инструкцией (saved_block) еще не закрыт;
        // нам нужно вставить Jmp из saved_block в resume_l, потом начать resume_l
        {
            Inst j; j.op = Op::Jmp; j.then_label = resume_l; j.loc = ds->loc;
            // вставляем в saved_block - он ещё не терминирован; просто добавляем Jmp
            auto& sb = fn_->blocks[saved_block];
            // sb может уже иметь инструкции и ещё не закрыт - добавляем Jmp
            sb.insts.push_back(std::move(j));
        }
        start_block(resume_l);

        // регистрируем defer
        DeferEntry de{id, body_l};
        fn_->defer_table.push_back(std::move(de));
        if (!defer_scopes_.empty())
            defer_scopes_.back().push_back(id);

        // эмитируем DeferPush чтобы codegen видел регистрацию в потоке
        Inst dp;
        dp.op = Op::DeferPush;
        dp.defer_id = id;
        dp.defer_body_label = body_l;
        dp.loc = ds->loc;
        emit(std::move(dp));
    }

    // выражения

    Operand lower_expr(Expr* e) {
        if (!e) return none_op();
        switch (e->kind) {
        case NodeKind::IntLit:    return lower_int_lit(ast_cast<IntLit>(e));
        case NodeKind::FloatLit:  return lower_float_lit(ast_cast<FloatLit>(e));
        case NodeKind::BoolLit:   return const_bool(ast_cast<BoolLit>(e)->value,
                                                    tid_of(e));
        case NodeKind::StringLit: return lower_string_lit(ast_cast<StringLit>(e));
        case NodeKind::IdentExpr: return lower_ident(ast_cast<IdentExpr>(e));
        case NodeKind::SelfExpr:  return lower_self(e);

        case NodeKind::FieldAccess: return lower_field_access(ast_cast<FieldAccess>(e));
        case NodeKind::IndexExpr:   return lower_index(ast_cast<IndexExpr>(e));

        case NodeKind::UnaryExpr:   return lower_unary(ast_cast<UnaryExpr>(e));
        case NodeKind::BinaryExpr:  return lower_binary(ast_cast<BinaryExpr>(e));
        case NodeKind::CastExpr:    return lower_cast(ast_cast<CastExpr>(e));

        case NodeKind::CallExpr:        return lower_call(ast_cast<CallExpr>(e));
        case NodeKind::MethodCallExpr:  return lower_method_call(ast_cast<MethodCallExpr>(e));

        case NodeKind::IfExpr:    return lower_if_expr(ast_cast<IfExpr>(e));
        case NodeKind::BlockExpr: return lower_block_expr(ast_cast<BlockExpr>(e));

        case NodeKind::ArrayLit:  return lower_array_lit(ast_cast<ArrayLit>(e));
        case NodeKind::StructLit: return lower_struct_lit(ast_cast<StructLit>(e));

        case NodeKind::RangeExpr: return lower_range_expr(ast_cast<RangeExpr>(e));

        default: return none_op();
        }
    }

    Operand lower_int_lit(IntLit* lit) {
        TypeId t = tid_of(lit);
        Operand o = const_uint(lit->data.value, t);
        if (is_signed_int(t)) o.ci = static_cast<int64_t>(lit->data.value);
        return o;
    }

    Operand lower_float_lit(FloatLit* lit) {
        return const_float(lit->data.value, tid_of(lit));
    }

    Operand lower_string_lit(StringLit* lit) {
        // интернируем строку в таблицу модуля; эмитируем StrLit - результат
        // %string (struct {ptr, i64})
        uint32_t id = static_cast<uint32_t>(mod_->strings.size());
        StringLiteral sl; sl.id = id; sl.value = lit->value;
        mod_->strings.push_back(std::move(sl));

        TypeId str_ty = tid_of(lit);
        Operand res = new_temp(str_ty);
        Inst i;
        i.op = Op::StrLit;
        i.type = str_ty;
        i.result = res;
        i.args.push_back(const_string(id, str_ty));
        i.loc = lit->loc;
        emit(std::move(i));
        return res;
    }

    Operand lower_ident(IdentExpr* ie) {
        TypeId t = tid_of(ie);
        auto it = named_slots_.find(ie->name);
        Operand addr = (it != named_slots_.end())
            ? it->second
            : global(ie->name, t);
        Operand res = new_temp(t);
        Inst ld; ld.op = Op::Load; ld.type = t;
        ld.result = res; ld.args.push_back(std::move(addr)); ld.loc = ie->loc;
        emit(std::move(ld));
        return res;
    }

    Operand lower_self(Expr* e) {
        TypeId t = tid_of(e);
        auto it = named_slots_.find("self");
        if (it == named_slots_.end()) return none_op();
        Operand res = new_temp(t);
        Inst ld; ld.op = Op::Load; ld.type = t;
        ld.result = res; ld.args.push_back(it->second); ld.loc = e->loc;
        emit(std::move(ld));
        return res;
    }

    Operand lower_field_access(FieldAccess* fa) {
        TypeId fty = tid_of(fa);
        Operand recv_addr = lower_lvalue(fa->receiver.get());
        uint32_t fi = struct_field_index(tid_of(fa->receiver.get()), fa->field_name);
        Operand fld = new_temp(fty);
        Inst gf; gf.op = Op::GetField; gf.type = fty; gf.result = fld;
        gf.field_index = fi; gf.args.push_back(recv_addr); gf.loc = fa->loc;
        emit(std::move(gf));
        Operand res = new_temp(fty);
        Inst ld; ld.op = Op::Load; ld.type = fty;
        ld.result = res; ld.args.push_back(fld); ld.loc = fa->loc;
        emit(std::move(ld));
        return res;
    }

    Operand lower_index(IndexExpr* ix) {
        TypeId ety = tid_of(ix);
        Operand base = lower_lvalue(ix->base.get());
        Operand idx  = lower_expr(ix->index.get());
        Operand elem = new_temp(ety);
        Inst ge; ge.op = Op::GetElem; ge.type = ety; ge.result = elem;
        ge.args.push_back(std::move(base));
        ge.args.push_back(std::move(idx));
        ge.loc = ix->loc;
        emit(std::move(ge));
        Operand res = new_temp(ety);
        Inst ld; ld.op = Op::Load; ld.type = ety;
        ld.result = res; ld.args.push_back(elem); ld.loc = ix->loc;
        emit(std::move(ld));
        return res;
    }

    Operand lower_unary(UnaryExpr* ue) {
        Operand v = lower_expr(ue->operand.get());
        TypeId t = tid_of(ue);
        Operand res = new_temp(t);
        Inst i;
        i.op   = (ue->op == UnaryOp::Not) ? Op::LNot : Op::Neg;
        i.type = t;
        i.result = res;
        i.args.push_back(std::move(v));
        i.loc = ue->loc;
        emit(std::move(i));
        return res;
    }

    Operand lower_binary(BinaryExpr* be) {
        // короткое замыкание && / || через поток управления, как в codegen §6.7
        if (be->op == BinaryOp::And || be->op == BinaryOp::Or) {
            return lower_short_circuit(be);
        }

        Operand l = lower_expr(be->left.get());
        Operand r = lower_expr(be->right.get());
        TypeId  lt = tid_of(be->left.get());
        TypeId  res_ty = tid_of(be);

        Op op;
        switch (be->op) {
        case BinaryOp::Add:
            op = is_float(lt) ? Op::Add : Op::Add; break; // тип-селектор через Inst.type
        case BinaryOp::Sub: op = Op::Sub; break;
        case BinaryOp::Mul: op = Op::Mul; break;
        case BinaryOp::Div:
            op = is_float(lt) ? Op::FDiv
               : is_signed_int(lt) ? Op::SDiv
               : Op::UDiv;
            break;
        case BinaryOp::Rem:
            op = is_signed_int(lt) ? Op::SRem : Op::URem;
            break;
        case BinaryOp::Eq:
            op = is_float(lt) ? Op::FEq : Op::IEq; break;
        case BinaryOp::Ne:
            op = is_float(lt) ? Op::FNe : Op::INe; break;
        case BinaryOp::Lt:
            op = is_float(lt) ? Op::FLt
               : is_signed_int(lt) ? Op::SLt : Op::ULt; break;
        case BinaryOp::Gt:
            op = is_float(lt) ? Op::FGt
               : is_signed_int(lt) ? Op::SGt : Op::UGt; break;
        case BinaryOp::Le:
            op = is_float(lt) ? Op::FLe
               : is_signed_int(lt) ? Op::SLe : Op::ULe; break;
        case BinaryOp::Ge:
            op = is_float(lt) ? Op::FGe
               : is_signed_int(lt) ? Op::SGe : Op::UGe; break;
        default: op = Op::Add; break; // And/Or уже обработаны выше
        }

        Operand res = new_temp(res_ty);
        Inst i;
        i.op = op;
        i.type = lt; // тип операнда; тип результата в `res`
        i.result = res;
        i.args.push_back(std::move(l));
        i.args.push_back(std::move(r));
        i.loc = be->loc;
        emit(std::move(i));
        return res;
    }

    Operand lower_short_circuit(BinaryExpr* be) {
        TypeId  bool_ty = tid(static_cast<uint32_t>(TypeKind::Bool));
        // result slot
        Operand slot = named("sc.tmp." + std::to_string(next_label_id_), bool_ty);
        next_label_id_++;
        {
            Inst a; a.op = Op::Alloca; a.type = bool_ty; a.result = slot;
            a.loc = be->loc; emit(std::move(a));
        }

        std::string rhs_l = fresh_label("sc.rhs");
        std::string end_l = fresh_label("sc.end");

        Operand lhs = lower_expr(be->left.get());
        store_at(slot, lhs, bool_ty, be->loc);

        Inst br;
        br.op = Op::Br; br.loc = be->loc;
        br.args.push_back(load_from(slot, bool_ty, be->loc));
        if (be->op == BinaryOp::And) {
            br.then_label = rhs_l;
            br.else_label = end_l;
        } else {
            br.then_label = end_l;
            br.else_label = rhs_l;
        }
        emit(std::move(br));

        start_block(rhs_l);
        Operand rhs = lower_expr(be->right.get());
        store_at(slot, rhs, bool_ty, be->loc);
        Inst j; j.op = Op::Jmp; j.then_label = end_l; j.loc = be->loc;
        emit(std::move(j));

        start_block(end_l);
        return load_from(slot, bool_ty, be->loc);
    }

    Operand lower_cast(CastExpr* ce) {
        Operand v = lower_expr(ce->operand.get());
        TypeId from_ty = tid_of(ce->operand.get());
        TypeId to_ty   = tid_of(ce);
        CastKind ck    = pick_cast_kind(from_ty, to_ty);
        Operand res = new_temp(to_ty);
        Inst i;
        i.op = Op::Cast;
        i.type = to_ty;
        i.cast_kind = ck;
        i.result = res;
        i.args.push_back(std::move(v));
        i.loc = ce->loc;
        emit(std::move(i));
        return res;
    }

    CastKind pick_cast_kind(TypeId from, TypeId to) const {
        const auto& fk = sema_.types().get(from).kind;
        const auto& tk = sema_.types().get(to).kind;
        bool fi_s = is_signed_int(from);
        bool fi_u = is_unsigned_int(from);
        bool ti_s = is_signed_int(to);
        bool ti_u = is_unsigned_int(to);
        bool ff   = is_float(from);
        bool tf   = is_float(to);

        if (fk == TypeKind::Bool && (ti_s || ti_u)) return CastKind::BoolToInt;
        if ((fi_s || fi_u) && tk == TypeKind::Bool) return CastKind::IntToBool;

        if (fi_s && (ti_s || ti_u)) {
            if (sema_.types().bit_width(from) < sema_.types().bit_width(to))
                return CastKind::SExt;
            if (sema_.types().bit_width(from) > sema_.types().bit_width(to))
                return CastKind::Trunc;
            return CastKind::Bitcast;
        }
        if (fi_u && (ti_s || ti_u)) {
            if (sema_.types().bit_width(from) < sema_.types().bit_width(to))
                return CastKind::ZExt;
            if (sema_.types().bit_width(from) > sema_.types().bit_width(to))
                return CastKind::Trunc;
            return CastKind::Bitcast;
        }
        if (ff && tf) {
            uint32_t fb = sema_.types().bit_width(from);
            uint32_t tb = sema_.types().bit_width(to);
            if (fb < tb) return CastKind::FPExt;
            if (fb > tb) return CastKind::FPTrunc;
            return CastKind::Bitcast;
        }
        if (fi_s && tf) return CastKind::SIToFP;
        if (fi_u && tf) return CastKind::UIToFP;
        if (ff && ti_s) return CastKind::FPToSI;
        if (ff && ti_u) return CastKind::FPToUI;
        return CastKind::Bitcast;
    }

    Operand lower_call(CallExpr* ce) {
        std::vector<Operand> args;
        args.reserve(ce->args.size());
        for (auto& a : ce->args) args.push_back(lower_expr(a.get()));

        std::string callee;
        CallKind kind = CallKind::User;
        if (ce->callee->kind == NodeKind::IdentExpr) {
            callee = ast_cast<IdentExpr>(ce->callee.get())->name;
            if (is_builtin_callee(callee)) kind = CallKind::Builtin;
        } else if (ce->callee->kind == NodeKind::NamespaceAccess) {
            auto* na = ast_cast<NamespaceAccess>(ce->callee.get());
            callee = na->scope + "::" + na->member;
        } else {
            callee = "?call?";
        }

        TypeId ret_ty = tid_of(ce);
        Operand res;
        Inst i;
        i.op = Op::Call;
        i.type = ret_ty;
        i.call_kind = kind;
        i.callee = callee;
        i.loc = ce->loc;
        i.args = std::move(args);
        if (sema_.types().get(ret_ty).kind != TypeKind::Hollow) {
            res = new_temp(ret_ty);
            i.result = res;
        }
        emit(std::move(i));
        return res;
    }

    static bool is_builtin_callee(const std::string& name) {
        return name == "print"   || name == "println" ||
               name == "input"   || name == "exit"    ||
               name == "panic"   || name == "len";
    }

    Operand lower_method_call(MethodCallExpr* mc) {
        std::vector<Operand> args;
        args.reserve(mc->args.size() + 1);
        Operand recv = lower_expr(mc->receiver.get());
        args.push_back(std::move(recv));
        for (auto& a : mc->args) args.push_back(lower_expr(a.get()));

        TypeId recv_ty = tid_of(mc->receiver.get());
        std::string callee =
            std::string(sema_.types().display_name(recv_ty)) + "::" + mc->method_name;

        TypeId ret_ty = tid_of(mc);
        Operand res;
        Inst i;
        i.op = Op::Call;
        i.type = ret_ty;
        i.call_kind = CallKind::Method;
        i.callee = callee;
        i.loc = mc->loc;
        i.args = std::move(args);
        if (sema_.types().get(ret_ty).kind != TypeKind::Hollow) {
            res = new_temp(ret_ty);
            i.result = res;
        }
        emit(std::move(i));
        return res;
    }

    Operand lower_if_expr(IfExpr* ie) {
        TypeId t = tid_of(ie);
        bool   has_value = sema_.types().get(t).kind != TypeKind::Hollow;

        std::string then_l = fresh_label("ifexpr.then");
        std::string else_l = fresh_label("ifexpr.else");
        std::string cont_l = fresh_label("ifexpr.cont");

        Operand slot;
        if (has_value) {
            slot = named("ifexpr.tmp." + std::to_string(next_label_id_), t);
            next_label_id_++;
            Inst a; a.op = Op::Alloca; a.type = t; a.result = slot;
            a.loc = ie->loc; emit(std::move(a));
        }

        Operand cond = lower_expr(ie->condition.get());
        Inst br;
        br.op = Op::Br; br.loc = ie->loc;
        br.args.push_back(std::move(cond));
        br.then_label = then_l; br.else_label = else_l;
        emit(std::move(br));

        start_block(then_l);
        Operand tv = lower_expr(ie->then_body.get());
        if (has_value) store_at(slot, tv, t, ie->loc);
        if (!block_terminated()) {
            Inst j; j.op = Op::Jmp; j.then_label = cont_l; j.loc = ie->loc;
            emit(std::move(j));
        }

        start_block(else_l);
        Operand ev = lower_expr(ie->else_body.get());
        if (has_value) store_at(slot, ev, t, ie->loc);
        if (!block_terminated()) {
            Inst j; j.op = Op::Jmp; j.then_label = cont_l; j.loc = ie->loc;
            emit(std::move(j));
        }

        start_block(cont_l);
        if (!has_value) return none_op();
        return load_from(slot, t, ie->loc);
    }

    Operand lower_block_expr(BlockExpr* be) {
        auto saved_slots = named_slots_;
        defer_scopes_.emplace_back();
        for (auto& s : be->stmts) lower_stmt(s.get());
        Operand v = none_op();
        if (be->final_expr) v = lower_expr(be->final_expr.get());

        if (!block_terminated()) {
            emit_defer_scope_(defer_scopes_.size() - 1);
        }
        defer_scopes_.pop_back();
        named_slots_ = std::move(saved_slots);
        return v;
    }

    Operand lower_array_lit(ArrayLit* al) {
        TypeId arr_ty = tid_of(al);
        TypeId elem_ty = sema_.types().get(arr_ty).elem;

        Operand slot = named("arr.lit." + std::to_string(next_label_id_), arr_ty);
        next_label_id_++;
        {
            Inst a; a.op = Op::Alloca; a.type = arr_ty; a.result = slot;
            a.loc = al->loc; emit(std::move(a));
        }
        for (size_t i = 0; i < al->elements.size(); ++i) {
            Operand v = lower_expr(al->elements[i].get());
            Operand addr = new_temp(elem_ty);
            Inst ge; ge.op = Op::GetElem; ge.type = elem_ty; ge.result = addr;
            ge.args.push_back(slot);
            ge.args.push_back(const_int(static_cast<int64_t>(i),
                                        tid(static_cast<uint32_t>(TypeKind::I64))));
            ge.loc = al->elements[i]->loc;
            emit(std::move(ge));
            Inst st; st.op = Op::Store; st.type = elem_ty;
            st.args.push_back(std::move(v)); st.args.push_back(addr);
            st.loc = al->elements[i]->loc;
            emit(std::move(st));
        }
        // загружаем агрегат как значение
        Operand res = new_temp(arr_ty);
        Inst ld; ld.op = Op::Load; ld.type = arr_ty;
        ld.result = res; ld.args.push_back(slot); ld.loc = al->loc;
        emit(std::move(ld));
        return res;
    }

    Operand lower_struct_lit(StructLit* sl) {
        TypeId st_ty = tid_of(sl);
        const auto& td = sema_.types().get(st_ty);
        Operand slot = named("struct.lit." + std::to_string(next_label_id_), st_ty);
        next_label_id_++;
        {
            Inst a; a.op = Op::Alloca; a.type = st_ty; a.result = slot;
            a.loc = sl->loc; emit(std::move(a));
        }
        if (td.struct_decl) {
            for (auto& lf : sl->fields) {
                uint32_t fi = 0;
                TypeId field_ty = kInvalidTypeId;
                for (uint32_t i = 0; i < td.struct_decl->fields.size(); ++i) {
                    if (td.struct_decl->fields[i].name == lf.name) {
                        fi = i;
                        // TypeId поля не разрешен здесь; берем из значения
                        field_ty = tid_of(lf.value.get());
                        break;
                    }
                }
                Operand v = lower_expr(lf.value.get());
                Operand addr = new_temp(field_ty);
                Inst gf; gf.op = Op::GetField; gf.type = field_ty;
                gf.result = addr; gf.field_index = fi;
                gf.args.push_back(slot); gf.loc = lf.value->loc;
                emit(std::move(gf));
                Inst stI; stI.op = Op::Store; stI.type = field_ty;
                stI.args.push_back(std::move(v)); stI.args.push_back(addr);
                stI.loc = lf.value->loc;
                emit(std::move(stI));
            }
        }
        Operand res = new_temp(st_ty);
        Inst ld; ld.op = Op::Load; ld.type = st_ty;
        ld.result = res; ld.args.push_back(slot); ld.loc = sl->loc;
        emit(std::move(ld));
        return res;
    }

    Operand lower_range_expr(RangeExpr* re) {
        TypeId r_ty = tid_of(re);
        Operand from = lower_expr(re->from.get());
        Operand to   = lower_expr(re->to.get());
        Operand res = new_temp(r_ty);
        Inst rn;
        rn.op = Op::RangeNew;
        rn.type = r_ty;
        rn.result = res;
        rn.args.push_back(std::move(from));
        rn.args.push_back(std::move(to));
        rn.loc = re->loc;
        emit(std::move(rn));
        return res;
    }

    // мелкие вспомогалки

    Operand load_from(Operand slot, TypeId t, diag::SourceLocation loc) {
        Operand res = new_temp(t);
        Inst i; i.op = Op::Load; i.type = t;
        i.result = res; i.args.push_back(std::move(slot)); i.loc = loc;
        emit(std::move(i));
        return res;
    }

    void store_at(Operand slot, Operand val, TypeId t, diag::SourceLocation loc) {
        Inst i; i.op = Op::Store; i.type = t;
        i.args.push_back(std::move(val));
        i.args.push_back(std::move(slot));
        i.loc = loc;
        emit(std::move(i));
    }

    // состояние
    sema::Sema&  sema_;
    diag::DiagnosticEngine& diag_;
    Module* mod_{nullptr};
    Function* fn_{nullptr};

    std::unordered_map<std::string, Operand> named_slots_;
    uint32_t next_temp_id_{0};
    uint32_t next_label_id_{0};
    uint32_t next_defer_id_{0};

    std::vector<std::vector<uint32_t>> defer_scopes_;

    struct LoopFrame {
        std::string continue_label;
        std::string break_label;
        size_t      defer_depth_at_entry;
    };
    std::vector<LoopFrame> loop_stack_;
};

} // namespace

std::unique_ptr<Module> lower_program(ast::Program& prog,  sema::Sema& sema,  diag::DiagnosticEngine& diag) {
    Lowerer L(sema, diag);
    return L.run(prog);
}

} // namespace mycc::ir
