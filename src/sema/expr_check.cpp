/* семантика проход 2 - проверка типов выражений и разрешение имен в телах функций */
module;

#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

module mycc.sema;

import mycc.diag;
import mycc.lexer;
import mycc.parser;

namespace mycc::sema {

using namespace ast;

// встроенные константы TypeId (встроенные типы занимают фиксированные индексы 0–12)

static constexpr TypeId kBoolTy   {static_cast<uint32_t>(TypeKind::Bool)};
static constexpr TypeId kHollowTy {static_cast<uint32_t>(TypeKind::Hollow)};
static constexpr TypeId kI32Ty    {static_cast<uint32_t>(TypeKind::I32)};
static constexpr TypeId kF32Ty    {static_cast<uint32_t>(TypeKind::F32)};
static constexpr TypeId kF64Ty    {static_cast<uint32_t>(TypeKind::F64)};
static constexpr TypeId kStringTy {static_cast<uint32_t>(TypeKind::String)};

// допустимые касты по types.md §7

static bool cast_valid(const TypeInterner& ti, TypeId from, TypeId to) {
    if (from == kInvalidTypeId || to == kInvalidTypeId) return false;
    auto fk = ti.get(from).kind;
    auto tk = ti.get(to).kind;
    if (ti.is_numeric(from) && ti.is_numeric(to)) return true;
    if (fk == TypeKind::Bool && ti.is_numeric(to)) return true;
    if ((ti.is_signed_int(from) || ti.is_unsigned_int(from)) && tk == TypeKind::Bool) return true;
    return false;
}

// точка входа прохода 2

bool Sema::analyze_pass2(ast::Program& prog) {
    desugar_program(prog, diag_);
    analyze_fns_in_scope(prog.decls, global_.get());
    check_control_flow(prog, diag_);
    check_moves(prog, types_, diag_);
    check_effects(prog, *global_, struct_type_map_, types_, diag_);
    return !diag_.has_errors();
}

void Sema::analyze_fns_in_scope(const std::vector<ast::DeclPtr>& decls, Scope* scope) {
    for (auto& d : decls) {
        switch (d->kind) {
        case NodeKind::FnDecl:
            check_fn_body(ast_cast<FnDecl>(d.get()), scope);
            break;
        case NodeKind::ImplBlock: {
            auto* impl = ast_cast<ImplBlock>(d.get());
            for (auto& method : impl->methods)
                check_fn_body(const_cast<FnDecl*>(&method), scope);
            break;
        }
        case NodeKind::NamespaceDecl: {
            auto* ns  = ast_cast<NamespaceDecl>(d.get());
            auto* ent = scope->lookup_local(ns->name);
            if (ent) {
                if (auto* ns_sym = std::get_if<NamespaceSymbol>(ent))
                    if (ns_sym->scope)
                        analyze_fns_in_scope(ns->decls, ns_sym->scope);
            }
            break;
        }
        case NodeKind::VarDecl: {
            auto* vd = ast_cast<VarDecl>(d.get());
            TypeId expected = vd->type_ann
                ? resolve_type(vd->type_ann.get(), scope) : kInvalidTypeId;
            TypeId init_ty = vd->init
                ? check_expr(vd->init.get(), expected, scope) : kInvalidTypeId;
            TypeId actual_ty = (expected != kInvalidTypeId) ? expected : init_ty;
            if (auto* ent = scope->lookup_local(vd->name))
                if (auto* vs = std::get_if<VarSymbol>(ent))
                    vs->type = actual_ty;
            break;
        }
        case NodeKind::ConstDecl: {
            auto* cd = ast_cast<ConstDecl>(d.get());
            TypeId expected = cd->type_ann
                ? resolve_type(cd->type_ann.get(), scope) : kInvalidTypeId;
            TypeId init_ty = cd->init
                ? check_expr(cd->init.get(), expected, scope) : kInvalidTypeId;
            TypeId actual_ty = (expected != kInvalidTypeId) ? expected : init_ty;
            if (auto* ent = scope->lookup_local(cd->name))
                if (auto* vs = std::get_if<VarSymbol>(ent))
                    vs->type = actual_ty;
            break;
        }
        default: break;
        }
    }
}

void Sema::check_fn_body(ast::FnDecl* fn, Scope* parent) {
    if (!fn->body) return;

    TypeId ret_ty = fn->return_type
        ? resolve_type(fn->return_type.get(), parent)
        : kHollowTy;

    auto fn_scope = std::make_unique<Scope>(parent);
    for (auto& param : fn->params) {
        if (!param.type) continue;
        TypeId pt = resolve_type(param.type.get(), parent);
        VarSymbol vs;
        vs.name           = param.name;
        vs.type           = pt;
        vs.is_const       = true;
        vs.is_initialized = true;
        fn_scope->declare(param.name, std::move(vs));
    }

    TypeId saved_ret  = current_fn_return_ty_;
    bool   saved_loop = current_in_loop_;
    current_fn_return_ty_ = ret_ty;
    current_in_loop_      = false;

    check_expr(fn->body.get(), kInvalidTypeId, fn_scope.get());

    current_fn_return_ty_ = saved_ret;
    current_in_loop_      = saved_loop;
}

// check_expr

TypeId Sema::check_expr(ast::Expr* expr, TypeId expected, Scope* scope) {
    if (!expr) return kHollowTy;

    TypeId ty = kInvalidTypeId;

    switch (expr->kind) {

    // литералы

    case NodeKind::IntLit: {
        auto* lit = ast_cast<IntLit>(expr);
        switch (lit->data.suffix) {
        case lex::IntSuffix::None:
            ty = (expected != kInvalidTypeId && types_.is_numeric(expected))
                 ? expected : kI32Ty;
            break;
        case lex::IntSuffix::U:
            ty = TypeId{static_cast<uint32_t>(TypeKind::U32)};
            break;
        case lex::IntSuffix::L:
            ty = TypeId{static_cast<uint32_t>(TypeKind::I64)};
            break;
        case lex::IntSuffix::UL:
            ty = TypeId{static_cast<uint32_t>(TypeKind::U64)};
            break;
        }
        break;
    }

    case NodeKind::FloatLit: {
        auto* lit = ast_cast<FloatLit>(expr);
        ty = (lit->data.is_f32 || expected == kF32Ty) ? kF32Ty : kF64Ty;
        break;
    }

    case NodeKind::BoolLit:
        ty = kBoolTy;
        break;

    case NodeKind::StringLit:
        ty = kStringTy;
        break;

    // идентификаторы

    case NodeKind::IdentExpr: {
        auto* ie = ast_cast<IdentExpr>(expr);
        auto* ent = scope->lookup(ie->name);
        if (!ent) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "undefined '" + ie->name + "'"});
            ty = kInvalidTypeId;
            break;
        }
        if (auto* vs = std::get_if<VarSymbol>(ent)) {
            ty = vs->type;
        } else {
            ty = kInvalidTypeId;
        }
        break;
    }

    case NodeKind::SelfExpr: {
        auto* ent = scope->lookup("self");
        if (ent) {
            if (auto* vs = std::get_if<VarSymbol>(ent))
                ty = vs->type;
        }
        if (ty == kInvalidTypeId)
            ty = kInvalidTypeId;
        break;
    }

    // доступ к полям и индексам

    case NodeKind::FieldAccess: {
        auto* fa = ast_cast<FieldAccess>(expr);
        TypeId recv_ty = check_expr(fa->receiver.get(), kInvalidTypeId, scope);
        if (recv_ty == kInvalidTypeId) { ty = kInvalidTypeId; break; }

        if (types_.get(recv_ty).kind != TypeKind::Struct) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "field access on non-struct type '" +
                          std::string(types_.display_name(recv_ty)) + "'"});
            ty = kInvalidTypeId;
            break;
        }
        auto it = struct_type_map_.find(recv_ty.index);
        if (it == struct_type_map_.end()) { ty = kInvalidTypeId; break; }
        const StructSymbol* ss = it->second;
        for (auto& f : ss->fields) {
            if (f.name == fa->field_name) { ty = f.type; break; }
        }
        if (ty == kInvalidTypeId) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "no field '" + fa->field_name + "' in '" + ss->name + "'"});
        }
        break;
    }

    case NodeKind::IndexExpr: {
        auto* ie = ast_cast<IndexExpr>(expr);
        TypeId base_ty  = check_expr(ie->base.get(),  kInvalidTypeId, scope);
        TypeId index_ty = check_expr(ie->index.get(), kInvalidTypeId, scope);
        if (base_ty == kInvalidTypeId) { ty = kInvalidTypeId; break; }
        if (types_.get(base_ty).kind != TypeKind::Array) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "index operator requires array type, got '" +
                          std::string(types_.display_name(base_ty)) + "'"});
            ty = kInvalidTypeId;
            break;
        }
        if (index_ty != kInvalidTypeId &&
            !types_.is_signed_int(index_ty) && !types_.is_unsigned_int(index_ty)) {
            diag_.report({diag::Severity::Error, ie->index->loc,
                          "array index must be integer type, got '" +
                          std::string(types_.display_name(index_ty)) + "'"});
        }
        ty = types_.get(base_ty).elem;
        break;
    }

    // унарные

    case NodeKind::UnaryExpr: {
        auto* ue = ast_cast<UnaryExpr>(expr);
        if (ue->op == UnaryOp::Not) {
            TypeId oty = check_expr(ue->operand.get(), kBoolTy, scope);
            if (oty != kInvalidTypeId && oty != kBoolTy) {
                diag_.report({diag::Severity::Error, expr->loc,
                              "unary '!' requires bool operand, got '" +
                              std::string(types_.display_name(oty)) + "'"});
                ty = kInvalidTypeId;
            } else {
                ty = kBoolTy;
            }
        } else {
            // UnaryOp::Neg
            TypeId oty = check_expr(ue->operand.get(), expected, scope);
            if (oty == kInvalidTypeId) {
                ty = kInvalidTypeId;
            } else if (!types_.is_numeric(oty)) {
                diag_.report({diag::Severity::Error, expr->loc,
                              "unary '-' requires numeric operand, got '" +
                              std::string(types_.display_name(oty)) + "'"});
                ty = kInvalidTypeId;
            } else {
                ty = oty;
            }
        }
        break;
    }

    // бинарные

    case NodeKind::BinaryExpr: {
        auto* be = ast_cast<BinaryExpr>(expr);
        ty = check_binary_expr(be, expected, scope);
        break;
    }

    // диапазоны

    case NodeKind::RangeExpr: {
        auto* re = ast_cast<RangeExpr>(expr);
        TypeId from_ty = check_expr(re->from.get(), kInvalidTypeId, scope);
        TypeId to_ty   = check_expr(re->to.get(), from_ty, scope);
        if (from_ty == kInvalidTypeId || to_ty == kInvalidTypeId) {
            ty = kInvalidTypeId; break;
        }
        if (!types_.is_signed_int(from_ty) && !types_.is_unsigned_int(from_ty)) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "range '..' requires integer operands, got '" +
                          std::string(types_.display_name(from_ty)) + "'"});
            ty = kInvalidTypeId; break;
        }
        if (from_ty != to_ty) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "type mismatch in range expression: '" +
                          std::string(types_.display_name(from_ty)) + "' and '" +
                          std::string(types_.display_name(to_ty)) + "'"});
            ty = kInvalidTypeId; break;
        }
        ty = types_.intern_range(from_ty);
        break;
    }

    // касты

    case NodeKind::CastExpr: {
        auto* ce = ast_cast<CastExpr>(expr);
        TypeId target_ty  = resolve_type(ce->target_type.get(), scope);
        TypeId operand_ty = check_expr(ce->operand.get(), kInvalidTypeId, scope);
        if (target_ty == kInvalidTypeId || operand_ty == kInvalidTypeId) {
            ty = target_ty; break;
        }
        if (!cast_valid(types_, operand_ty, target_ty)) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "invalid cast from '" +
                          std::string(types_.display_name(operand_ty)) + "' to '" +
                          std::string(types_.display_name(target_ty)) + "'"});
            ty = kInvalidTypeId; break;
        }
        ty = target_ty;
        break;
    }

    // if-выражение

    case NodeKind::IfExpr: {
        auto* ie = ast_cast<IfExpr>(expr);
        TypeId cond_ty = check_expr(ie->condition.get(), kBoolTy, scope);
        if (cond_ty != kInvalidTypeId && cond_ty != kBoolTy) {
            diag_.report({diag::Severity::Error, ie->condition->loc,
                          "if condition must be bool, got '" +
                          std::string(types_.display_name(cond_ty)) + "'"});
        }
        TypeId then_ty = check_expr(ie->then_body.get(), expected, scope);
        TypeId else_ty = ie->else_body
            ? check_expr(ie->else_body.get(), expected, scope) : kHollowTy;

        if (then_ty == kInvalidTypeId || else_ty == kInvalidTypeId) {
            ty = (then_ty != kInvalidTypeId) ? then_ty : else_ty;
            break;
        }
        if (then_ty != else_ty) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "if expression branches have incompatible types: '" +
                          std::string(types_.display_name(then_ty)) + "' and '" +
                          std::string(types_.display_name(else_ty)) + "'"});
            ty = kInvalidTypeId;
        } else {
            ty = then_ty;
        }
        break;
    }

    // блок-выражение

    case NodeKind::BlockExpr: {
        auto* block      = ast_cast<BlockExpr>(expr);
        auto  blk_scope  = std::make_unique<Scope>(scope);
        for (auto& s : block->stmts)
            check_stmt(s.get(), blk_scope.get());
        ty = block->final_expr
            ? check_expr(block->final_expr.get(), expected, blk_scope.get())
            : kHollowTy;
        break;
    }

    // литерал массива

    case NodeKind::ArrayLit: {
        auto* al = ast_cast<ArrayLit>(expr);
        if (al->elements.empty()) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "array literal cannot be empty"});
            ty = kInvalidTypeId; break;
        }

        TypeId elem_expected = kInvalidTypeId;
        size_t size_expected = 0;
        bool   has_expected  = false;
        if (expected != kInvalidTypeId && types_.get(expected).kind == TypeKind::Array) {
            elem_expected = types_.get(expected).elem;
            size_expected = types_.get(expected).array_size;
            has_expected  = true;
        }

        TypeId elem_ty = check_expr(al->elements[0].get(), elem_expected, scope);
        if (elem_ty == kInvalidTypeId && elem_expected != kInvalidTypeId)
            elem_ty = elem_expected;

        for (size_t i = 1; i < al->elements.size(); ++i) {
            TypeId et = check_expr(al->elements[i].get(), elem_ty, scope);
            if (et != kInvalidTypeId && elem_ty != kInvalidTypeId && et != elem_ty) {
                diag_.report({diag::Severity::Error, al->elements[i]->loc,
                              "type mismatch in array literal: '" +
                              std::string(types_.display_name(elem_ty)) + "' vs '" +
                              std::string(types_.display_name(et)) + "'"});
            }
        }

        if (has_expected && al->elements.size() != size_expected) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "array literal has " + std::to_string(al->elements.size()) +
                          " elements but type expects " + std::to_string(size_expected)});
            ty = expected;
            break;
        }
        ty = (elem_ty != kInvalidTypeId)
            ? types_.intern_array(elem_ty, al->elements.size())
            : kInvalidTypeId;
        break;
    }

    // литерал структуры

    case NodeKind::StructLit: {
        auto* sl = ast_cast<StructLit>(expr);
        auto* ent = scope->lookup(sl->type_name);
        if (!ent) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "undefined type '" + sl->type_name + "'"});
            ty = kInvalidTypeId; break;
        }
        auto* ss = std::get_if<StructSymbol>(ent);
        if (!ss) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "'" + sl->type_name + "' is not a struct type"});
            ty = kInvalidTypeId; break;
        }

        // строим карту имя поля -> тип
        std::unordered_map<std::string, TypeId> field_map;
        for (auto& f : ss->fields)
            field_map[f.name] = f.type;

        std::unordered_set<std::string> provided;
        for (auto& lf : sl->fields) {
            auto fit = field_map.find(lf.name);
            if (fit == field_map.end()) {
                diag_.report({diag::Severity::Error, lf.name_loc,
                              "unknown field '" + lf.name + "' in struct '" + ss->name + "'"});
            } else {
                provided.insert(lf.name);
                TypeId vty = check_expr(lf.value.get(), fit->second, scope);
                if (vty != kInvalidTypeId && vty != fit->second) {
                    diag_.report({diag::Severity::Error, lf.value->loc,
                                  "type mismatch for field '" + lf.name + "': expected '" +
                                  std::string(types_.display_name(fit->second)) + "', got '" +
                                  std::string(types_.display_name(vty)) + "'"});
                }
            }
        }
        for (auto& f : ss->fields) {
            if (!provided.count(f.name))
                diag_.report({diag::Severity::Error, expr->loc,
                              "missing field '" + f.name + "' in struct literal '" +
                              ss->name + "'"});
        }
        ty = ss->type_id;
        break;
    }

    // вызовы функций с разрешением перегрузки (§13.2)

    case NodeKind::CallExpr: {
        auto* ce = ast_cast<CallExpr>(expr);

        // сначала вычисляем все аргументы (естественные типы, без контекста ожидания)
        std::vector<TypeId>  arg_types;
        std::vector<ArgKind> arg_kinds;
        arg_types.reserve(ce->args.size());
        arg_kinds.reserve(ce->args.size());
        for (auto& arg : ce->args) {
            arg_types.push_back(check_expr(arg.get(), kInvalidTypeId, scope));
            arg_kinds.push_back(arg_kind_of(arg.get()));
        }

        // если хоть один аргумент не прошел, не выдаем каскадных ошибок перегрузки
        bool any_bad = false;
        for (TypeId t : arg_types) if (t == kInvalidTypeId) { any_bad = true; break; }
        if (any_bad) { ty = kHollowTy; break; }

        // находим набор перегрузок из выражения вызываемого
        std::optional<OverloadSet> tmp_os;
        const OverloadSet* overloads = nullptr;
        std::string        fn_name;
        diag::SourceLocation call_loc = expr->loc;

        if (ce->callee->kind == NodeKind::IdentExpr) {
            auto* ie = ast_cast<IdentExpr>(ce->callee.get());
            fn_name  = ie->name;
            call_loc = ie->loc;
            auto* ent = scope->lookup(fn_name);
            if (!ent) {
                diag_.report({diag::Severity::Error, call_loc,
                              "undefined function '" + fn_name + "'"});
                ty = kInvalidTypeId; break;
            }
            if (auto* os = std::get_if<OverloadSet>(ent)) {
                overloads = os;
            } else {
                diag_.report({diag::Severity::Error, call_loc,
                              "'" + fn_name + "' is not a function"});
                ty = kInvalidTypeId; break;
            }
        } else if (ce->callee->kind == NodeKind::NamespaceAccess) {
            auto* na = ast_cast<NamespaceAccess>(ce->callee.get());
            call_loc = na->loc;
            fn_name  = na->scope + "::" + na->member;
            auto* ent = scope->lookup(na->scope);
            if (!ent) {
                diag_.report({diag::Severity::Error, call_loc,
                              "undefined '" + na->scope + "'"});
                ty = kInvalidTypeId; break;
            }
            if (auto* ns_sym = std::get_if<NamespaceSymbol>(ent)) {
                auto* fn_ent = ns_sym->scope->lookup(na->member);
                if (!fn_ent) {
                    diag_.report({diag::Severity::Error, call_loc,
                                  "undefined function '" + fn_name + "'"});
                    ty = kInvalidTypeId; break;
                }
                if (auto* os = std::get_if<OverloadSet>(fn_ent)) {
                    overloads = os;
                } else {
                    diag_.report({diag::Severity::Error, call_loc,
                                  "'" + fn_name + "' is not a function"});
                    ty = kInvalidTypeId; break;
                }
            } else if (auto* ss = std::get_if<StructSymbol>(ent)) {
                tmp_os    = filter_by_name(ss->methods, na->member);
                overloads = &tmp_os.value();
            } else {
                diag_.report({diag::Severity::Error, call_loc,
                              "'" + na->scope + "' is not a namespace or type"});
                ty = kInvalidTypeId; break;
            }
        } else {
            // неподдерживаемая форма вызываемого
            ty = kHollowTy; break;
        }

        if (!overloads) { ty = kInvalidTypeId; break; }

        auto result = resolve_call(*overloads, arg_types, arg_kinds, types_);
        switch (result.status) {
        case OverloadStatus::Resolved:
            // перепроверяем аргументы с уточненными типами параметров - правим resolved_type_id у литералов
            for (size_t i = 0; i < ce->args.size(); ++i)
                check_expr(ce->args[i].get(), result.fn->params[i].type, scope);
            ce->resolved_param_types.clear();
            for (const auto& p : result.fn->params)
                ce->resolved_param_types.push_back(p.type.index);
            ty = result.fn->return_ty;
            break;
        case OverloadStatus::NoMatch:
            diag_.report({diag::Severity::Error, call_loc,
                          "no matching overload for call to '" + fn_name + "'"});
            ty = kInvalidTypeId;
            break;
        case OverloadStatus::Ambiguous:
            diag_.report({diag::Severity::Error, call_loc,
                          "ambiguous call to '" + fn_name + "'"});
            ty = kInvalidTypeId;
            break;
        }
        break;
    }

    // вызовы методов с разрешением перегрузки (§13.4)

    case NodeKind::MethodCallExpr: {
        auto* mc = ast_cast<MethodCallExpr>(expr);

        TypeId recv_ty = check_expr(mc->receiver.get(), kInvalidTypeId, scope);

        std::vector<TypeId>  arg_types = {recv_ty};
        std::vector<ArgKind> arg_kinds = {ArgKind::Regular};
        for (auto& arg : mc->args) {
            arg_types.push_back(check_expr(arg.get(), kInvalidTypeId, scope));
            arg_kinds.push_back(arg_kind_of(arg.get()));
        }

        if (recv_ty == kInvalidTypeId) { ty = kHollowTy; break; }

        bool any_bad = false;
        for (size_t i = 1; i < arg_types.size(); ++i)
            if (arg_types[i] == kInvalidTypeId) { any_bad = true; break; }
        if (any_bad) { ty = kHollowTy; break; }

        if (types_.get(recv_ty).kind != TypeKind::Struct) {
            diag_.report({diag::Severity::Error, mc->receiver->loc,
                          "method call on non-struct type '" +
                          std::string(types_.display_name(recv_ty)) + "'"});
            ty = kInvalidTypeId; break;
        }
        auto it = struct_type_map_.find(recv_ty.index);
        if (it == struct_type_map_.end()) { ty = kInvalidTypeId; break; }
        const StructSymbol* ss = it->second;

        OverloadSet candidates = filter_by_name(ss->methods, mc->method_name);
        if (candidates.overloads.empty()) {
            diag_.report({diag::Severity::Error, expr->loc,
                          "no method '" + mc->method_name + "' in '" + ss->name + "'"});
            ty = kInvalidTypeId; break;
        }

        auto result = resolve_call(candidates, arg_types, arg_kinds, types_);
        switch (result.status) {
        case OverloadStatus::Resolved:
            mc->resolved_param_types.clear();
            for (const auto& p : result.fn->params)
                mc->resolved_param_types.push_back(p.type.index);
            ty = result.fn->return_ty;
            break;
        case OverloadStatus::NoMatch:
            diag_.report({diag::Severity::Error, expr->loc,
                          "no matching overload for method '" + mc->method_name +
                          "' in '" + ss->name + "'"});
            ty = kInvalidTypeId;
            break;
        case OverloadStatus::Ambiguous:
            diag_.report({diag::Severity::Error, expr->loc,
                          "ambiguous call to method '" + mc->method_name +
                          "' in '" + ss->name + "'"});
            ty = kInvalidTypeId;
            break;
        }
        break;
    }

    // pipe и одиночный доступ к пространству имен

    // PipeExpr переписывается в CallExpr до прохода 2 в desugar_program(),
    // значит сюда мы попали только если desugar уже сообщил об ошибке
    // (недопустимая правая часть). подавляем каскадные диагностики.
    case NodeKind::PipeExpr:
        ty = kInvalidTypeId;
        break;

    case NodeKind::NamespaceAccess:
        ty = kHollowTy;
        break;

    default:
        ty = kHollowTy;
        break;
    }

    expr->resolved_type_id = ty.index;
    return ty;
}

