
#include "sema/move_check.h"
#include "sema/type.h"

#include "lexer/token.h"
#include "parser/ast.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace mycc::sema {

using namespace ast;

namespace {

class MoveWalker {
public:
    MoveWalker(const TypeInterner& ti, diag::DiagnosticEngine& diag)
        : ti_(ti), diag_(diag) {}

    void walk_fn(FnDecl* fn) {
        if (!fn->body) return;
        push_scope();
        for (auto& p : fn->params) {
            bool is_range = is_range_type_node(p.type.get());
            declare(p.name, is_range);
        }
        walk_expr(fn->body.get());
        pop_scope();
    }

private:
    struct Binding {
        bool is_range;
        bool consumed;
    };

    const TypeInterner&                                            ti_;
    diag::DiagnosticEngine&                                        diag_;
    std::vector<std::vector<std::pair<std::string, Binding>>>      scopes_;

    void push_scope() { scopes_.emplace_back(); }
    void pop_scope()  { scopes_.pop_back(); }

    void declare(const std::string& name, bool is_range) {
        scopes_.back().push_back({name, {is_range, false}});
    }

    Binding* lookup(const std::string& name) {
        for (auto sit = scopes_.rbegin(); sit != scopes_.rend(); ++sit) {
            for (auto& [k, b] : *sit) {
                if (k == name) return &b;
            }
        }
        return nullptr;
    }

    static bool is_range_type_node(const TypeNode* tn) {
        return tn && tn->kind == NodeKind::RangeTypeRef;
    }

    bool is_range_type_id(uint32_t id) const {
        if (id == kInvalidTypeId.index) return false;
        return ti_.get(TypeId{id}).kind == TypeKind::Range;
    }

    

    using Snapshot = std::vector<std::vector<bool>>;

    Snapshot snapshot() const {
        Snapshot s;
        s.reserve(scopes_.size());
        for (auto& sc : scopes_) {
            std::vector<bool> v;
            v.reserve(sc.size());
            for (auto& [k, b] : sc) v.push_back(b.consumed);
            s.push_back(std::move(v));
        }
        return s;
    }

    void restore(const Snapshot& s) {
        for (size_t i = 0; i < scopes_.size() && i < s.size(); ++i) {
            for (size_t j = 0; j < scopes_[i].size() && j < s[i].size(); ++j) {
                scopes_[i][j].second.consumed = s[i][j];
            }
        }
    }

    void merge_consumed(const Snapshot& other) {
        for (size_t i = 0; i < scopes_.size() && i < other.size(); ++i) {
            for (size_t j = 0; j < scopes_[i].size() && j < other[i].size(); ++j) {
                if (other[i][j]) scopes_[i][j].second.consumed = true;
            }
        }
    }

    

