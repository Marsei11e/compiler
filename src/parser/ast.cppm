module;

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

export module mycc.parser:ast;

import mycc.diag;
import mycc.lexer;

export namespace mycc::ast {

// предварительные объявления

struct TypeNode;
struct Decl;
struct Stmt;
struct Expr;

using TypePtr = std::unique_ptr<TypeNode>;
using DeclPtr = std::unique_ptr<Decl>;
using StmtPtr = std::unique_ptr<Stmt>;
using ExprPtr = std::unique_ptr<Expr>;

// тег вида узла (вместо RTTI)

enum class NodeKind {
    // ссылки на типы
    BuiltinTypeRef, NamedTypeRef, NamespacedTypeRef, ArrayTypeRef, RangeTypeRef,

    // объявления
    Program,
    FnDecl, StructDecl, ImplBlock, NamespaceDecl, TypeAliasDecl,
    VarDecl, ConstDecl,
    FieldDecl, ParamDecl,

    // инструкции
    DeclStmt, // оборачивает VarDecl/ConstDecl внутри тела
    AssignStmt, ExprStmt,
    IfStmt, WhileStmt, ForStmt,
    ReturnStmt, BreakStmt, ContinueStmt,
    DeferStmt, BlockStmt, EmptyStmt,

    // выражения
    IntLit, FloatLit, BoolLit, StringLit,
    ArrayLit, StructLit,
    IdentExpr, SelfExpr,
    NamespaceAccess, FieldAccess,
    IndexExpr, CallExpr, MethodCallExpr,
    UnaryExpr, BinaryExpr,
    RangeExpr, PipeExpr,
    CastExpr,
    IfExpr, BlockExpr,
};

// вспомогательные перечисления

enum class EffectKind { Pure, Io, Panics };

enum class UnaryOp  { Neg, Not };
enum class BinaryOp {
    Add, Sub, Mul, Div, Rem,
    Eq, Ne, Lt, Gt, Le, Ge,
    And, Or,
};

// база

struct Node {
    NodeKind             kind;
    diag::SourceLocation loc;
    explicit Node(NodeKind k, diag::SourceLocation l) : kind(k), loc(l) {}
    virtual ~Node() = default;
    Node(const Node&)            = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&)                 = default;
    Node& operator=(Node&&)      = default;
};

// ссылки на типы (сырые, как в исходнике)

struct TypeNode : Node {
    using Node::Node;
};

struct BuiltinTypeRef : TypeNode {
    lex::TokenKind builtin; // один из Int8..KwString, Hollow
    BuiltinTypeRef(diag::SourceLocation l, lex::TokenKind b)
        : TypeNode(NodeKind::BuiltinTypeRef, l), builtin(b) {}
};

struct NamedTypeRef : TypeNode {
    std::string name;
    NamedTypeRef(diag::SourceLocation l, std::string n)
        : TypeNode(NodeKind::NamedTypeRef, l), name(std::move(n)) {}
};

struct NamespacedTypeRef : TypeNode {
    std::string ns;
    std::string name;
    NamespacedTypeRef(diag::SourceLocation l, std::string ns_, std::string n)
        : TypeNode(NodeKind::NamespacedTypeRef, l), ns(std::move(ns_)), name(std::move(n)) {}
};

struct ArrayTypeRef : TypeNode {
    TypePtr  elem_type;
    uint64_t size;
    ArrayTypeRef(diag::SourceLocation l, TypePtr et, uint64_t sz)
        : TypeNode(NodeKind::ArrayTypeRef, l), elem_type(std::move(et)), size(sz) {}
};

struct RangeTypeRef : TypeNode {
    TypePtr elem_type;
    RangeTypeRef(diag::SourceLocation l, TypePtr et)
        : TypeNode(NodeKind::RangeTypeRef, l), elem_type(std::move(et)) {}
};

// объявления

struct Decl : Node {
    using Node::Node;
};

struct ParamDecl : Decl {
    std::string name;
    TypePtr     type;
    bool        is_self{false}; // true для параметра "self: T"
    ParamDecl(diag::SourceLocation l, std::string n, TypePtr t, bool self = false)
        : Decl(NodeKind::ParamDecl, l), name(std::move(n)), type(std::move(t)), is_self(self) {}
};

struct FieldDecl : Decl {
    std::string name;
    TypePtr     type;
    FieldDecl(diag::SourceLocation l, std::string n, TypePtr t)
        : Decl(NodeKind::FieldDecl, l), name(std::move(n)), type(std::move(t)) {}
};

