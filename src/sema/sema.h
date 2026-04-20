#pragma once
#include "diag/diagnostic.h"
#include "diag/source.h"
#include "parser/ast.h"
#include "sema/scope.h"
#include "sema/type.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace mycc::sema {

class Sema {
public:
    Sema(diag::DiagnosticEngine& diag, diag::SourceManager& sm);

    
    
    bool analyze_pass1(ast::Program& prog);

    
    
    
    bool analyze_pass2(ast::Program& prog);

    TypeInterner& types()        { return types_; }
    Scope&        global_scope() { return *global_; }

private:
    diag::DiagnosticEngine& diag_;
    diag::SourceManager&    sm_;
    TypeInterner             types_;
    std::unique_ptr<Scope>   global_;
    std::vector<std::unique_ptr<Scope>> ns_scopes_;

    
    TypeId current_fn_return_ty_{kInvalidTypeId};
    bool   current_in_loop_{false};
    
    std::unordered_map<uint32_t, StructSymbol*> struct_type_map_;

    
    void   init_builtins();
    TypeId resolve_type(const ast::TypeNode* node, Scope* scope);
    void   collect_decl(ast::Decl* decl, Scope* scope);
    void   collect_fn(ast::FnDecl* fn, Scope* scope);
    void   collect_struct(ast::StructDecl* s, Scope* scope);
    void   collect_impl(ast::ImplBlock* impl, Scope* scope);
    void   collect_namespace_decl(ast::NamespaceDecl* ns, Scope* scope);
    bool   signatures_match(const FnSymbol& a, const FnSymbol& b);
    void   check_body_redeclarations(ast::Expr* body_expr);

    
    void   analyze_fns_in_scope(const std::vector<ast::DeclPtr>& decls, Scope* scope);
    void   check_fn_body(ast::FnDecl* fn, Scope* parent);
    TypeId check_expr(ast::Expr* expr, TypeId expected, Scope* scope);
    TypeId check_binary_expr(ast::BinaryExpr* be, TypeId expected, Scope* scope);
    void   check_stmt(ast::Stmt* stmt, Scope* scope);
};

} 