    void walk_expr(Expr* e) {
        if (!e) return;
        switch (e->kind) {
        case NodeKind::IdentExpr: {
            auto* ie = ast_cast<IdentExpr>(e);
            if (auto* b = lookup(ie->name); b && b->is_range) {
                if (b->consumed) {
                    diag_.report({diag::Severity::Error, e->loc,
                                  "use of moved value '" + ie->name + "'"});
                } else {
                    b->consumed = true;
                }
            }
            break;
        }
        case NodeKind::BlockExpr: walk_block(ast_cast<BlockExpr>(e)); break;
        case NodeKind::IfExpr: {
            auto* ie = ast_cast<IfExpr>(e);
            walk_expr(ie->condition.get());
            auto pre = snapshot();
            walk_expr(ie->then_body.get());
            auto then_state = snapshot();
            restore(pre);
            walk_expr(ie->else_body.get());
            merge_consumed(then_state);
            break;
        }
        case NodeKind::CallExpr: {
            auto* ce = ast_cast<CallExpr>(e);
            
            
            if (ce->callee->kind != NodeKind::IdentExpr &&
                ce->callee->kind != NodeKind::NamespaceAccess) {
                walk_expr(ce->callee.get());
            }
            for (auto& a : ce->args) walk_expr(a.get());
            break;
        }
        case NodeKind::MethodCallExpr: {
            auto* mc = ast_cast<MethodCallExpr>(e);
            walk_expr(mc->receiver.get());
            for (auto& a : mc->args) walk_expr(a.get());
            break;
        }
        case NodeKind::BinaryExpr: {
            auto* be = ast_cast<BinaryExpr>(e);
            walk_expr(be->left.get());
            walk_expr(be->right.get());
            break;
        }
        case NodeKind::UnaryExpr:
            walk_expr(ast_cast<UnaryExpr>(e)->operand.get()); break;
        case NodeKind::CastExpr:
            walk_expr(ast_cast<CastExpr>(e)->operand.get()); break;
        case NodeKind::FieldAccess:
            walk_expr(ast_cast<FieldAccess>(e)->receiver.get()); break;
        case NodeKind::IndexExpr: {
            auto* ix = ast_cast<IndexExpr>(e);
            walk_expr(ix->base.get());
            walk_expr(ix->index.get());
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
        case NodeKind::RangeExpr: {
            auto* re = ast_cast<RangeExpr>(e);
            walk_expr(re->from.get());
            walk_expr(re->to.get());
            break;
        }
        default: break;
        }
    }

    void walk_block(BlockExpr* b) {
        push_scope();
        for (auto& s : b->stmts) walk_stmt(s.get());
        if (b->final_expr) walk_expr(b->final_expr.get());
        pop_scope();
    }

    void walk_stmt(Stmt* s) {
        if (!s) return;
        switch (s->kind) {
        case NodeKind::DeclStmt: {
            auto* ds = ast_cast<DeclStmt>(s);
            std::string     name;
            Expr*           init     = nullptr;
            const TypeNode* type_ann = nullptr;
            if (ds->decl->kind == NodeKind::VarDecl) {
                auto* vd = ast_cast<VarDecl>(ds->decl.get());
                name = vd->name; init = vd->init.get(); type_ann = vd->type_ann.get();
            } else if (ds->decl->kind == NodeKind::ConstDecl) {
                auto* cd = ast_cast<ConstDecl>(ds->decl.get());
                name = cd->name; init = cd->init.get(); type_ann = cd->type_ann.get();
            } else break;
            if (init) walk_expr(init);
            bool is_range = is_range_type_node(type_ann);
            if (!is_range && init) is_range = is_range_type_id(init->resolved_type_id);
            declare(name, is_range);
            break;
        }
        case NodeKind::AssignStmt: {
            auto* as = ast_cast<AssignStmt>(s);
            walk_expr(as->value.get());
            walk_expr(as->target.get());
            break;
        }
        case NodeKind::ExprStmt:
            walk_expr(ast_cast<ExprStmt>(s)->expr.get());
            break;
        case NodeKind::IfStmt: {
            auto* is_ = ast_cast<IfStmt>(s);
            walk_expr(is_->condition.get());
            auto pre = snapshot();
            walk_expr(is_->then_body.get());
            auto then_state = snapshot();
            restore(pre);
            if (is_->else_branch) walk_stmt(is_->else_branch.get());
            merge_consumed(then_state);
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
            push_scope();
            
            declare(fs->var_name, false);
            walk_expr(fs->body.get());
            pop_scope();
            break;
        }
        case NodeKind::ReturnStmt: {
            auto* rs = ast_cast<ReturnStmt>(s);
            if (rs->value) walk_expr(rs->value.get());
            break;
        }
        case NodeKind::DeferStmt:
            walk_stmt(ast_cast<DeferStmt>(s)->body.get());
            break;
        case NodeKind::BlockStmt:
            walk_expr(ast_cast<BlockStmt>(s)->block.get());
            break;
        default: break;
        }
    }
};

void walk_decls(const std::vector<DeclPtr>& decls,
                const TypeInterner& ti, diag::DiagnosticEngine& diag) {
    for (auto& d : decls) {
        switch (d->kind) {
        case NodeKind::FnDecl: {
            MoveWalker w(ti, diag);
            w.walk_fn(ast_cast<FnDecl>(d.get()));
            break;
        }
        case NodeKind::ImplBlock: {
            auto* impl = ast_cast<ImplBlock>(d.get());
            for (auto& m : impl->methods) {
                MoveWalker w(ti, diag);
                w.walk_fn(const_cast<FnDecl*>(&m));
            }
            break;
        }
        case NodeKind::NamespaceDecl:
            walk_decls(ast_cast<NamespaceDecl>(d.get())->decls, ti, diag);
            break;
        default: break;
        }
    }
}

} 

void check_moves(ast::Program& prog, const TypeInterner& ti,
                 diag::DiagnosticEngine& diag) {
    walk_decls(prog.decls, ti, diag);
}

} 