struct VarDecl : Decl {
    std::string name;
    TypePtr     type_ann; // явная аннотация типа, опциональная
    ExprPtr     init; // обязательный инициализатор
    VarDecl(diag::SourceLocation l, std::string n, TypePtr ta, ExprPtr i)
        : Decl(NodeKind::VarDecl, l), name(std::move(n)), type_ann(std::move(ta)), init(std::move(i)) {}
};

struct ConstDecl : Decl {
    std::string name;
    TypePtr     type_ann;
    ExprPtr     init;
    ConstDecl(diag::SourceLocation l, std::string n, TypePtr ta, ExprPtr i)
        : Decl(NodeKind::ConstDecl, l), name(std::move(n)), type_ann(std::move(ta)), init(std::move(i)) {}
};

struct FnDecl : Decl {
    std::string              name;
    std::vector<ParamDecl>   params;
    TypePtr                  return_type;
    std::vector<EffectKind>  effects;
    ExprPtr                  body; // BlockExpr
    FnDecl(diag::SourceLocation l, std::string n,
           std::vector<ParamDecl> p, TypePtr rt,
           std::vector<EffectKind> eff, ExprPtr b)
        : Decl(NodeKind::FnDecl, l), name(std::move(n)), params(std::move(p)),
          return_type(std::move(rt)), effects(std::move(eff)), body(std::move(b)) {}
};

struct StructDecl : Decl {
    std::string           name;
    std::vector<FieldDecl> fields;
    StructDecl(diag::SourceLocation l, std::string n, std::vector<FieldDecl> f)
        : Decl(NodeKind::StructDecl, l), name(std::move(n)), fields(std::move(f)) {}
};

struct ImplBlock : Decl {
    std::string         type_name;
    std::vector<FnDecl>  methods;
    ImplBlock(diag::SourceLocation l, std::string tn, std::vector<FnDecl> m)
        : Decl(NodeKind::ImplBlock, l), type_name(std::move(tn)), methods(std::move(m)) {}
};

struct NamespaceDecl : Decl {
    std::string name;
    std::vector<DeclPtr> decls;
    NamespaceDecl(diag::SourceLocation l, std::string n, std::vector<DeclPtr> d)
        : Decl(NodeKind::NamespaceDecl, l), name(std::move(n)), decls(std::move(d)) {}
};

struct TypeAliasDecl : Decl {
    std::string name;
    TypePtr     target;
    TypeAliasDecl(diag::SourceLocation l, std::string n, TypePtr t)
        : Decl(NodeKind::TypeAliasDecl, l), name(std::move(n)), target(std::move(t)) {}
};

// узел программы верхнего уровня
struct Program {
    std::vector<DeclPtr> decls;
};

// инструкции

struct Stmt : Node {
    using Node::Node;
};

// оборачивает VarDecl/ConstDecl внутри тела функции
struct DeclStmt : Stmt {
    DeclPtr decl;
    DeclStmt(diag::SourceLocation l, DeclPtr d)
        : Stmt(NodeKind::DeclStmt, l), decl(std::move(d)) {}
};

struct AssignStmt : Stmt {
    ExprPtr target; // lvalue: IdentExpr | IndexExpr | FieldAccess
    ExprPtr value;
    AssignStmt(diag::SourceLocation l, ExprPtr t, ExprPtr v)
        : Stmt(NodeKind::AssignStmt, l), target(std::move(t)), value(std::move(v)) {}
};

struct ExprStmt : Stmt {
    ExprPtr expr;
    ExprStmt(diag::SourceLocation l, ExprPtr e)
        : Stmt(NodeKind::ExprStmt, l), expr(std::move(e)) {}
};

struct IfStmt : Stmt {
    ExprPtr condition;
    ExprPtr then_body; // BlockExpr
    StmtPtr else_branch; // nullptr | IfStmt | ExprStmt(BlockExpr)
    IfStmt(diag::SourceLocation l, ExprPtr c, ExprPtr tb, StmtPtr eb)
        : Stmt(NodeKind::IfStmt, l), condition(std::move(c)),
          then_body(std::move(tb)), else_branch(std::move(eb)) {}
};

struct WhileStmt : Stmt {
    ExprPtr condition;
    ExprPtr body; // BlockExpr
    WhileStmt(diag::SourceLocation l, ExprPtr c, ExprPtr b)
        : Stmt(NodeKind::WhileStmt, l), condition(std::move(c)), body(std::move(b)) {}
};

