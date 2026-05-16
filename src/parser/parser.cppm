module;

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module mycc.parser;

export import :ast;
export import :dump;

import mycc.diag;
import mycc.lexer;

export namespace mycc::parse {

/* преобразует плоский поток токенов в АСТ программы */
class Parser {
public:
    explicit Parser(std::span<const lex::Token> tokens,
                    diag::DiagnosticEngine& diag);

    ast::Program parse();

private:
    std::span<const lex::Token> tokens_;
    std::size_t                 pos_{0};
    diag::DiagnosticEngine&     diag_;
    bool                        no_struct_lit_{false}; // true при разборе условий if/while/for

    // навигация по потоку токенов
    const lex::Token& peek(std::size_t ahead = 0) const;
    const lex::Token& advance();
    bool at_end() const;
    bool check(lex::TokenKind k) const;
    bool match(lex::TokenKind k);
    bool expect(lex::TokenKind k, std::string_view msg);
    void error(const lex::Token& tok, std::string_view msg);

    // диспетчеризация верхнего уровня
    ast::DeclPtr parse_top_level_decl();

    // парсеры объявлений
    std::unique_ptr<ast::FnDecl>        parse_fn_decl();
    std::unique_ptr<ast::StructDecl>    parse_struct_decl();
    std::unique_ptr<ast::ImplBlock>     parse_impl_block();
    std::unique_ptr<ast::NamespaceDecl> parse_namespace_decl();
    std::unique_ptr<ast::TypeAliasDecl> parse_type_alias_decl();
    std::unique_ptr<ast::VarDecl>       parse_var_decl();
    std::unique_ptr<ast::ConstDecl>     parse_const_decl();

    // разбор типов
    std::vector<ast::ParamDecl>  parse_param_list(bool allow_self);
    std::vector<ast::EffectKind> parse_effect_list();
    ast::TypePtr                 parse_type();

    // разбор инструкций
    ast::StmtPtr parse_stmt();
    ast::StmtPtr parse_if_stmt();
    ast::StmtPtr parse_while_stmt();
    ast::StmtPtr parse_for_stmt();
    ast::StmtPtr parse_return_stmt();
    ast::StmtPtr parse_defer_stmt();
    ast::StmtPtr parse_assign_or_expr_stmt();
    void         synchronize();

    // разбор выражений
    ast::ExprPtr parse_expr(int min_bp = 0);
    ast::ExprPtr parse_unary_expr();
    ast::ExprPtr parse_postfix_expr(ast::ExprPtr lhs);
    ast::ExprPtr parse_primary_expr();
    ast::ExprPtr parse_block_expr();
    ast::ExprPtr parse_if_expr();
    ast::ExprPtr parse_struct_literal(diag::SourceLocation loc, std::string name);
    std::vector<ast::ExprPtr> parse_arg_list();
    ast::ExprPtr build_binary(diag::SourceLocation loc, lex::TokenKind op,
                              ast::ExprPtr lhs, ast::ExprPtr rhs);
};

} // export namespace mycc::parse
