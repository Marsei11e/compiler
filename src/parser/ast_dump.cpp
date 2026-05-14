/* реализация AstDumper - выводит дерево с отступами */
#include "ast_dump.h"
#include <ostream>
#include <string>

import mycc.diag;

namespace mycc::ast {

// вспомогалки

void AstDumper::indent() {
    out_ << std::string(static_cast<std::size_t>(depth_ * step_), ' ');
}

struct Scope {
    int& depth;
    explicit Scope(int& d) : depth(d) { ++depth; }
    ~Scope() { --depth; }
};

static const char* unary_op_str(UnaryOp op) {
    switch (op) {
    case UnaryOp::Neg: return "-";
    case UnaryOp::Not: return "!";
    }
    return "?";
}

static const char* binary_op_str(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add: return "+";
    case BinaryOp::Sub: return "-";
    case BinaryOp::Mul: return "*";
    case BinaryOp::Div: return "/";
    case BinaryOp::Rem: return "%";
    case BinaryOp::Eq:  return "==";
    case BinaryOp::Ne:  return "!=";
    case BinaryOp::Lt:  return "<";
    case BinaryOp::Gt:  return ">";
    case BinaryOp::Le:  return "<=";
    case BinaryOp::Ge:  return ">=";
    case BinaryOp::And: return "&&";
    case BinaryOp::Or:  return "||";
    }
    return "?";
}

static const char* effect_str(EffectKind e) {
    switch (e) {
    case EffectKind::Pure:   return "@pure";
    case EffectKind::Io:     return "@io";
    case EffectKind::Panics: return "@panics";
    }
    return "@?";
}

// публичная точка входа

void AstDumper::dump(const Program& prog) {
    indent(); out_ << "Program\n";
    Scope s(depth_);
    for (const auto& d : prog.decls)
        dump_decl(*d);
}

// тип

void AstDumper::dump_type(const TypeNode& t) {
    switch (t.kind) {
    case NodeKind::BuiltinTypeRef: {
        const auto& bt = static_cast<const BuiltinTypeRef&>(t);
        out_ << lex::token_kind_name(bt.builtin);
        break;
    }
    case NodeKind::NamedTypeRef:
        out_ << static_cast<const NamedTypeRef&>(t).name;
        break;
    case NodeKind::NamespacedTypeRef: {
        const auto& ns = static_cast<const NamespacedTypeRef&>(t);
        out_ << ns.ns << "::" << ns.name;
        break;
    }
    case NodeKind::ArrayTypeRef: {
        const auto& at = static_cast<const ArrayTypeRef&>(t);
        out_ << "array[";
        dump_type(*at.elem_type);
        out_ << ", " << at.size << "]";
        break;
    }
    case NodeKind::RangeTypeRef: {
        const auto& rt = static_cast<const RangeTypeRef&>(t);
        out_ << "range[";
        dump_type(*rt.elem_type);
        out_ << "]";
        break;
    }
    default:
        out_ << "<type?>";
    }
}

// объявления

void AstDumper::dump_params(const std::vector<ParamDecl>& params) {
    for (const auto& p : params) {
        indent(); out_ << "Param " << p.name;
        if (p.is_self) out_ << " [self]";
        out_ << " : "; dump_type(*p.type); out_ << "\n";
    }
}

void AstDumper::dump_effects(const std::vector<EffectKind>& eff) {
    for (auto e : eff) out_ << ' ' << effect_str(e);
}

void AstDumper::dump_fn(const FnDecl& fn) {
    indent(); out_ << "FnDecl " << fn.name << "(";
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i) out_ << ", ";
        out_ << fn.params[i].name << ": ";
        dump_type(*fn.params[i].type);
    }
    out_ << ") -> "; dump_type(*fn.return_type);
    dump_effects(fn.effects);
    out_ << "\n";
    if (fn.body) {
        Scope s(depth_);
        dump_expr(*fn.body);
    }
}