struct ForStmt : Stmt {
    std::string          var_name;
    diag::SourceLocation var_loc;
    ExprPtr              range_expr;
    ExprPtr              body; // BlockExpr
    ForStmt(diag::SourceLocation l, std::string vn, diag::SourceLocation vl,
            ExprPtr re, ExprPtr b)
        : Stmt(NodeKind::ForStmt, l), var_name(std::move(vn)), var_loc(vl),
          range_expr(std::move(re)), body(std::move(b)) {}
};

struct ReturnStmt : Stmt {
    ExprPtr value; // опциональное (nullptr для полого return)
    ReturnStmt(diag::SourceLocation l, ExprPtr v)
        : Stmt(NodeKind::ReturnStmt, l), value(std::move(v)) {}
};

struct BreakStmt    : Stmt { explicit BreakStmt   (diag::SourceLocation l) : Stmt(NodeKind::BreakStmt,    l) {} };
struct ContinueStmt : Stmt { explicit ContinueStmt(diag::SourceLocation l) : Stmt(NodeKind::ContinueStmt, l) {} };
struct EmptyStmt    : Stmt { explicit EmptyStmt   (diag::SourceLocation l) : Stmt(NodeKind::EmptyStmt,    l) {} };

struct DeferStmt : Stmt {
    StmtPtr body;
    DeferStmt(diag::SourceLocation l, StmtPtr b)
        : Stmt(NodeKind::DeferStmt, l), body(std::move(b)) {}
};

struct BlockStmt : Stmt {
    ExprPtr block; // BlockExpr
    explicit BlockStmt(diag::SourceLocation l, ExprPtr b)
        : Stmt(NodeKind::BlockStmt, l), block(std::move(b)) {}
};

// выражения

struct Expr : Node {
    uint32_t resolved_type_id{0};
    using Node::Node;
};

struct IntLit : Expr {
    lex::IntLiteralData data;
    IntLit(diag::SourceLocation l, lex::IntLiteralData d)
        : Expr(NodeKind::IntLit, l), data(d) {}
};

struct FloatLit : Expr {
    lex::FloatLiteralData data;
    FloatLit(diag::SourceLocation l, lex::FloatLiteralData d)
        : Expr(NodeKind::FloatLit, l), data(d) {}
};

struct BoolLit : Expr {
    bool value;
    BoolLit(diag::SourceLocation l, bool v)
        : Expr(NodeKind::BoolLit, l), value(v) {}
};

struct StringLit : Expr {
    std::string value;
    StringLit(diag::SourceLocation l, std::string v)
        : Expr(NodeKind::StringLit, l), value(std::move(v)) {}
};

struct ArrayLit : Expr {
    std::vector<ExprPtr> elements;
    ArrayLit(diag::SourceLocation l, std::vector<ExprPtr> elems)
        : Expr(NodeKind::ArrayLit, l), elements(std::move(elems)) {}
};

struct StructLitField {
    std::string          name;
    diag::SourceLocation name_loc;
    ExprPtr              value;
};

struct StructLit : Expr {
    std::string                 type_name;
    std::vector<StructLitField> fields;
    StructLit(diag::SourceLocation l, std::string tn, std::vector<StructLitField> f)
        : Expr(NodeKind::StructLit, l), type_name(std::move(tn)), fields(std::move(f)) {}
};

struct IdentExpr : Expr {
    std::string name;
    IdentExpr(diag::SourceLocation l, std::string n)
        : Expr(NodeKind::IdentExpr, l), name(std::move(n)) {}
};

struct SelfExpr : Expr {
    explicit SelfExpr(diag::SourceLocation l) : Expr(NodeKind::SelfExpr, l) {}
};

// namespace::member или Type::assoc_fn
struct NamespaceAccess : Expr {
    std::string scope; // namespace или имя типа
    std::string member;
    NamespaceAccess(diag::SourceLocation l, std::string sc, std::string m)
        : Expr(NodeKind::NamespaceAccess, l), scope(std::move(sc)), member(std::move(m)) {}
};

struct FieldAccess : Expr {
    ExprPtr     receiver;
    std::string field_name;
    FieldAccess(diag::SourceLocation l, ExprPtr r, std::string f)
        : Expr(NodeKind::FieldAccess, l), receiver(std::move(r)), field_name(std::move(f)) {}
};