// проверка типов бинарного выражения

TypeId Sema::check_binary_expr(ast::BinaryExpr* be, TypeId expected, Scope* scope) {
    using BO = BinaryOp;

    // логические операторы
    if (be->op == BO::And || be->op == BO::Or) {
        TypeId lt = check_expr(be->left.get(),  kBoolTy, scope);
        TypeId rt = check_expr(be->right.get(), kBoolTy, scope);
        bool bad = false;
        if (lt != kInvalidTypeId && lt != kBoolTy) bad = true;
        if (rt != kInvalidTypeId && rt != kBoolTy) bad = true;
        if (bad) {
            diag_.report({diag::Severity::Error, be->loc,
                          "logical operator requires bool operands"});
            return kInvalidTypeId;
        }
        return kBoolTy;
    }

    // операторы сравнения
    if (be->op == BO::Eq  || be->op == BO::Ne  ||
        be->op == BO::Lt  || be->op == BO::Gt  ||
        be->op == BO::Le  || be->op == BO::Ge) {

        TypeId lt = check_expr(be->left.get(),  kInvalidTypeId, scope);
        TypeId rt = check_expr(be->right.get(), lt, scope);
        if (lt == kInvalidTypeId || rt == kInvalidTypeId) return kInvalidTypeId;

        // range[T] не сравнимы (types.md §2.8)
        if (types_.get(lt).kind == TypeKind::Range) {
            diag_.report({diag::Severity::Error, be->loc,
                          std::string(types_.display_name(lt)) + " is not comparable"});
            return kInvalidTypeId;
        }
        if (types_.get(rt).kind == TypeKind::Range) {
            diag_.report({diag::Severity::Error, be->loc,
                          std::string(types_.display_name(rt)) + " is not comparable"});
            return kInvalidTypeId;
        }

        if (lt != rt) {
            diag_.report({diag::Severity::Error, be->loc,
                          "type mismatch: '" + std::string(types_.display_name(lt)) +
                          "' and '" + std::string(types_.display_name(rt)) + "'"});
            return kInvalidTypeId;
        }

        // операторы порядка не определены для string (types.md §17)
        if ((be->op == BO::Lt || be->op == BO::Gt ||
             be->op == BO::Le || be->op == BO::Ge) && lt == kStringTy) {
            diag_.report({diag::Severity::Error, be->loc,
                          "ordering comparison not defined for string"});
            return kInvalidTypeId;
        }

        // eq/ne допустимы для: numeric, bool, string
        if (be->op == BO::Eq || be->op == BO::Ne) {
            if (!types_.is_numeric(lt) && lt != kBoolTy && lt != kStringTy) {
                diag_.report({diag::Severity::Error, be->loc,
                              "operator '==' requires numeric, bool, or string operands"});
                return kInvalidTypeId;
            }
        } else {
            // lt/gt/le/ge: только числовые типы
            if (!types_.is_numeric(lt)) {
                diag_.report({diag::Severity::Error, be->loc,
                              "ordering comparison requires numeric operands, got '" +
                              std::string(types_.display_name(lt)) + "'"});
                return kInvalidTypeId;
            }
        }
        return kBoolTy;
    }

    // арифметика и остаток
    {
        TypeId lt = check_expr(be->left.get(), expected, scope);
        TypeId rt = check_expr(be->right.get(), lt != kInvalidTypeId ? lt : expected, scope);
        if (lt == kInvalidTypeId || rt == kInvalidTypeId) return kInvalidTypeId;

        // строка + строка (types.md §17)
        if (be->op == BO::Add && lt == kStringTy && rt == kStringTy) return kStringTy;

        // остаток: только целые
        if (be->op == BO::Rem) {
            if (!types_.is_signed_int(lt) && !types_.is_unsigned_int(lt)) {
                diag_.report({diag::Severity::Error, be->loc,
                              "'%' requires integer operands, got '" +
                              std::string(types_.display_name(lt)) + "'"});
                return kInvalidTypeId;
            }
        } else {
            if (!types_.is_numeric(lt)) {
                diag_.report({diag::Severity::Error, be->loc,
                              "arithmetic operator requires numeric operands, got '" +
                              std::string(types_.display_name(lt)) + "'"});
                return kInvalidTypeId;
            }
        }

        if (lt != rt) {
            diag_.report({diag::Severity::Error, be->loc,
                          "type mismatch: '" + std::string(types_.display_name(lt)) +
                          "' and '" + std::string(types_.display_name(rt)) + "'"});
            return kInvalidTypeId;
        }
        return lt;
    }
}

