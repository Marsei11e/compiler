/*проход проверки потока управления
один walker на тело функции. отслеживает:
- глубину вложенности циклов  -> break/continue должны быть внутри цикла
- глубину вложенности defer   -> тело не должно содержать return/break/continue/defer
 - завершаемость каждого пути  -> если тип возврата не hollow, не должно быть пути,  достигающего закрывающей скобки
 - завершаемость вычисляется снизу вверх. инструкция дивергирует если передает
-  управление из функции (return, panic, exit) или из блока (break, continue).
-  блок дивергирует если одна из инструкций дивергирует до конца.
 - if-инструкция дивергирует если обе ветки дивергируют (else обязателен для распространения)*/
#include "sema/cflow_check.h"

import mycc.diag;
import mycc.lexer;
import mycc.parser;

namespace mycc::sema {

using namespace ast;

namespace {

enum class Term { FallsThrough, Diverges };

bool fn_returns_hollow(const FnDecl* fn) {
    if (!fn->return_type) return true;
    if (fn->return_type->kind != NodeKind::BuiltinTypeRef) return false;
    return ast_cast<BuiltinTypeRef>(fn->return_type.get())->builtin
           == lex::TokenKind::Hollow;
}

class CFlowWalker {
public:
    CFlowWalker(diag::DiagnosticEngine& diag, bool requires_return)
        : diag_(diag), requires_return_(requires_return) {}

    void walk_fn_body(Expr* body) {
        if (!body) return;
        Term t = walk_expr_term(body);
        if (requires_return_ && t == Term::FallsThrough) {
            diag_.report({diag::Severity::Error, body->loc,
                          "missing return in non-hollow function"});
        }
    }

private:
    diag::DiagnosticEngine& diag_;
    bool requires_return_;
    int  loop_depth_{0};
    int  defer_depth_{0};

    bool in_defer() const { return defer_depth_ > 0; }

    static bool is_diverging_call(const CallExpr* ce) {
        if (ce->callee->kind != NodeKind::IdentExpr) return false;
        const auto& name = ast_cast<IdentExpr>(ce->callee.get())->name;
        return name == "panic" || name == "exit";
    }

    Term walk_block(BlockExpr* b) {
        Term result = Term::FallsThrough;
        for (size_t i = 0; i < b->stmts.size(); ++i) {
            Term t = walk_stmt(b->stmts[i].get());
            if (t == Term::Diverges) result = Term::Diverges;
        }
        if (b->final_expr) {
            Term t = walk_expr_term(b->final_expr.get());
            if (t == Term::Diverges) result = Term::Diverges;
        }
        return result;
    }

    Term walk_expr_term(Expr* e) {
        if (!e) return Term::FallsThrough;
        switch (e->kind) {
        case NodeKind::BlockExpr:
            return walk_block(ast_cast<BlockExpr>(e));
        case NodeKind::IfExpr: {
            auto* ie = ast_cast<IfExpr>(e);
            walk_expr_value(ie->condition.get());
            Term t1 = walk_expr_term(ie->then_body.get());
            Term t2 = ie->else_body ? walk_expr_term(ie->else_body.get())
                                    : Term::FallsThrough;
            return (t1 == Term::Diverges && t2 == Term::Diverges)
                   ? Term::Diverges : Term::FallsThrough;
        }
        case NodeKind::CallExpr: {
            auto* ce = ast_cast<CallExpr>(e);
            for (auto& a : ce->args) walk_expr_value(a.get());
            return is_diverging_call(ce) ? Term::Diverges : Term::FallsThrough;
        }
        default:
            walk_expr_value(e);
            return Term::FallsThrough;
        }
    }

    void walk_expr_value(Expr* e) {
        if (!e) return;
        switch (e->kind) {
        case NodeKind::BlockExpr: walk_block(ast_cast<BlockExpr>(e)); break;
        case NodeKind::IfExpr: {
            auto* ie = ast_cast<IfExpr>(e);
            walk_expr_value(ie->condition.get());
            walk_expr_value(ie->then_body.get());
            walk_expr_value(ie->else_body.get());
            break;
        }
        case NodeKind::CallExpr: {
            auto* ce = ast_cast<CallExpr>(e);
            for (auto& a : ce->args) walk_expr_value(a.get());
            break;
        }
        case NodeKind::MethodCallExpr: {
            auto* mc = ast_cast<MethodCallExpr>(e);
            walk_expr_value(mc->receiver.get());
            for (auto& a : mc->args) walk_expr_value(a.get());
            break;
        }
        case NodeKind::BinaryExpr: {
            auto* be = ast_cast<BinaryExpr>(e);
            walk_expr_value(be->left.get());
            walk_expr_value(be->right.get());
            break;
        }
        case NodeKind::UnaryExpr:
            walk_expr_value(ast_cast<UnaryExpr>(e)->operand.get()); break;
        case NodeKind::CastExpr:
            walk_expr_value(ast_cast<CastExpr>(e)->operand.get()); break;
        case NodeKind::FieldAccess:
            walk_expr_value(ast_cast<FieldAccess>(e)->receiver.get()); break;
        case NodeKind::IndexExpr: {
            auto* ix = ast_cast<IndexExpr>(e);
            walk_expr_value(ix->base.get());
            walk_expr_value(ix->index.get());
            break;
        }
        case NodeKind::ArrayLit: {
            auto* al = ast_cast<ArrayLit>(e);
            for (auto& el : al->elements) walk_expr_value(el.get());
            break;
        }
        case NodeKind::StructLit: {
            auto* sl = ast_cast<StructLit>(e);
            for (auto& f : sl->fields) walk_expr_value(f.value.get());
            break;
        }
        case NodeKind::RangeExpr: {
            auto* re = ast_cast<RangeExpr>(e);
            walk_expr_value(re->from.get());
            walk_expr_value(re->to.get());
            break;
        }
        default: break;
        }
    }