struct IndexExpr : Expr {
    ExprPtr base;
    ExprPtr index;
    IndexExpr(diag::SourceLocation l, ExprPtr b, ExprPtr i)
        : Expr(NodeKind::IndexExpr, l), base(std::move(b)), index(std::move(i)) {}
};

struct CallExpr : Expr {
    ExprPtr              callee; // IdentExpr, NamespaceAccess или раскрытый pipe
    std::vector<ExprPtr> args;                                                                                                                                                                                      // типы параметров выбранной перегрузки (TypeId.index); заполняет sema -
    std::vector<uint32_t> resolved_param_types;                                                                                                                                                  // нужно codegen-у для mangling, иначе перегрузки схлопываются по имени.
    FnDecl* resolved_decl{nullptr};                                                                                                                                                                                 // выбранная перегрузка (заполняет sema): нужна lowering-у, чтобы получить
    CallExpr(diag::SourceLocation l, ExprPtr c, std::vector<ExprPtr> a)                                                                                                             // полное квалифицированное имя - неквалифицированный вызов соседа по namespace иначе понизился бы в голое имя.
        : Expr(NodeKind::CallExpr, l), callee(std::move(c)), args(std::move(a)) {}
};

struct MethodCallExpr : Expr {
    ExprPtr              receiver;
    std::string          method_name;
    std::vector<ExprPtr> args;
    std::vector<uint32_t> resolved_param_types; // как у CallExpr
    MethodCallExpr(diag::SourceLocation l, ExprPtr r, std::string m, std::vector<ExprPtr> a)
        : Expr(NodeKind::MethodCallExpr, l), receiver(std::move(r)),
          method_name(std::move(m)), args(std::move(a)) {}
};

struct UnaryExpr : Expr {
    UnaryOp op;
    ExprPtr operand;
    UnaryExpr(diag::SourceLocation l, UnaryOp o, ExprPtr e)
        : Expr(NodeKind::UnaryExpr, l), op(o), operand(std::move(e)) {}
};

struct BinaryExpr : Expr {
    BinaryOp op;
    ExprPtr  left;
    ExprPtr  right;
    BinaryExpr(diag::SourceLocation l, BinaryOp o, ExprPtr lhs, ExprPtr rhs)
        : Expr(NodeKind::BinaryExpr, l), op(o), left(std::move(lhs)), right(std::move(rhs)) {}
};

struct RangeExpr : Expr {
    ExprPtr from;
    ExprPtr to;
    RangeExpr(diag::SourceLocation l, ExprPtr f, ExprPtr t)
        : Expr(NodeKind::RangeExpr, l), from(std::move(f)), to(std::move(t)) {}
};

struct PipeExpr : Expr {
    ExprPtr left;
    ExprPtr right; // IdentExpr или CallExpr (частичное применение)
    PipeExpr(diag::SourceLocation l, ExprPtr lhs, ExprPtr rhs)
        : Expr(NodeKind::PipeExpr, l), left(std::move(lhs)), right(std::move(rhs)) {}
};

struct CastExpr : Expr {
    TypePtr target_type;
    ExprPtr operand;
    CastExpr(diag::SourceLocation l, TypePtr tt, ExprPtr e)
        : Expr(NodeKind::CastExpr, l), target_type(std::move(tt)), operand(std::move(e)) {}
};

struct IfExpr : Expr {
    ExprPtr condition;
    ExprPtr then_body; // BlockExpr
    ExprPtr else_body; // BlockExpr (обязателен для IfExpr)
    IfExpr(diag::SourceLocation l, ExprPtr c, ExprPtr tb, ExprPtr eb)
        : Expr(NodeKind::IfExpr, l), condition(std::move(c)),
          then_body(std::move(tb)), else_body(std::move(eb)) {}
};

struct BlockExpr : Expr {
    std::vector<StmtPtr> stmts;
    ExprPtr              final_expr; // опциональное хвостовое выражение (значение блока)
    BlockExpr(diag::SourceLocation l, std::vector<StmtPtr> s, ExprPtr fe)
        : Expr(NodeKind::BlockExpr, l), stmts(std::move(s)), final_expr(std::move(fe)) {}
};

// вспомогательные касты

template<typename T>
T* ast_cast(Node* n) {
    return static_cast<T*>(n);
}
template<typename T>
const T* ast_cast(const Node* n) {
    return static_cast<const T*>(n);
}

} // export namespace mycc::ast