// check_stmt

void Sema::check_stmt(ast::Stmt* stmt, Scope* scope) {
    if (!stmt) return;

    switch (stmt->kind) {

    case NodeKind::DeclStmt: {
        auto* ds = ast_cast<DeclStmt>(stmt);
        if (ds->decl->kind == NodeKind::VarDecl) {
            auto* vd = ast_cast<VarDecl>(ds->decl.get());
            TypeId expected = vd->type_ann
                ? resolve_type(vd->type_ann.get(), scope) : kInvalidTypeId;
            TypeId init_ty = vd->init
                ? check_expr(vd->init.get(), expected, scope) : kInvalidTypeId;
            TypeId actual_ty = (expected != kInvalidTypeId) ? expected : init_ty;
            if (expected != kInvalidTypeId && init_ty != kInvalidTypeId &&
                init_ty != expected) {
                diag_.report({diag::Severity::Error, vd->init->loc,
                              "type mismatch: expected '" +
                              std::string(types_.display_name(expected)) + "', got '" +
                              std::string(types_.display_name(init_ty)) + "'"});
            }
            VarSymbol vs;
            vs.name           = vd->name;
            vs.type           = actual_ty;
            vs.is_const       = false;
            vs.is_initialized = (vd->init != nullptr);
            vs.decl           = vd;
            scope->declare(vd->name, std::move(vs));
        } else if (ds->decl->kind == NodeKind::ConstDecl) {
            auto* cd = ast_cast<ConstDecl>(ds->decl.get());
            TypeId expected = cd->type_ann
                ? resolve_type(cd->type_ann.get(), scope) : kInvalidTypeId;
            TypeId init_ty = cd->init
                ? check_expr(cd->init.get(), expected, scope) : kInvalidTypeId;
            TypeId actual_ty = (expected != kInvalidTypeId) ? expected : init_ty;
            if (expected != kInvalidTypeId && init_ty != kInvalidTypeId &&
                init_ty != expected) {
                diag_.report({diag::Severity::Error, cd->init->loc,
                              "type mismatch: expected '" +
                              std::string(types_.display_name(expected)) + "', got '" +
                              std::string(types_.display_name(init_ty)) + "'"});
            }
            VarSymbol vs;
            vs.name           = cd->name;
            vs.type           = actual_ty;
            vs.is_const       = true;
            vs.is_initialized = (cd->init != nullptr);
            vs.decl           = cd;
            scope->declare(cd->name, std::move(vs));
        }
        break;
    }

    case NodeKind::AssignStmt: {
        auto* as = ast_cast<AssignStmt>(stmt);
        // Проверка присваивания константе
        if (as->target->kind == NodeKind::IdentExpr) {
            auto* ident = ast_cast<IdentExpr>(as->target.get());
            if (auto* entry = scope->lookup(std::string(ident->name))) {
                if (auto* vs = std::get_if<VarSymbol>(entry); vs && vs->is_const) {
                    diag_.report({diag::Severity::Error, as->target->loc,
                                  "assignment to constant '" + std::string(ident->name) + "'"});
                }
            }
        }
        TypeId lval_ty = check_expr(as->target.get(), kInvalidTypeId, scope);
        TypeId rval_ty = check_expr(as->value.get(),  lval_ty, scope);
        if (lval_ty != kInvalidTypeId && rval_ty != kInvalidTypeId &&
            lval_ty != rval_ty) {
            diag_.report({diag::Severity::Error, as->value->loc,
                          "type mismatch in assignment: expected '" +
                          std::string(types_.display_name(lval_ty)) + "', got '" +
                          std::string(types_.display_name(rval_ty)) + "'"});
        }
        break;
    }

    case NodeKind::ExprStmt:
        check_expr(ast_cast<ExprStmt>(stmt)->expr.get(), kInvalidTypeId, scope);
        break;

    case NodeKind::IfStmt: {
        auto* is_ = ast_cast<IfStmt>(stmt);
        TypeId cond_ty = check_expr(is_->condition.get(), kBoolTy, scope);
        if (cond_ty != kInvalidTypeId && cond_ty != kBoolTy) {
            diag_.report({diag::Severity::Error, is_->condition->loc,
                          "if condition must be bool, got '" +
                          std::string(types_.display_name(cond_ty)) + "'"});
        }
        check_expr(is_->then_body.get(), kInvalidTypeId, scope);
        if (is_->else_branch) check_stmt(is_->else_branch.get(), scope);
        break;
    }

    case NodeKind::WhileStmt: {
        auto* ws = ast_cast<WhileStmt>(stmt);
        TypeId cond_ty = check_expr(ws->condition.get(), kBoolTy, scope);
        if (cond_ty != kInvalidTypeId && cond_ty != kBoolTy) {
            diag_.report({diag::Severity::Error, ws->condition->loc,
                          "while condition must be bool, got '" +
                          std::string(types_.display_name(cond_ty)) + "'"});
        }
        bool saved = current_in_loop_;
        current_in_loop_ = true;
        check_expr(ws->body.get(), kInvalidTypeId, scope);
        current_in_loop_ = saved;
        break;
    }

    case NodeKind::ForStmt: {
        auto* fs = ast_cast<ForStmt>(stmt);
        TypeId iter_ty = check_expr(fs->range_expr.get(), kInvalidTypeId, scope);
        TypeId var_ty  = kInvalidTypeId;
        if (iter_ty != kInvalidTypeId) {
            auto& td = types_.get(iter_ty);
            if (td.kind == TypeKind::Range || td.kind == TypeKind::Array)
                var_ty = td.elem;
            else
                diag_.report({diag::Severity::Error, fs->range_expr->loc,
                              "for loop expects range[T] or array[T, N], got '" +
                              std::string(types_.display_name(iter_ty)) + "'"});
        }
        auto inner = std::make_unique<Scope>(scope);
        if (var_ty != kInvalidTypeId) {
            VarSymbol vs;
            vs.name = fs->var_name; vs.type = var_ty;
            vs.is_const = true; vs.is_initialized = true;
            inner->declare(fs->var_name, std::move(vs));
        }
        bool saved = current_in_loop_;
        current_in_loop_ = true;
        check_expr(fs->body.get(), kInvalidTypeId, inner.get());
        current_in_loop_ = saved;
        break;
    }

    case NodeKind::ReturnStmt: {
        auto* rs = ast_cast<ReturnStmt>(stmt);
        if (rs->value) {
            TypeId val_ty = check_expr(rs->value.get(), current_fn_return_ty_, scope);
            if (val_ty != kInvalidTypeId &&
                current_fn_return_ty_ != kInvalidTypeId &&
                current_fn_return_ty_ != kHollowTy &&
                val_ty != current_fn_return_ty_) {
                diag_.report({diag::Severity::Error, rs->value->loc,
                              "return type mismatch: expected '" +
                              std::string(types_.display_name(current_fn_return_ty_)) +
                              "', got '" + std::string(types_.display_name(val_ty)) + "'"});
            }
        }
        break;
    }

    case NodeKind::DeferStmt:
        check_stmt(ast_cast<DeferStmt>(stmt)->body.get(), scope);
        break;

    case NodeKind::BlockStmt:
        check_expr(ast_cast<BlockStmt>(stmt)->block.get(), kInvalidTypeId, scope);
        break;

    case NodeKind::BreakStmt:
    case NodeKind::ContinueStmt:
    case NodeKind::EmptyStmt:
        break;

    default: break;
    }
}

} // namespace mycc::sema
