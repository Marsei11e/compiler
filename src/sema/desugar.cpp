
#include "sema/desugar.h"

#include "parser/ast.h"

#include <utility>
#include <vector>

namespace mycc::sema {

using namespace ast;

namespace {

void desugar_expr(ExprPtr& slot, diag::DiagnosticEngine& diag);
void desugar_stmt(Stmt* s,        diag::DiagnosticEngine& diag);
void desugar_decl(Decl* d,        diag::DiagnosticEngine& diag);




ExprPtr build_pipe_call(PipeExpr* pe, diag::DiagnosticEngine& diag) {
    diag::SourceLocation loc = pe->loc;
    ExprPtr left  = std::move(pe->left);
    ExprPtr right = std::move(pe->right);

    switch (right->kind) {
    case NodeKind::IdentExpr:
    case NodeKind::NamespaceAccess: {
        std::vector<ExprPtr> args;
        args.push_back(std::move(left));
        return std::make_unique<CallExpr>(loc, std::move(right), std::move(args));
    }
    case NodeKind::CallExpr: {
        auto* ce = ast_cast<CallExpr>(right.get());
        if (ce->callee->kind != NodeKind::IdentExpr &&
            ce->callee->kind != NodeKind::NamespaceAccess) {
            diag.report({diag::Severity::Error, pe->loc,
                         "right side of '|>' must be a function name or call"});
            return nullptr;
        }
        std::vector<ExprPtr> args;
        args.reserve(ce->args.size() + 1);
        args.push_back(std::move(left));
        for (auto& a : ce->args) args.push_back(std::move(a));
        return std::make_unique<CallExpr>(loc, std::move(ce->callee), std::move(args));
    }
    default:
        diag.report({diag::Severity::Error, pe->loc,
                     "right side of '|>' must be a function name or call"});
        return nullptr;
    }
}

void desugar_expr(ExprPtr& slot, diag::DiagnosticEngine& diag) {
    if (!slot) return;
    Expr* e = slot.get();

    switch (e->kind) {
    case NodeKind::ArrayLit: {
        auto* al = ast_cast<ArrayLit>(e);
        for (auto& el : al->elements) desugar_expr(el, diag);
        return;
    }
    case NodeKind::StructLit: {
        auto* sl = ast_cast<StructLit>(e);
        for (auto& f : sl->fields) desugar_expr(f.value, diag);
        return;
    }
    case NodeKind::FieldAccess:
        desugar_expr(ast_cast<FieldAccess>(e)->receiver, diag);
        return;
    case NodeKind::IndexExpr: {
        auto* ie = ast_cast<IndexExpr>(e);
        desugar_expr(ie->base,  diag);
        desugar_expr(ie->index, diag);
        return;
    }
    case NodeKind::CallExpr: {
        auto* ce = ast_cast<CallExpr>(e);
        desugar_expr(ce->callee, diag);
        for (auto& a : ce->args) desugar_expr(a, diag);
        return;
    }
    case NodeKind::MethodCallExpr: {
        auto* mc = ast_cast<MethodCallExpr>(e);
        desugar_expr(mc->receiver, diag);
        for (auto& a : mc->args) desugar_expr(a, diag);
        return;
    }
    case NodeKind::UnaryExpr:
        desugar_expr(ast_cast<UnaryExpr>(e)->operand, diag);
        return;
    case NodeKind::BinaryExpr: {
        auto* be = ast_cast<BinaryExpr>(e);
        desugar_expr(be->left,  diag);
        desugar_expr(be->right, diag);
        return;
    }
    case NodeKind::RangeExpr: {
        auto* re = ast_cast<RangeExpr>(e);
        desugar_expr(re->from, diag);
        desugar_expr(re->to,   diag);
        return;
    }
    case NodeKind::PipeExpr: {
        auto* pe = ast_cast<PipeExpr>(e);
        desugar_expr(pe->left,  diag);
        desugar_expr(pe->right, diag);
        if (auto repl = build_pipe_call(pe, diag))
            slot = std::move(repl);
        return;
    }
    case NodeKind::CastExpr:
        desugar_expr(ast_cast<CastExpr>(e)->operand, diag);
        return;
    case NodeKind::IfExpr: {
        auto* ie = ast_cast<IfExpr>(e);
        desugar_expr(ie->condition, diag);
        desugar_expr(ie->then_body, diag);
        desugar_expr(ie->else_body, diag);
        return;
    }
    case NodeKind::BlockExpr: {
        auto* be = ast_cast<BlockExpr>(e);
        for (auto& s : be->stmts) desugar_stmt(s.get(), diag);
        desugar_expr(be->final_expr, diag);
        return;
    }
    case NodeKind::IntLit:
    case NodeKind::FloatLit:
    case NodeKind::BoolLit:
    case NodeKind::StringLit:
    case NodeKind::IdentExpr:
    case NodeKind::SelfExpr:
    case NodeKind::NamespaceAccess:
        return;
    default:
        return;
    }
}

void desugar_stmt(Stmt* s, diag::DiagnosticEngine& diag) {
    if (!s) return;
    switch (s->kind) {
    case NodeKind::DeclStmt:
        desugar_decl(ast_cast<DeclStmt>(s)->decl.get(), diag);
        return;
    case NodeKind::AssignStmt: {
        auto* as = ast_cast<AssignStmt>(s);
        desugar_expr(as->target, diag);
        desugar_expr(as->value,  diag);
        return;
    }
    case NodeKind::ExprStmt:
        desugar_expr(ast_cast<ExprStmt>(s)->expr, diag);
        return;
    case NodeKind::IfStmt: {
        auto* is_ = ast_cast<IfStmt>(s);
        desugar_expr(is_->condition, diag);
        desugar_expr(is_->then_body, diag);
        if (is_->else_branch) desugar_stmt(is_->else_branch.get(), diag);
        return;
    }
    case NodeKind::WhileStmt: {
        auto* ws = ast_cast<WhileStmt>(s);
        desugar_expr(ws->condition, diag);
        desugar_expr(ws->body,      diag);
        return;
    }
    case NodeKind::ForStmt: {
        auto* fs = ast_cast<ForStmt>(s);
        desugar_expr(fs->range_expr, diag);
        desugar_expr(fs->body,       diag);
        return;
    }
    case NodeKind::ReturnStmt:
        desugar_expr(ast_cast<ReturnStmt>(s)->value, diag);
        return;
    case NodeKind::DeferStmt:
        desugar_stmt(ast_cast<DeferStmt>(s)->body.get(), diag);
        return;
    case NodeKind::BlockStmt:
        desugar_expr(ast_cast<BlockStmt>(s)->block, diag);
        return;
    case NodeKind::BreakStmt:
    case NodeKind::ContinueStmt:
    case NodeKind::EmptyStmt:
        return;
    default:
        return;
    }
}

void desugar_decl(Decl* d, diag::DiagnosticEngine& diag) {
    if (!d) return;
    switch (d->kind) {
    case NodeKind::VarDecl:
        desugar_expr(ast_cast<VarDecl>(d)->init, diag);
        return;
    case NodeKind::ConstDecl:
        desugar_expr(ast_cast<ConstDecl>(d)->init, diag);
        return;
    case NodeKind::FnDecl:
        desugar_expr(ast_cast<FnDecl>(d)->body, diag);
        return;
    case NodeKind::ImplBlock: {
        auto* ib = ast_cast<ImplBlock>(d);
        for (auto& m : ib->methods) desugar_expr(m.body, diag);
        return;
    }
    case NodeKind::NamespaceDecl: {
        auto* ns = ast_cast<NamespaceDecl>(d);
        for (auto& sub : ns->decls) desugar_decl(sub.get(), diag);
        return;
    }
    default:
        return;
    }
}

} 

void desugar_program(ast::Program& prog, diag::DiagnosticEngine& diag) {
    for (auto& d : prog.decls) desugar_decl(d.get(), diag);
}

} 