    Term walk_stmt(Stmt* s) {
        if (!s) return Term::FallsThrough;
        switch (s->kind) {
        case NodeKind::DeclStmt: {
            auto* ds = ast_cast<DeclStmt>(s);
            if (ds->decl->kind == NodeKind::VarDecl) {
                auto* vd = ast_cast<VarDecl>(ds->decl.get());
                if (vd->init) walk_expr_value(vd->init.get());
            } else if (ds->decl->kind == NodeKind::ConstDecl) {
                auto* cd = ast_cast<ConstDecl>(ds->decl.get());
                if (cd->init) walk_expr_value(cd->init.get());
            }
            return Term::FallsThrough;
        }
        case NodeKind::AssignStmt: {
            auto* as = ast_cast<AssignStmt>(s);
            walk_expr_value(as->target.get());
            walk_expr_value(as->value.get());
            return Term::FallsThrough;
        }
        case NodeKind::ExprStmt:
            return walk_expr_term(ast_cast<ExprStmt>(s)->expr.get());
        case NodeKind::IfStmt: {
            auto* is_ = ast_cast<IfStmt>(s);
            walk_expr_value(is_->condition.get());
            Term t1 = walk_expr_term(is_->then_body.get());
            Term t2 = is_->else_branch ? walk_stmt(is_->else_branch.get())
                                       : Term::FallsThrough;
            return (is_->else_branch &&
                    t1 == Term::Diverges && t2 == Term::Diverges)
                   ? Term::Diverges : Term::FallsThrough;
        }
        case NodeKind::WhileStmt: {
            auto* ws = ast_cast<WhileStmt>(s);
            walk_expr_value(ws->condition.get());
            ++loop_depth_;
            walk_expr_value(ws->body.get());
            --loop_depth_;
            return Term::FallsThrough;
        }
        case NodeKind::ForStmt: {
            auto* fs = ast_cast<ForStmt>(s);
            walk_expr_value(fs->range_expr.get());
            ++loop_depth_;
            walk_expr_value(fs->body.get());
            --loop_depth_;
            return Term::FallsThrough;
        }
        case NodeKind::ReturnStmt: {
            auto* rs = ast_cast<ReturnStmt>(s);
            if (in_defer()) {
                diag_.report({diag::Severity::Error, s->loc,
                              "defer body must not contain return"});
            }
            if (rs->value) walk_expr_value(rs->value.get());
            return Term::Diverges;
        }
        case NodeKind::BreakStmt:
            if (in_defer()) {
                diag_.report({diag::Severity::Error, s->loc,
                              "defer body must not contain break"});
            } else if (loop_depth_ == 0) {
                diag_.report({diag::Severity::Error, s->loc,
                              "break outside loop"});
            }
            return Term::Diverges;
        case NodeKind::ContinueStmt:
            if (in_defer()) {
                diag_.report({diag::Severity::Error, s->loc,
                              "defer body must not contain continue"});
            } else if (loop_depth_ == 0) {
                diag_.report({diag::Severity::Error, s->loc,
                              "continue outside loop"});
            }
            return Term::Diverges;
        case NodeKind::DeferStmt: {
            auto* ds = ast_cast<DeferStmt>(s);
            if (in_defer()) {
                diag_.report({diag::Severity::Error, s->loc,
                              "defer body must not contain defer"});
            }
            ++defer_depth_;
            walk_stmt(ds->body.get());
            --defer_depth_;
            return Term::FallsThrough;
        }
        case NodeKind::BlockStmt:
            return walk_expr_term(ast_cast<BlockStmt>(s)->block.get());
        case NodeKind::EmptyStmt:
            return Term::FallsThrough;
        default:
            return Term::FallsThrough;
        }
    }
};

void walk_fn(FnDecl* fn, diag::DiagnosticEngine& diag) {
    if (!fn->body) return;
    bool requires_return = !fn_returns_hollow(fn);
    CFlowWalker w(diag, requires_return);
    w.walk_fn_body(fn->body.get());
}

void walk_decls(const std::vector<DeclPtr>& decls, diag::DiagnosticEngine& diag) {
    for (auto& d : decls) {
        switch (d->kind) {
        case NodeKind::FnDecl:
            walk_fn(ast_cast<FnDecl>(d.get()), diag);
            break;
        case NodeKind::ImplBlock: {
            auto* impl = ast_cast<ImplBlock>(d.get());
            for (auto& m : impl->methods)
                walk_fn(const_cast<FnDecl*>(&m), diag);
            break;
        }
        case NodeKind::NamespaceDecl:
            walk_decls(ast_cast<NamespaceDecl>(d.get())->decls, diag);
            break;
        default: break;
        }
    }
}

} // namespace

void check_control_flow(ast::Program& prog, diag::DiagnosticEngine& diag) {
    walk_decls(prog.decls, diag);
}

} // namespace mycc::sema
