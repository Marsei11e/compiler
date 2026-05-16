/* семантика проход 1 - сбор объявлений верхнего уровня и структурные проверки */
#include "sema/sema.h"
#include "sema/overload.h"

#include <cassert>
#include <unordered_set>

import mycc.diag;
import mycc.lexer;
import mycc.parser;

namespace mycc::sema {

using namespace ast;

// статические вспомогалки

static TypeKind token_to_typekind(lex::TokenKind k) {
    switch (k) {
    case lex::TokenKind::Int8:     return TypeKind::I8;
    case lex::TokenKind::Int16:    return TypeKind::I16;
    case lex::TokenKind::Int32:    return TypeKind::I32;
    case lex::TokenKind::Int64:    return TypeKind::I64;
    case lex::TokenKind::Uint8:    return TypeKind::U8;
    case lex::TokenKind::Uint16:   return TypeKind::U16;
    case lex::TokenKind::Uint32:   return TypeKind::U32;
    case lex::TokenKind::Uint64:   return TypeKind::U64;
    case lex::TokenKind::Float32:  return TypeKind::F32;
    case lex::TokenKind::Float64:  return TypeKind::F64;
    case lex::TokenKind::KwBool:   return TypeKind::Bool;
    case lex::TokenKind::KwString: return TypeKind::String;
    case lex::TokenKind::Hollow:   return TypeKind::Hollow;
    default:
        assert(false && "unexpected builtin type token");
        std::unreachable();
    }
}

// проверка повторных объявлений в теле (без анализа типов)

namespace {

void check_expr_blocks(const Expr* expr, diag::DiagnosticEngine& diag);
void check_stmt_blocks(const Stmt* stmt, diag::DiagnosticEngine& diag);

void check_block_redecl(const BlockExpr* block, diag::DiagnosticEngine& diag) {
    std::unordered_set<std::string> declared;
    for (auto& stmt : block->stmts) {
        if (stmt->kind == NodeKind::DeclStmt) {
            auto* ds = ast_cast<DeclStmt>(stmt.get());
            std::string      name;
            diag::SourceLocation loc = stmt->loc;
            if (ds->decl->kind == NodeKind::VarDecl) {
                auto* vd = ast_cast<VarDecl>(ds->decl.get());
                name = vd->name; loc = vd->loc;
            } else if (ds->decl->kind == NodeKind::ConstDecl) {
                auto* cd = ast_cast<ConstDecl>(ds->decl.get());
                name = cd->name; loc = cd->loc;
            }
            if (!name.empty() && !declared.insert(name).second) {
                diag.report({diag::Severity::Error, loc,
                             "redeclaration of '" + name + "'"});
            }
        }
        check_stmt_blocks(stmt.get(), diag);
    }
    if (block->final_expr)
        check_expr_blocks(block->final_expr.get(), diag);
}

void check_expr_blocks(const Expr* expr, diag::DiagnosticEngine& diag) {
    if (!expr) return;
    switch (expr->kind) {
    case NodeKind::BlockExpr:
        check_block_redecl(ast_cast<BlockExpr>(expr), diag);
        break;
    case NodeKind::IfExpr: {
        auto* ie = ast_cast<IfExpr>(expr);
        check_expr_blocks(ie->then_body.get(), diag);
        check_expr_blocks(ie->else_body.get(), diag);
        break;
    }
    default: break;
    }
}

void check_stmt_blocks(const Stmt* stmt, diag::DiagnosticEngine& diag) {
    if (!stmt) return;
    switch (stmt->kind) {
    case NodeKind::IfStmt: {
        auto* s = ast_cast<IfStmt>(stmt);
        check_expr_blocks(s->then_body.get(), diag);
        check_stmt_blocks(s->else_branch.get(), diag);
        break;
    }
    case NodeKind::WhileStmt:
        check_expr_blocks(ast_cast<WhileStmt>(stmt)->body.get(), diag);
        break;
    case NodeKind::ForStmt:
        check_expr_blocks(ast_cast<ForStmt>(stmt)->body.get(), diag);
        break;
    case NodeKind::DeferStmt:
        check_stmt_blocks(ast_cast<DeferStmt>(stmt)->body.get(), diag);
        break;
    case NodeKind::BlockStmt:
        check_expr_blocks(ast_cast<BlockStmt>(stmt)->block.get(), diag);
        break;
    default: break;
    }
}

} // anonymous namespace

// Sema

Sema::Sema(diag::DiagnosticEngine& diag, diag::SourceManager& sm)
    : diag_(diag), sm_(sm), global_(std::make_unique<Scope>()) {
    init_builtins();
}

void Sema::init_builtins() {
    // лямбда для регистрации одной перегрузки в глобальном скоупе
    auto reg = [&](const char* fn_name, TypeId ret_ty,
                    std::initializer_list<TypeId> param_tys,
                    std::initializer_list<ast::EffectKind> effs) {
        FnSymbol sym;
        sym.name      = fn_name;
        sym.return_ty = ret_ty;
        sym.decl      = nullptr;
        sym.effects   = std::vector<ast::EffectKind>(effs);
        for (TypeId pt : param_tys) {
            FnSymbol::Param p;
            p.type = pt;
            sym.params.push_back(p);
        }
        global_->declare(fn_name, std::move(sym));
    };

    // константы TypeId (встроенные по индексам, соответствующим TypeKind enum)
    TypeId kHol{static_cast<uint32_t>(TypeKind::Hollow)};
    TypeId kI8 {static_cast<uint32_t>(TypeKind::I8)};
    TypeId kI16{static_cast<uint32_t>(TypeKind::I16)};
    TypeId kI32{static_cast<uint32_t>(TypeKind::I32)};
    TypeId kI64{static_cast<uint32_t>(TypeKind::I64)};
    TypeId kU8 {static_cast<uint32_t>(TypeKind::U8)};
    TypeId kU16{static_cast<uint32_t>(TypeKind::U16)};
    TypeId kU32{static_cast<uint32_t>(TypeKind::U32)};
    TypeId kU64{static_cast<uint32_t>(TypeKind::U64)};
    TypeId kF32{static_cast<uint32_t>(TypeKind::F32)};
    TypeId kF64{static_cast<uint32_t>(TypeKind::F64)};
    TypeId kBol{static_cast<uint32_t>(TypeKind::Bool)};
    TypeId kStr{static_cast<uint32_t>(TypeKind::String)};

    using EK = ast::EffectKind;

    // print(T) -> hollow @io и println(T) -> hollow @io для всех печатаемых T
    for (TypeId pt : {kI8, kI16, kI32, kI64, kU8, kU16, kU32, kU64,
                       kF32, kF64, kBol, kStr}) {
        reg("print",   kHol, {pt}, {EK::Io});
        reg("println", kHol, {pt}, {EK::Io});
    }
    reg("println", kHol, {}, {EK::Io});   // println() — newline only

    reg("input", kStr, {},    {EK::Io});       // input() -> string @io
    reg("exit",  kHol, {kI32},{EK::Panics});   // exit(int32) -> hollow @panics
    reg("panic", kHol, {kStr},{EK::Panics});   // panic(string) -> hollow @panics
    reg("len",   kI32, {kStr},{EK::Pure});     // len(string) -> int32 @pure
}

bool Sema::analyze_pass1(ast::Program& prog) {
    for (auto& decl : prog.decls)
        collect_decl(decl.get(), global_.get());
    return !diag_.has_errors();
}

TypeId Sema::resolve_type(const ast::TypeNode* node, Scope* scope) {
    switch (node->kind) {
    case NodeKind::BuiltinTypeRef:
        return types_.intern_builtin(
            token_to_typekind(ast_cast<BuiltinTypeRef>(node)->builtin));

    case NodeKind::NamedTypeRef: {
        auto* n     = ast_cast<NamedTypeRef>(node);
        auto* entry = scope->lookup(n->name);
        if (!entry) {
            diag_.report({diag::Severity::Error, node->loc,
                          "undefined type '" + n->name + "'"});
            return kInvalidTypeId;
        }
        if (auto* ss = std::get_if<StructSymbol>(entry))
            return ss->type_id;
        diag_.report({diag::Severity::Error, node->loc,
                      "'" + n->name + "' is not a type"});
        return kInvalidTypeId;
    }

    case NodeKind::NamespacedTypeRef: {
        auto* n      = ast_cast<NamespacedTypeRef>(node);
        auto* ns_ent = scope->lookup(n->ns);
        if (!ns_ent) {
            diag_.report({diag::Severity::Error, node->loc,
                          "undefined namespace '" + n->ns + "'"});
            return kInvalidTypeId;
        }
        auto* ns_sym = std::get_if<NamespaceSymbol>(ns_ent);
        if (!ns_sym || !ns_sym->scope) {
            diag_.report({diag::Severity::Error, node->loc,
                          "'" + n->ns + "' is not a namespace"});
            return kInvalidTypeId;
        }
        auto* ty_ent = ns_sym->scope->lookup(n->name);
        if (!ty_ent) {
            diag_.report({diag::Severity::Error, node->loc,
                          "undefined type '" + n->ns + "::" + n->name + "'"});
            return kInvalidTypeId;
        }
        if (auto* ss = std::get_if<StructSymbol>(ty_ent))
            return ss->type_id;
        diag_.report({diag::Severity::Error, node->loc,
                      "'" + n->ns + "::" + n->name + "' is not a type"});
        return kInvalidTypeId;
    }

    case NodeKind::ArrayTypeRef: {
        auto* a    = ast_cast<ArrayTypeRef>(node);
        TypeId elem = resolve_type(a->elem_type.get(), scope);
        if (elem == kInvalidTypeId) return kInvalidTypeId;
        return types_.intern_array(elem, a->size);
    }

    case NodeKind::RangeTypeRef: {
        auto* r    = ast_cast<RangeTypeRef>(node);
        TypeId elem = resolve_type(r->elem_type.get(), scope);
        if (elem == kInvalidTypeId) return kInvalidTypeId;
        return types_.intern_range(elem);
    }

    default:
        return kInvalidTypeId;
    }
}

void Sema::collect_decl(ast::Decl* decl, Scope* scope) {
    switch (decl->kind) {
    case NodeKind::FnDecl:
        collect_fn(ast_cast<FnDecl>(decl), scope);
        break;
    case NodeKind::StructDecl:
        collect_struct(ast_cast<StructDecl>(decl), scope);
        break;
    case NodeKind::ImplBlock:
        collect_impl(ast_cast<ImplBlock>(decl), scope);
        break;
    case NodeKind::NamespaceDecl:
        collect_namespace_decl(ast_cast<NamespaceDecl>(decl), scope);
        break;
    case NodeKind::TypeAliasDecl:
        // отложено: псевдонимы пока не используются в проходе 1
        break;
    case NodeKind::VarDecl: {
        auto* vd = ast_cast<VarDecl>(decl);
        VarSymbol sym;
        sym.name           = vd->name;
        sym.is_const       = false;
        sym.is_initialized = (vd->init != nullptr);
        sym.decl           = vd;
        if (!scope->declare(vd->name, std::move(sym)))
            diag_.report({diag::Severity::Error, vd->loc,
                          "redeclaration of '" + vd->name + "'"});
        break;
    }
    case NodeKind::ConstDecl: {
        auto* cd = ast_cast<ConstDecl>(decl);
        VarSymbol sym;
        sym.name           = cd->name;
        sym.is_const       = true;
        sym.is_initialized = (cd->init != nullptr);
        sym.decl           = cd;
        if (!scope->declare(cd->name, std::move(sym)))
            diag_.report({diag::Severity::Error, cd->loc,
                          "redeclaration of '" + cd->name + "'"});
        break;
    }
    default: break;
    }
}

void Sema::collect_fn(ast::FnDecl* fn, Scope* scope) {
    FnSymbol sym;
    sym.name    = fn->name;
    sym.decl    = fn;
    sym.effects = fn->effects;

    for (auto& param : fn->params) {
        FnSymbol::Param p;
        p.name = param.name;
        p.type = param.type ? resolve_type(param.type.get(), scope) : kInvalidTypeId;
        sym.params.push_back(p);
    }
    sym.return_ty = fn->return_type
        ? resolve_type(fn->return_type.get(), scope)
        : types_.intern_builtin(TypeKind::Hollow);

    // 'main' нельзя перегружать
    if (fn->name == "main" && scope->lookup_local("main")) {
        diag_.report({diag::Severity::Error, fn->loc,
                      "'main' cannot be overloaded"});
        if (fn->body) check_body_redeclarations(fn->body.get());
        return;
    }

    // дублирующая сигнатура в существующем наборе перегрузок
    if (auto* ent = scope->lookup_local(fn->name)) {
        if (auto* os = std::get_if<OverloadSet>(ent)) {
            for (auto* other : os->overloads) {
                if (signatures_match(sym, *other)) {
                    diag_.report({diag::Severity::Error, fn->loc,
                                  "duplicate overload for '" + fn->name +
                                  "': same parameter types"});
                    if (fn->body) check_body_redeclarations(fn->body.get());
                    return;
                }
            }
        } else {
            diag_.report({diag::Severity::Error, fn->loc,
                          "redeclaration of '" + fn->name + "' as a function"});
            if (fn->body) check_body_redeclarations(fn->body.get());
            return;
        }
    }

    scope->declare(fn->name, std::move(sym));
    if (fn->body) check_body_redeclarations(fn->body.get());
}

bool Sema::signatures_match(const FnSymbol& a, const FnSymbol& b) {
    if (a.params.size() != b.params.size()) return false;
    for (size_t i = 0; i < a.params.size(); ++i) {
        if (a.params[i].type != b.params[i].type) return false;
    }
    return true;
}

void Sema::collect_struct(ast::StructDecl* s, Scope* scope) {
    if (scope->lookup_local(s->name)) {
        diag_.report({diag::Severity::Error, s->loc,
                      "redeclaration of '" + s->name + "'"});
        return;
    }
    StructSymbol sym;
    sym.name    = s->name;
    sym.type_id = types_.intern_struct(s);
    for (auto& field : s->fields) {
        StructSymbol::Field f;
        f.name = field.name;
        f.type = resolve_type(field.type.get(), scope);
        sym.fields.push_back(std::move(f));
    }
    scope->declare(s->name, std::move(sym));
    // регистрируем стабильный указатель для поиска полей в проходе 2
    // unordered_map гарантирует стабильность ссылок на значения после вставок
    if (auto* ent = scope->lookup_local(s->name))
        if (auto* ss = std::get_if<StructSymbol>(ent))
            struct_type_map_[ss->type_id.index] = ss;
}

void Sema::collect_impl(ast::ImplBlock* impl, Scope* scope) {
    auto* entry = scope->lookup(impl->type_name);
    if (!entry) {
        diag_.report({diag::Severity::Error, impl->loc,
                      "impl for undefined type '" + impl->type_name + "'"});
        return;
    }
    auto* ss = std::get_if<StructSymbol>(entry);
    if (!ss) {
        diag_.report({diag::Severity::Error, impl->loc,
                      "impl only allowed for struct types, not '" +
                      impl->type_name + "'"});
        return;
    }

    for (auto& method : impl->methods) {
        FnSymbol sym;
        sym.name    = method.name;
        sym.decl    = const_cast<FnDecl*>(&method);
        sym.effects = method.effects;

        for (auto& param : method.params) {
            FnSymbol::Param p;
            p.name = param.name;
            p.type = param.type ? resolve_type(param.type.get(), scope) : kInvalidTypeId;
            sym.params.push_back(p);
        }
        sym.return_ty = method.return_type
            ? resolve_type(method.return_type.get(), scope)
            : types_.intern_builtin(TypeKind::Hollow);

        bool dup = false;
        for (auto* existing : ss->methods.overloads) {
            if (existing->name == sym.name && signatures_match(sym, *existing)) {
                diag_.report({diag::Severity::Error, method.loc,
                              "duplicate method overload '" + method.name +
                              "' in impl of '" + impl->type_name + "'"});
                dup = true;
                break;
            }
        }
        if (!dup) {
            ss->method_storage.push_back(std::make_unique<FnSymbol>(std::move(sym)));
            ss->methods.overloads.push_back(ss->method_storage.back().get());
        }
        if (method.body) check_body_redeclarations(method.body.get());
    }
}

void Sema::collect_namespace_decl(ast::NamespaceDecl* ns, Scope* scope) {
    Scope* ns_scope = nullptr;
    auto*  existing = scope->lookup_local(ns->name);

    if (existing) {
        auto* ns_sym = std::get_if<NamespaceSymbol>(existing);
        if (!ns_sym) {
            diag_.report({diag::Severity::Error, ns->loc,
                          "redeclaration of '" + ns->name + "' as namespace"});
            return;
        }
        ns_scope = ns_sym->scope;
    } else {
        ns_scopes_.push_back(std::make_unique<Scope>(scope));
        ns_scope = ns_scopes_.back().get();
        NamespaceSymbol ns_sym;
        ns_sym.name  = ns->name;
        ns_sym.scope = ns_scope;
        scope->declare(ns->name, std::move(ns_sym));
    }

    for (auto& decl : ns->decls)
        collect_decl(decl.get(), ns_scope);
}

void Sema::check_body_redeclarations(ast::Expr* body_expr) {
    if (!body_expr || body_expr->kind != NodeKind::BlockExpr) return;
    check_block_redecl(ast_cast<BlockExpr>(body_expr), diag_);
}

} // namespace mycc::sema