void AstDumper::dump_decl(const Decl& d) {
    switch (d.kind) {
    case NodeKind::FnDecl:
        dump_fn(static_cast<const FnDecl&>(d));
        break;
    case NodeKind::StructDecl: {
        const auto& sd = static_cast<const StructDecl&>(d);
        indent(); out_ << "StructDecl " << sd.name << "\n";
        Scope s(depth_);
        for (const auto& f : sd.fields) {
            indent(); out_ << "Field " << f.name << " : "; dump_type(*f.type); out_ << "\n";
        }
        break;
    }
    case NodeKind::ImplBlock: {
        const auto& ib = static_cast<const ImplBlock&>(d);
        indent(); out_ << "ImplBlock " << ib.type_name << "\n";
        Scope s(depth_);
        for (const auto& m : ib.methods) dump_fn(m);
        break;
    }
    case NodeKind::NamespaceDecl: {
        const auto& nd = static_cast<const NamespaceDecl&>(d);
        indent(); out_ << "NamespaceDecl " << nd.name << "\n";
        Scope s(depth_);
        for (const auto& inner : nd.decls) dump_decl(*inner);
        break;
    }
    case NodeKind::TypeAliasDecl: {
        const auto& ta = static_cast<const TypeAliasDecl&>(d);
        indent(); out_ << "TypeAlias " << ta.name << " = "; dump_type(*ta.target); out_ << "\n";
        break;
    }
    case NodeKind::VarDecl: {
        const auto& vd = static_cast<const VarDecl&>(d);
        indent(); out_ << "VarDecl " << vd.name;
        if (vd.type_ann) { out_ << " : "; dump_type(*vd.type_ann); }
        out_ << "\n";
        if (vd.init) { Scope s(depth_); dump_expr(*vd.init); }
        break;
    }
    case NodeKind::ConstDecl: {
        const auto& cd = static_cast<const ConstDecl&>(d);
        indent(); out_ << "ConstDecl " << cd.name;
        if (cd.type_ann) { out_ << " : "; dump_type(*cd.type_ann); }
        out_ << "\n";
        if (cd.init) { Scope s(depth_); dump_expr(*cd.init); }
        break;
    }
    default:
        indent(); out_ << "<decl?>\n";
    }
}

// инструкции

void AstDumper::dump_stmt(const Stmt& s) {
    switch (s.kind) {
    case NodeKind::DeclStmt: {
        const auto& ds = static_cast<const DeclStmt&>(s);
        dump_decl(*ds.decl);
        break;
    }
    case NodeKind::AssignStmt: {
        const auto& as = static_cast<const AssignStmt&>(s);
        indent(); out_ << "AssignStmt\n";
        { Scope sc(depth_); dump_expr(*as.target); dump_expr(*as.value); }
        break;
    }
    case NodeKind::ExprStmt: {
        indent(); out_ << "ExprStmt\n";
        Scope sc(depth_); dump_expr(*static_cast<const ExprStmt&>(s).expr);
        break;
    }
    case NodeKind::IfStmt: {
        const auto& is = static_cast<const IfStmt&>(s);
        indent(); out_ << "IfStmt\n";
        { Scope sc(depth_); dump_expr(*is.condition); dump_expr(*is.then_body); }
        if (is.else_branch) { indent(); out_ << "else:\n"; Scope sc(depth_); dump_stmt(*is.else_branch); }
        break;
    }
    case NodeKind::WhileStmt: {
        const auto& ws = static_cast<const WhileStmt&>(s);
        indent(); out_ << "WhileStmt\n";
        Scope sc(depth_); dump_expr(*ws.condition); dump_expr(*ws.body);
        break;
    }
    case NodeKind::ForStmt: {
        const auto& fs = static_cast<const ForStmt&>(s);
        indent(); out_ << "ForStmt " << fs.var_name << " in\n";
        Scope sc(depth_); dump_expr(*fs.range_expr); dump_expr(*fs.body);
        break;
    }
    case NodeKind::ReturnStmt: {
        const auto& rs = static_cast<const ReturnStmt&>(s);
        indent(); out_ << "ReturnStmt\n";
        if (rs.value) { Scope sc(depth_); dump_expr(*rs.value); }
        break;
    }
    case NodeKind::BreakStmt:    indent(); out_ << "BreakStmt\n";    break;
    case NodeKind::ContinueStmt: indent(); out_ << "ContinueStmt\n"; break;
    case NodeKind::EmptyStmt:    indent(); out_ << "EmptyStmt\n";    break;
    case NodeKind::DeferStmt: {
        indent(); out_ << "DeferStmt\n";
        Scope sc(depth_); dump_stmt(*static_cast<const DeferStmt&>(s).body);
        break;
    }
    case NodeKind::BlockStmt: {
        indent(); out_ << "BlockStmt\n";
        Scope sc(depth_); dump_expr(*static_cast<const BlockStmt&>(s).block);
        break;
    }
    default:
        indent(); out_ << "<stmt?>\n";
    }
}

// выражения

