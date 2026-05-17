/* проход валидации системы эффектов
для каждого аннотированного тела обходим АСТ и выдаем диагностику в каждом месте, чьи эффекты не входят в объявленный набор.
источники эффектов (§12.2):
целочисленные / и %  (любые signed/unsigned int)  -> @panics
индексирование массива a[i] -> @panics
вызов функции с аннотацией E ->E
вызов неаннотированной пользовательской функции -> неизвестно
встроенные (FnSymbol::decl == nullptr) всегда несут явные эффекты, никогда не считаются "неаннотированными"
 */
#include "sema/_pod.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

import mycc.diag;
import mycc.lexer;
import mycc.parser;

namespace mycc::sema {

using namespace ast;

namespace {

struct AllowSet {
    bool is_pure{false};
    bool io{false};
    bool panics{false};
};

class EffectWalker {
public:
    EffectWalker(Scope& outer,
                 std::string fn_name,
                 AllowSet allow,
                 const std::unordered_map<uint32_t, StructSymbol*>& struct_map,
                 const TypeInterner& types,
                 diag::DiagnosticEngine& diag)
        : outer_(outer), fn_name_(std::move(fn_name)), allow_(allow),
          struct_map_(struct_map), types_(types), diag_(diag) {}

    void walk_expr(Expr* e) {
        if (!e) return;
        switch (e->kind) {
        case NodeKind::BinaryExpr: {
            auto* be = ast_cast<BinaryExpr>(e);
            walk_expr(be->left.get());
            walk_expr(be->right.get());
            if (be->op == BinaryOp::Div || be->op == BinaryOp::Rem) {
                TypeId lt{be->left->resolved_type_id};
                if (lt != kInvalidTypeId &&
                    (types_.is_signed_int(lt) || types_.is_unsigned_int(lt))) {
                    report_int_division(e->loc);
                }
                // float / (и невозможный float %) - чистые по A.1
            }
            break;
        }
        case NodeKind::IndexExpr: {
            auto* ix = ast_cast<IndexExpr>(e);
            walk_expr(ix->base.get());
            walk_expr(ix->index.get());
            report_indexing(e->loc);
            break;
        }
        case NodeKind::CallExpr: {
            auto* ce = ast_cast<CallExpr>(e);
            for (auto& a : ce->args) walk_expr(a.get());
            check_call(ce);
            break;
        }
        case NodeKind::MethodCallExpr: {
            auto* mc = ast_cast<MethodCallExpr>(e);
            walk_expr(mc->receiver.get());
            for (auto& a : mc->args) walk_expr(a.get());
            check_method_call(mc);
            break;
        }
        case NodeKind::UnaryExpr:
            walk_expr(ast_cast<UnaryExpr>(e)->operand.get());
            break;
        case NodeKind::CastExpr:
            walk_expr(ast_cast<CastExpr>(e)->operand.get());
            break;
        case NodeKind::FieldAccess:
            walk_expr(ast_cast<FieldAccess>(e)->receiver.get());
            break;
        case NodeKind::RangeExpr: {
            auto* re = ast_cast<RangeExpr>(e);
            walk_expr(re->from.get());
            walk_expr(re->to.get());
            break;
        }
        case NodeKind::ArrayLit: {
            auto* al = ast_cast<ArrayLit>(e);
            for (auto& el : al->elements) walk_expr(el.get());
            break;
        }
        case NodeKind::StructLit: {
            auto* sl = ast_cast<StructLit>(e);
            for (auto& f : sl->fields) walk_expr(f.value.get());
            break;
        }
        case NodeKind::IfExpr: {
            auto* ie = ast_cast<IfExpr>(e);
            walk_expr(ie->condition.get());
            walk_expr(ie->then_body.get());
            walk_expr(ie->else_body.get());
            break;
        }
        case NodeKind::BlockExpr: {
            auto* b = ast_cast<BlockExpr>(e);
            for (auto& s : b->stmts) walk_stmt(s.get());
            walk_expr(b->final_expr.get());
            break;
        }
        default: break;
        }
    }

