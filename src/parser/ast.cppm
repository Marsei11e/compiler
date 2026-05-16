module;

#include "parser/_pod.h"

export module mycc.parser:ast;

export namespace mycc::ast {

using ::mycc::ast::TypePtr;
using ::mycc::ast::DeclPtr;
using ::mycc::ast::StmtPtr;
using ::mycc::ast::ExprPtr;

using ::mycc::ast::NodeKind;
using ::mycc::ast::EffectKind;
using ::mycc::ast::UnaryOp;
using ::mycc::ast::BinaryOp;

using ::mycc::ast::Node;
using ::mycc::ast::TypeNode;
using ::mycc::ast::BuiltinTypeRef;
using ::mycc::ast::NamedTypeRef;
using ::mycc::ast::NamespacedTypeRef;
using ::mycc::ast::ArrayTypeRef;
using ::mycc::ast::RangeTypeRef;

using ::mycc::ast::Decl;
using ::mycc::ast::ParamDecl;
using ::mycc::ast::FieldDecl;
using ::mycc::ast::VarDecl;
using ::mycc::ast::ConstDecl;
using ::mycc::ast::FnDecl;
using ::mycc::ast::StructDecl;
using ::mycc::ast::ImplBlock;
using ::mycc::ast::NamespaceDecl;
using ::mycc::ast::TypeAliasDecl;
using ::mycc::ast::Program;

using ::mycc::ast::Stmt;
using ::mycc::ast::DeclStmt;
using ::mycc::ast::AssignStmt;
using ::mycc::ast::ExprStmt;
using ::mycc::ast::IfStmt;
using ::mycc::ast::WhileStmt;
using ::mycc::ast::ForStmt;
using ::mycc::ast::ReturnStmt;
using ::mycc::ast::BreakStmt;
using ::mycc::ast::ContinueStmt;
using ::mycc::ast::EmptyStmt;
using ::mycc::ast::DeferStmt;
using ::mycc::ast::BlockStmt;

using ::mycc::ast::Expr;
using ::mycc::ast::IntLit;
using ::mycc::ast::FloatLit;
using ::mycc::ast::BoolLit;
using ::mycc::ast::StringLit;
using ::mycc::ast::ArrayLit;
using ::mycc::ast::StructLitField;
using ::mycc::ast::StructLit;
using ::mycc::ast::IdentExpr;
using ::mycc::ast::SelfExpr;
using ::mycc::ast::NamespaceAccess;
using ::mycc::ast::FieldAccess;
using ::mycc::ast::IndexExpr;
using ::mycc::ast::CallExpr;
using ::mycc::ast::MethodCallExpr;
using ::mycc::ast::UnaryExpr;
using ::mycc::ast::BinaryExpr;
using ::mycc::ast::RangeExpr;
using ::mycc::ast::PipeExpr;
using ::mycc::ast::CastExpr;
using ::mycc::ast::IfExpr;
using ::mycc::ast::BlockExpr;

using ::mycc::ast::ast_cast;

} // export namespace mycc::ast