void AstDumper::dump_expr(const Expr& e) {
    switch (e.kind) {
    case NodeKind::IntLit: {
        const auto& il = static_cast<const IntLit&>(e);
        indent(); out_ << "IntLit " << il.data.value;
        switch (il.data.suffix) {
        case lex::IntSuffix::U:  out_ << "u";  break;
        case lex::IntSuffix::L:  out_ << "L";  break;
        case lex::IntSuffix::UL: out_ << "uL"; break;
        default: break;
        }
        out_ << "\n";
        break;
    }
    case NodeKind::FloatLit: {
        const auto& fl = static_cast<const FloatLit&>(e);
        indent(); out_ << "FloatLit " << fl.data.value;
        if (fl.data.is_f32) out_ << "f";
        out_ << "\n";
        break;
    }
    case NodeKind::BoolLit:
        indent(); out_ << "BoolLit " << (static_cast<const BoolLit&>(e).value ? "true" : "false") << "\n";
        break;
    case NodeKind::StringLit:
        indent(); out_ << "StringLit \"" << static_cast<const StringLit&>(e).value << "\"\n";
        break;
    case NodeKind::IdentExpr:
        indent(); out_ << "Ident " << static_cast<const IdentExpr&>(e).name << "\n";
        break;
    case NodeKind::SelfExpr:
        indent(); out_ << "Self\n";
        break;
    case NodeKind::NamespaceAccess: {
        const auto& na = static_cast<const NamespaceAccess&>(e);
        indent(); out_ << "NamespaceAccess " << na.scope << "::" << na.member << "\n";
        break;
    }
    case NodeKind::UnaryExpr: {
        const auto& ue = static_cast<const UnaryExpr&>(e);
        indent(); out_ << "UnaryExpr " << unary_op_str(ue.op) << "\n";
        Scope sc(depth_); dump_expr(*ue.operand);
        break;
    }
    case NodeKind::BinaryExpr: {
        const auto& be = static_cast<const BinaryExpr&>(e);
        indent(); out_ << "BinaryExpr " << binary_op_str(be.op) << "\n";
        Scope sc(depth_); dump_expr(*be.left); dump_expr(*be.right);
        break;
    }
    case NodeKind::RangeExpr: {
        const auto& re = static_cast<const RangeExpr&>(e);
        indent(); out_ << "RangeExpr\n";
        Scope sc(depth_); dump_expr(*re.from); dump_expr(*re.to);
        break;
    }
    case NodeKind::PipeExpr: {
        const auto& pe = static_cast<const PipeExpr&>(e);
        indent(); out_ << "PipeExpr\n";
        Scope sc(depth_); dump_expr(*pe.left); dump_expr(*pe.right);
        break;
    }
    case NodeKind::CallExpr: {
        const auto& ce = static_cast<const CallExpr&>(e);
        indent(); out_ << "CallExpr\n";
        Scope sc(depth_);
        dump_expr(*ce.callee);
        for (const auto& a : ce.args) dump_expr(*a);
        break;
    }
    case NodeKind::MethodCallExpr: {
        const auto& me = static_cast<const MethodCallExpr&>(e);
        indent(); out_ << "MethodCallExpr ." << me.method_name << "\n";
        Scope sc(depth_);
        dump_expr(*me.receiver);
        for (const auto& a : me.args) dump_expr(*a);
        break;
    }
    case NodeKind::FieldAccess: {
        const auto& fa = static_cast<const FieldAccess&>(e);
        indent(); out_ << "FieldAccess ." << fa.field_name << "\n";
        Scope sc(depth_); dump_expr(*fa.receiver);
        break;
    }
    case NodeKind::IndexExpr: {
        const auto& ie = static_cast<const IndexExpr&>(e);
        indent(); out_ << "IndexExpr\n";
        Scope sc(depth_); dump_expr(*ie.base); dump_expr(*ie.index);
        break;
    }
    case NodeKind::CastExpr: {
        const auto& ce = static_cast<const CastExpr&>(e);
        indent(); out_ << "CastExpr -> "; dump_type(*ce.target_type); out_ << "\n";
        Scope sc(depth_); dump_expr(*ce.operand);
        break;
    }
    case NodeKind::ArrayLit: {
        const auto& al = static_cast<const ArrayLit&>(e);
        indent(); out_ << "ArrayLit\n";
        Scope sc(depth_); for (const auto& el : al.elements) dump_expr(*el);
        break;
    }
    case NodeKind::StructLit: {
        const auto& sl = static_cast<const StructLit&>(e);
        indent(); out_ << "StructLit " << sl.type_name << "\n";
        Scope sc(depth_);
        for (const auto& f : sl.fields) {
            indent(); out_ << "." << f.name << ":\n";
            Scope sc2(depth_); dump_expr(*f.value);
        }
        break;
    }
    case NodeKind::IfExpr: {
        const auto& ie = static_cast<const IfExpr&>(e);
        indent(); out_ << "IfExpr\n";
        Scope sc(depth_); dump_expr(*ie.condition); dump_expr(*ie.then_body); dump_expr(*ie.else_body);
        break;
    }
    case NodeKind::BlockExpr: {
        const auto& be = static_cast<const BlockExpr&>(e);
        indent(); out_ << "BlockExpr\n";
        Scope sc(depth_);
        for (const auto& st : be.stmts) dump_stmt(*st);
        if (be.final_expr) { indent(); out_ << "[final]\n"; Scope sc2(depth_); dump_expr(*be.final_expr); }
        break;
    }
    default:
        indent(); out_ << "<expr?>\n";
    }
}

} // namespace mycc::ast