    void walk_stmt(Stmt* s) {
        if (!s) return;
        switch (s->kind) {
        case NodeKind::DeclStmt: {
            auto* ds = ast_cast<DeclStmt>(s);
            if (ds->decl->kind == NodeKind::VarDecl) {
                walk_expr(ast_cast<VarDecl>(ds->decl.get())->init.get());
            } else if (ds->decl->kind == NodeKind::ConstDecl) {
                walk_expr(ast_cast<ConstDecl>(ds->decl.get())->init.get());
            }
            break;
        }
        case NodeKind::AssignStmt: {
            auto* as = ast_cast<AssignStmt>(s);
            walk_expr(as->target.get());
            walk_expr(as->value.get());
            break;
        }
        case NodeKind::ExprStmt:
            walk_expr(ast_cast<ExprStmt>(s)->expr.get());
            break;
        case NodeKind::IfStmt: {
            auto* is_ = ast_cast<IfStmt>(s);
            walk_expr(is_->condition.get());
            walk_expr(is_->then_body.get());
            walk_stmt(is_->else_branch.get());
            break;
        }
        case NodeKind::WhileStmt: {
            auto* ws = ast_cast<WhileStmt>(s);
            walk_expr(ws->condition.get());
            walk_expr(ws->body.get());
            break;
        }
        case NodeKind::ForStmt: {
            auto* fs = ast_cast<ForStmt>(s);
            walk_expr(fs->range_expr.get());
            walk_expr(fs->body.get());
            break;
        }
        case NodeKind::ReturnStmt:
            walk_expr(ast_cast<ReturnStmt>(s)->value.get());
            break;
        case NodeKind::DeferStmt:
            walk_stmt(ast_cast<DeferStmt>(s)->body.get());
            break;
        case NodeKind::BlockStmt:
            walk_expr(ast_cast<BlockStmt>(s)->block.get());
            break;
        default: break;
        }
    }

private:
    Scope&                                              outer_;
    std::string                                         fn_name_;
    AllowSet                                            allow_;
    const std::unordered_map<uint32_t, StructSymbol*>&  struct_map_;
    const TypeInterner&                                 types_;
    diag::DiagnosticEngine&                             diag_;
    bool                                                unknown_warning_emitted_{false};

    void report_int_division(diag::SourceLocation loc) {
        if (allow_.is_pure) {
            diag_.report({diag::Severity::Error, loc,
                "@pure function '" + fn_name_ +
                "' cannot use integer division (carries @panics effect)"});
        } else if (!allow_.panics) {
            diag_.report({diag::Severity::Error, loc,
                "function '" + fn_name_ +
                "' uses integer division (@panics effect) but is not declared @panics"});
        }
    }

    void report_indexing(diag::SourceLocation loc) {
        if (allow_.is_pure) {
            diag_.report({diag::Severity::Error, loc,
                "@pure function '" + fn_name_ +
                "' cannot use array indexing (carries @panics effect)"});
        } else if (!allow_.panics) {
            diag_.report({diag::Severity::Error, loc,
                "function '" + fn_name_ +
                "' uses array indexing (@panics effect) but is not declared @panics"});
        }
    }

    void report_io_call(diag::SourceLocation loc, const std::string& callee) {
        if (allow_.is_pure) {
            diag_.report({diag::Severity::Error, loc,
                "@pure function '" + fn_name_ +
                "' cannot call '" + callee + "' (@io effect)"});
        } else if (!allow_.io) {
            diag_.report({diag::Severity::Error, loc,
                "function '" + fn_name_ + "' calls '" + callee +
                "' (@io effect) but is not declared @io"});
        }
    }

    void report_panics_call(diag::SourceLocation loc, const std::string& callee) {
        if (allow_.is_pure) {
            diag_.report({diag::Severity::Error, loc,
                "@pure function '" + fn_name_ +
                "' cannot call '" + callee + "' (@panics effect)"});
        } else if (!allow_.panics) {
            diag_.report({diag::Severity::Error, loc,
                "function '" + fn_name_ + "' calls '" + callee +
                "' (@panics effect) but is not declared @panics"});
        }
    }

    void report_unknown_call(diag::SourceLocation loc, const std::string& callee) {
        if (allow_.is_pure) {
            // @pure -> жесткая ошибка на каждом месте, без дросселирования (§12.3)
            diag_.report({diag::Severity::Error, loc,
                "@pure function '" + fn_name_ +
                "' cannot call unannotated function '" + callee + "'"});
            return;
        }
        // §12.5(б): не более одного предупреждения на объявление вызывающего
        if (unknown_warning_emitted_) return;
        unknown_warning_emitted_ = true;
        diag_.report({diag::Severity::Warning, loc,
            "function '" + fn_name_ + "' calls unannotated function '" +
            callee + "'; effect checking is incomplete"});
    }

    void apply_callee_effects(diag::SourceLocation loc,
                              const std::string& callee,
                              const FnSymbol* fn) {
        if (!fn) return;
        // встроенные (decl == nullptr) всегда несут явные эффекты, никогда не считаются неаннотированными даже с пустым набором
        if (fn->effects.empty() && fn->decl != nullptr) {
            report_unknown_call(loc, callee);
            return;
        }
        bool has_io = false;
        bool has_panics = false;
        for (auto e : fn->effects) {
            if (e == EffectKind::Io)     has_io     = true;
            if (e == EffectKind::Panics) has_panics = true;
        }
        if (has_io)     report_io_call(loc, callee);
        if (has_panics) report_panics_call(loc, callee);
    }

    void check_call(CallExpr* ce) {
        std::vector<TypeId>  arg_types;
        std::vector<ArgKind> arg_kinds;
        arg_types.reserve(ce->args.size());
        arg_kinds.reserve(ce->args.size());
        for (auto& a : ce->args) {
            arg_types.push_back(TypeId{a->resolved_type_id});
            arg_kinds.push_back(arg_kind_of(a.get()));
        }

        diag::SourceLocation call_loc = ce->callee->loc;
        std::string          call_name;
        const OverloadSet*   overloads = nullptr;
        OverloadSet          tmp;

        if (ce->callee->kind == NodeKind::IdentExpr) {
            auto* ie  = ast_cast<IdentExpr>(ce->callee.get());
            call_name = ie->name;
            auto* ent = outer_.lookup(ie->name);
            if (!ent) return;
            if (auto* os = std::get_if<OverloadSet>(ent)) overloads = os;
            else return;
        } else if (ce->callee->kind == NodeKind::NamespaceAccess) {
            auto* na  = ast_cast<NamespaceAccess>(ce->callee.get());
            call_name = na->scope + "::" + na->member;
            auto* ent = outer_.lookup(na->scope);
            if (!ent) return;
            if (auto* ns = std::get_if<NamespaceSymbol>(ent)) {
                if (!ns->scope) return;
                auto* fe = ns->scope->lookup(na->member);
                if (!fe) return;
                if (auto* os = std::get_if<OverloadSet>(fe)) overloads = os;
                else return;
            } else if (auto* ss = std::get_if<StructSymbol>(ent)) {
                tmp       = filter_by_name(ss->methods, na->member);
                overloads = &tmp;
            } else {
                return;
            }
        } else {
            return;
        }

        if (!overloads || overloads->overloads.empty()) return;

        auto res = resolve_call(*overloads, arg_types, arg_kinds, types_);
        if (res.status != OverloadStatus::Resolved) return;
        apply_callee_effects(call_loc, call_name, res.fn);
    }

    void check_method_call(MethodCallExpr* mc) {
        TypeId recv_ty{mc->receiver->resolved_type_id};
        if (recv_ty == kInvalidTypeId) return;
        if (types_.get(recv_ty).kind != TypeKind::Struct) return;

        auto it = struct_map_.find(recv_ty.index);
        if (it == struct_map_.end()) return;
        const StructSymbol* ss = it->second;

        OverloadSet candidates = filter_by_name(ss->methods, mc->method_name);
        if (candidates.overloads.empty()) return;

        std::vector<TypeId>  arg_types{recv_ty};
        std::vector<ArgKind> arg_kinds{ArgKind::Regular};
        for (auto& a : mc->args) {
            arg_types.push_back(TypeId{a->resolved_type_id});
            arg_kinds.push_back(arg_kind_of(a.get()));
        }
        auto res = resolve_call(candidates, arg_types, arg_kinds, types_);
        if (res.status != OverloadStatus::Resolved) return;

        apply_callee_effects(mc->loc, ss->name + "::" + mc->method_name, res.fn);
    }
};

void walk_fn(FnDecl* fn,
             Scope& outer,
             const std::unordered_map<uint32_t, StructSymbol*>& struct_map,
             const TypeInterner& types,
             diag::DiagnosticEngine& diag) {
    if (!fn->body) return;
    if (fn->effects.empty()) return; // без аннотации -> не проверяем (§12.5)

    AllowSet allow;
    for (auto e : fn->effects) {
        switch (e) {
        case EffectKind::Pure:   allow.is_pure = true; break;
        case EffectKind::Io:     allow.io      = true; break;
        case EffectKind::Panics: allow.panics  = true; break;
        }
    }

    EffectWalker w(outer, fn->name, allow, struct_map, types, diag);
    w.walk_expr(fn->body.get());
}

void walk_decls(const std::vector<DeclPtr>& decls,
                Scope& current,
                const std::unordered_map<uint32_t, StructSymbol*>& struct_map,
                const TypeInterner& types,
                diag::DiagnosticEngine& diag) {
    for (auto& d : decls) {
        switch (d->kind) {
        case NodeKind::FnDecl:
            walk_fn(ast_cast<FnDecl>(d.get()), current, struct_map, types, diag);
            break;
        case NodeKind::ImplBlock: {
            auto* impl = ast_cast<ImplBlock>(d.get());
            for (auto& m : impl->methods)
                walk_fn(&m, current, struct_map, types, diag);
            break;
        }
        case NodeKind::NamespaceDecl: {
            auto* ns  = ast_cast<NamespaceDecl>(d.get());
            auto* ent = current.lookup_local(ns->name);
            if (!ent) break;
            auto* ns_sym = std::get_if<NamespaceSymbol>(ent);
            if (!ns_sym || !ns_sym->scope) break;
            walk_decls(ns->decls, *ns_sym->scope, struct_map, types, diag);
            break;
        }
        default: break;
        }
    }
}

} // namespace

void check_effects(ast::Program& prog,
                   Scope& global_scope,
                   const std::unordered_map<uint32_t, StructSymbol*>& struct_type_map,
                   const TypeInterner& types,
                   diag::DiagnosticEngine& diag) {
    walk_decls(prog.decls, global_scope, struct_type_map, types, diag);
}

} // namespace mycc::sema
