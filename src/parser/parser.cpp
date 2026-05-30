module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module mycc.parser;

import mycc.diag;
import mycc.lexer;

namespace mycc::parse {

using lex::TokenKind;
using namespace ast;

// конструктор

Parser::Parser(std::span<const lex::Token> tokens, diag::DiagnosticEngine& diag)
    : tokens_(tokens), diag_(diag) {}

// примитивы потока токенов

const lex::Token& Parser::peek(std::size_t ahead) const {
    std::size_t idx = pos_ + ahead;
    if (idx >= tokens_.size()) return tokens_.back(); // всегда Eof-заглушка
    return tokens_[idx];
}

const lex::Token& Parser::advance() {
    if (!at_end()) ++pos_;
    return tokens_[pos_ - 1];
}

bool Parser::at_end() const {
    return pos_ >= tokens_.size() || tokens_[pos_].kind == TokenKind::Eof;
}

bool Parser::check(TokenKind k) const {
    return !at_end() && peek().kind == k;
}

bool Parser::match(TokenKind k) {
    if (check(k)) { advance(); return true; }
    return false;
}

bool Parser::expect(TokenKind k, std::string_view msg) {
    if (match(k)) return true;
    error(peek(), msg);
    return false;
}

void Parser::error(const lex::Token& tok, std::string_view msg) {
    diag_.report({diag::Severity::Error, tok.loc, std::string(msg)});
}

// точка входа

Program Parser::parse() {
    Program prog;
    while (!at_end()) {
        auto decl = parse_top_level_decl();
        if (decl) prog.decls.push_back(std::move(decl));
    }
    return prog;
}

// диспетчеризация верхнего уровня

DeclPtr Parser::parse_top_level_decl() {
    switch (peek().kind) {
    case TokenKind::Fn:        return parse_fn_decl();
    case TokenKind::Struct:    return parse_struct_decl();
    case TokenKind::Impl:      return parse_impl_block();
    case TokenKind::Namespace: return parse_namespace_decl();
    case TokenKind::Type:      return parse_type_alias_decl();
    case TokenKind::Var:       return parse_var_decl();
    case TokenKind::Const:     return parse_const_decl();
    default:
        error(peek(), "expected top-level declaration");
        advance();
        return nullptr;
    }
}

// объявления

std::unique_ptr<FnDecl> Parser::parse_fn_decl() {
    auto loc = peek().loc;
    expect(TokenKind::Fn, "expected 'fn'");

    if (!check(TokenKind::Ident)) {
        error(peek(), "expected function name after 'fn'");
        return nullptr;
    }
    auto name = std::string(peek().lexeme);
    advance();

    expect(TokenKind::LParen, "expected '(' after function name");
    auto params = parse_param_list(/*allow_self=*/true);
    expect(TokenKind::RParen, "expected ')' after parameter list");

    expect(TokenKind::Arrow, "expected '->' before return type");
    auto ret_type = parse_type();
    if (!ret_type) return nullptr;

    auto effects = parse_effect_list();
    auto body    = parse_block_expr();

    return std::make_unique<FnDecl>(
        loc, std::move(name),
        std::move(params), std::move(ret_type),
        std::move(effects), std::move(body));
}

std::unique_ptr<StructDecl> Parser::parse_struct_decl() {
    auto loc = peek().loc;
    expect(TokenKind::Struct, "expected 'struct'");

    if (!check(TokenKind::Ident)) {
        error(peek(), "expected struct name");
        return nullptr;
    }
    auto name = std::string(peek().lexeme);
    advance();

    expect(TokenKind::LBrace, "expected '{' after struct name");

    std::vector<FieldDecl> fields;
    while (!at_end() && !check(TokenKind::RBrace)) {
        auto floc = peek().loc;
        if (!check(TokenKind::Ident)) {
            error(peek(), "expected field name");
            break;
        }
        auto fname = std::string(peek().lexeme);
        advance();

        expect(TokenKind::Colon, "expected ':' after field name");
        auto ftype = parse_type();
        if (!ftype) break;

        fields.emplace_back(floc, std::move(fname), std::move(ftype));

        if (!match(TokenKind::Comma)) break;
    }

    expect(TokenKind::RBrace, "expected '}'");
    return std::make_unique<StructDecl>(loc, std::move(name), std::move(fields));
}

std::unique_ptr<ImplBlock> Parser::parse_impl_block() {
    auto loc = peek().loc;
    expect(TokenKind::Impl, "expected 'impl'");

    if (!check(TokenKind::Ident)) {
        error(peek(), "expected type name after 'impl'");
        return nullptr;
    }
    auto type_name = std::string(peek().lexeme);
    advance();

    expect(TokenKind::LBrace, "expected '{'");

    std::vector<FnDecl> methods;
    while (!at_end() && !check(TokenKind::RBrace)) {
        if (!check(TokenKind::Fn)) {
            error(peek(), "expected 'fn' in impl block");
            advance();
            continue;
        }
        auto fn = parse_fn_decl();
        if (fn) methods.push_back(std::move(*fn));
    }

    expect(TokenKind::RBrace, "expected '}'");
    return std::make_unique<ImplBlock>(loc, std::move(type_name), std::move(methods));
}

std::unique_ptr<NamespaceDecl> Parser::parse_namespace_decl() {
    auto loc = peek().loc;
    expect(TokenKind::Namespace, "expected 'namespace'");

    if (!check(TokenKind::Ident)) {
        error(peek(), "expected namespace name");
        return nullptr;
    }
    auto name = std::string(peek().lexeme);
    advance();

    expect(TokenKind::LBrace, "expected '{'");

    std::vector<DeclPtr> decls;
    while (!at_end() && !check(TokenKind::RBrace)) {
        auto decl = parse_top_level_decl();
        if (decl) decls.push_back(std::move(decl));
    }

    expect(TokenKind::RBrace, "expected '}'");
    return std::make_unique<NamespaceDecl>(loc, std::move(name), std::move(decls));
}

std::unique_ptr<TypeAliasDecl> Parser::parse_type_alias_decl() {
    auto loc = peek().loc;
    expect(TokenKind::Type, "expected 'type'");

    if (!check(TokenKind::Ident)) {
        error(peek(), "expected type alias name");
        return nullptr;
    }
    auto name = std::string(peek().lexeme);
    advance();

    expect(TokenKind::Eq, "expected '='");
    auto target = parse_type();
    if (!target) return nullptr;
    expect(TokenKind::Semi, "expected ';'");

    return std::make_unique<TypeAliasDecl>(loc, std::move(name), std::move(target));
}

std::unique_ptr<VarDecl> Parser::parse_var_decl() {
    auto loc = peek().loc;
    expect(TokenKind::Var, "expected 'var'");

    if (!check(TokenKind::Ident)) {
        error(peek(), "expected variable name after 'var'");
        return nullptr;
    }
    auto name = std::string(peek().lexeme);
    advance();

    TypePtr type_ann;
    if (match(TokenKind::Colon)) {
        type_ann = parse_type();
    }

    expect(TokenKind::Eq, "variable declaration requires initializer ('=' expected)");
    auto init = parse_expr(); // настоящий разбор выражений
    expect(TokenKind::Semi, "expected ';'");

    return std::make_unique<VarDecl>(loc, std::move(name), std::move(type_ann), std::move(init));
}

std::unique_ptr<ConstDecl> Parser::parse_const_decl() {
    auto loc = peek().loc;
    expect(TokenKind::Const, "expected 'const'");

    if (!check(TokenKind::Ident)) {
        error(peek(), "expected constant name after 'const'");
        return nullptr;
    }
    auto name = std::string(peek().lexeme);
    advance();

    TypePtr type_ann;
    if (match(TokenKind::Colon)) {
        type_ann = parse_type();
    }

    expect(TokenKind::Eq, "expected '='");
    auto init = parse_expr(); // настоящий разбор выражений
    expect(TokenKind::Semi, "expected ';'");

    return std::make_unique<ConstDecl>(loc, std::move(name), std::move(type_ann), std::move(init));
}

// вспомогалки: параметры, эффекты, типы

std::vector<ParamDecl> Parser::parse_param_list(bool allow_self) {
    std::vector<ParamDecl> params;
    if (check(TokenKind::RParen)) return params;

    do {
        auto ploc = peek().loc;
        bool is_self = false;
        std::string pname;

        if (allow_self && check(TokenKind::Self)) {
            is_self = true;
            pname   = "self";
            advance();
        } else if (check(TokenKind::Ident)) {
            pname = std::string(peek().lexeme);
            advance();
        } else {
            error(peek(), "expected parameter name");
            break;
        }

        expect(TokenKind::Colon, "expected ':' after parameter name");
        auto ptype = parse_type();
        if (!ptype) break;

        params.emplace_back(ploc, std::move(pname), std::move(ptype), is_self);
    } while (match(TokenKind::Comma) && !check(TokenKind::RParen));

    return params;
}

std::vector<EffectKind> Parser::parse_effect_list() {
    std::vector<EffectKind> effects;
    while (true) {
        if (check(TokenKind::AtPure)) {
            effects.push_back(EffectKind::Pure);
            advance();
        } else if (check(TokenKind::AtIo)) {
            effects.push_back(EffectKind::Io);
            advance();
        } else if (check(TokenKind::AtPanics)) {
            effects.push_back(EffectKind::Panics);
            advance();
        } else if (check(TokenKind::AtUnknown)) {
            error(peek(), "unknown effect attribute");
            advance();
        } else {
            break;
        }
        match(TokenKind::Comma); // опциональная запятая между несколькими эффектами
    }
    return effects;
}

TypePtr Parser::parse_type() {
    auto loc = peek().loc;

    switch (peek().kind) {
    case TokenKind::Int8:    advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Int8);
    case TokenKind::Int16:   advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Int16);
    case TokenKind::Int32:   advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Int32);
    case TokenKind::Int64:   advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Int64);
    case TokenKind::Uint8:   advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Uint8);
    case TokenKind::Uint16:  advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Uint16);
    case TokenKind::Uint32:  advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Uint32);
    case TokenKind::Uint64:  advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Uint64);
    case TokenKind::Float32: advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Float32);
    case TokenKind::Float64: advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Float64);
    case TokenKind::KwBool:  advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::KwBool);
    case TokenKind::KwString:advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::KwString);
    case TokenKind::Hollow:  advance(); return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Hollow);

    case TokenKind::Array: {
        advance();
        expect(TokenKind::LBracket, "expected '[' after 'array'");
        auto elem = parse_type();
        if (!elem) return nullptr;
        expect(TokenKind::Comma, "expected ',' in array type");
        if (!check(TokenKind::IntLit)) {
            error(peek(), "expected integer size in array type");
            return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Hollow);
        }
        uint64_t size = std::get<lex::IntLiteralData>(peek().value).value;
        advance();
        expect(TokenKind::RBracket, "expected ']' after array size");
        return std::make_unique<ArrayTypeRef>(loc, std::move(elem), size);
    }

    case TokenKind::Range: {
        advance();
        expect(TokenKind::LBracket, "expected '[' after 'range'");
        auto elem = parse_type();
        if (!elem) return nullptr;
        expect(TokenKind::RBracket, "expected ']' in range type");
        return std::make_unique<RangeTypeRef>(loc, std::move(elem));
    }

    case TokenKind::Ident: {
        auto name = std::string(peek().lexeme);
        advance();
        if (check(TokenKind::ColonColon)) {
            advance();
            if (!check(TokenKind::Ident)) {
                error(peek(), "expected identifier after '::'");
                return std::make_unique<BuiltinTypeRef>(loc, TokenKind::Hollow);
            }
            auto member = std::string(peek().lexeme);
            advance();
            return std::make_unique<NamespacedTypeRef>(loc, std::move(name), std::move(member));
        }
        return std::make_unique<NamedTypeRef>(loc, std::move(name));
    }

    default:
        error(peek(), "expected type");
        return nullptr;
    }
}

// возвращает левый приоритет инфиксных операторов, -1 для неинфиксных
static int infix_bp(TokenKind k) {
    switch (k) {
    case TokenKind::PipeGt:   return 1;
    case TokenKind::PipePipe: return 2;
    case TokenKind::AmpAmp:   return 3;
    case TokenKind::EqEq:
    case TokenKind::BangEq:   return 4;
    case TokenKind::Lt:
    case TokenKind::Gt:
    case TokenKind::LtEq:
    case TokenKind::GtEq:     return 5;
    case TokenKind::DotDot:   return 6;
    case TokenKind::Plus:
    case TokenKind::Minus:    return 7;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:  return 8;
    default:                  return -1;
    }
}

ExprPtr Parser::parse_expr(int min_bp) {
    auto lhs = parse_unary_expr();
    if (!lhs) return nullptr;

    while (true) {
        int bp = infix_bp(peek().kind);
        if (bp < 0 || bp < min_bp) break;

        bool is_dotdot = (peek().kind == TokenKind::DotDot);
        auto loc = peek().loc;
        auto op_kind = peek().kind;
        advance(); // съедаем оператор

        // правая часть для лево-ассоциативных: min_bp = bp + 1 не дает переассоциировать
        auto rhs = parse_expr(bp + 1);
        if (!rhs) return nullptr;

        lhs = build_binary(loc, op_kind, std::move(lhs), std::move(rhs));

        // `..` не ассоциативен - после a..b запрещаем цепочку ..c
        if (is_dotdot) {
            if (check(TokenKind::DotDot)) {
                error(peek(), "'..' is not associative; parenthesise sub-ranges");
            }
            break;
        }
    }
    return lhs;
}

ExprPtr Parser::build_binary(diag::SourceLocation loc, TokenKind op,
                              ExprPtr lhs, ExprPtr rhs) {
    switch (op) {
    case TokenKind::PipeGt:   return std::make_unique<PipeExpr>(loc, std::move(lhs), std::move(rhs));
    case TokenKind::DotDot:   return std::make_unique<RangeExpr>(loc, std::move(lhs), std::move(rhs));
    case TokenKind::PipePipe: return std::make_unique<BinaryExpr>(loc, BinaryOp::Or,  std::move(lhs), std::move(rhs));
    case TokenKind::AmpAmp:   return std::make_unique<BinaryExpr>(loc, BinaryOp::And, std::move(lhs), std::move(rhs));
    case TokenKind::EqEq:     return std::make_unique<BinaryExpr>(loc, BinaryOp::Eq,  std::move(lhs), std::move(rhs));
    case TokenKind::BangEq:   return std::make_unique<BinaryExpr>(loc, BinaryOp::Ne,  std::move(lhs), std::move(rhs));
    case TokenKind::Lt:       return std::make_unique<BinaryExpr>(loc, BinaryOp::Lt,  std::move(lhs), std::move(rhs));
    case TokenKind::Gt:       return std::make_unique<BinaryExpr>(loc, BinaryOp::Gt,  std::move(lhs), std::move(rhs));
    case TokenKind::LtEq:     return std::make_unique<BinaryExpr>(loc, BinaryOp::Le,  std::move(lhs), std::move(rhs));
    case TokenKind::GtEq:     return std::make_unique<BinaryExpr>(loc, BinaryOp::Ge,  std::move(lhs), std::move(rhs));
    case TokenKind::Plus:     return std::make_unique<BinaryExpr>(loc, BinaryOp::Add, std::move(lhs), std::move(rhs));
    case TokenKind::Minus:    return std::make_unique<BinaryExpr>(loc, BinaryOp::Sub, std::move(lhs), std::move(rhs));
    case TokenKind::Star:     return std::make_unique<BinaryExpr>(loc, BinaryOp::Mul, std::move(lhs), std::move(rhs));
    case TokenKind::Slash:    return std::make_unique<BinaryExpr>(loc, BinaryOp::Div, std::move(lhs), std::move(rhs));
    case TokenKind::Percent:  return std::make_unique<BinaryExpr>(loc, BinaryOp::Rem, std::move(lhs), std::move(rhs));
    default:
        return lhs; // unreachable
    }
}

ExprPtr Parser::parse_unary_expr() {
    auto loc = peek().loc;

    if (match(TokenKind::Minus)) {
        auto operand = parse_unary_expr(); // право-ассоциативный
        if (!operand) return nullptr;
        return std::make_unique<UnaryExpr>(loc, UnaryOp::Neg, std::move(operand));
    }
    if (match(TokenKind::Bang)) {
        auto operand = parse_unary_expr();
        if (!operand) return nullptr;
        return std::make_unique<UnaryExpr>(loc, UnaryOp::Not, std::move(operand));
    }

    return parse_postfix_expr(parse_primary_expr());
}

ExprPtr Parser::parse_postfix_expr(ExprPtr lhs) {
    if (!lhs) return nullptr;

    while (true) {
        auto loc = peek().loc;

        if (check(TokenKind::LBracket)) {
            advance();
            auto idx = parse_expr();
            expect(TokenKind::RBracket, "expected ']'");
            lhs = std::make_unique<IndexExpr>(loc, std::move(lhs), std::move(idx));

        } else if (check(TokenKind::Dot)) {
            advance();
            if (!check(TokenKind::Ident)) {
                error(peek(), "expected field or method name after '.'");
                break;
            }
            auto fname = std::string(peek().lexeme);
            advance();

            if (check(TokenKind::LParen)) {
                advance();
                auto args = parse_arg_list();
                expect(TokenKind::RParen, "expected ')'");
                lhs = std::make_unique<MethodCallExpr>(loc, std::move(lhs), fname, std::move(args));
            } else {
                lhs = std::make_unique<FieldAccess>(loc, std::move(lhs), fname);
            }

        } else if (check(TokenKind::LParen)) {
            advance();
            auto args = parse_arg_list();
            expect(TokenKind::RParen, "expected ')'");
            lhs = std::make_unique<CallExpr>(loc, std::move(lhs), std::move(args));

        } else {
            break;
        }
    }
    return lhs;
}

std::vector<ExprPtr> Parser::parse_arg_list() {
    std::vector<ExprPtr> args;
    if (check(TokenKind::RParen)) return args;
    do {
        auto arg = parse_expr();
        if (arg) args.push_back(std::move(arg));
    } while (match(TokenKind::Comma) && !check(TokenKind::RParen));
    return args;
}

ExprPtr Parser::parse_primary_expr() {
    auto loc = peek().loc;

    switch (peek().kind) {

    // целочисленный литерал
    case TokenKind::IntLit: {
        auto data = std::get<lex::IntLiteralData>(peek().value);
        advance();
        return std::make_unique<IntLit>(loc, data);
    }

    // вещественный литерал
    case TokenKind::FloatLit: {
        auto data = std::get<lex::FloatLiteralData>(peek().value);
        advance();
        return std::make_unique<FloatLit>(loc, data);
    }

    // булевые литералы
    case TokenKind::True:  advance(); return std::make_unique<BoolLit>(loc, true);
    case TokenKind::False: advance(); return std::make_unique<BoolLit>(loc, false);

    // строковый литерал
    case TokenKind::StringLit: {
        auto val = std::get<std::string>(peek().value);
        advance();
        return std::make_unique<StringLit>(loc, std::move(val));
    }

    // self
    case TokenKind::Self:
        advance();
        return std::make_unique<SelfExpr>(loc);

    // выражение в скобках
    case TokenKind::LParen: {
        advance();
        auto e = parse_expr();
        expect(TokenKind::RParen, "expected ')' after expression");
        return e;
    }

    // массивный литерал: [expr, ...]
    case TokenKind::LBracket: {
        advance();
        if (check(TokenKind::RBracket)) {
            error(peek(), "empty array literal is not allowed");
            advance();
            return std::make_unique<ArrayLit>(loc, std::vector<ExprPtr>{});
        }
        std::vector<ExprPtr> elems;
        do {
            auto elem = parse_expr();
            if (elem) elems.push_back(std::move(elem));
        } while (match(TokenKind::Comma) && !check(TokenKind::RBracket));
        expect(TokenKind::RBracket, "expected ']'");
        return std::make_unique<ArrayLit>(loc, std::move(elems));
    }

    // cast<type>(expr): < и > здесь разделяют параметры типа
    case TokenKind::Cast: {
        advance();
        expect(TokenKind::Lt, "expected '<' after 'cast'");
        auto target_type = parse_type();
        expect(TokenKind::Gt, "expected '>' after cast type");
        expect(TokenKind::LParen, "expected '(' after cast type");
        auto operand = parse_expr();
        expect(TokenKind::RParen, "expected ')'");
        if (!target_type || !operand) return nullptr;
        return std::make_unique<CastExpr>(loc, std::move(target_type), std::move(operand));
    }

    // выражение if: из контекста выражения, else обязателен
    case TokenKind::If:
        return parse_if_expr();

    // выражение-блок
    case TokenKind::LBrace:
        return parse_block_expr();

    // идентификатор, обращение к неймспейсу или структурный литерал
    case TokenKind::Ident: {
        auto name = std::string(peek(0).lexeme);

        // шаблон NS::member
        if (peek(1).kind == TokenKind::ColonColon) {
            advance(); advance(); // consume ident and ::
            if (!check(TokenKind::Ident)) {
                error(peek(), "expected identifier after '::'");
                return std::make_unique<IdentExpr>(loc, name);
            }
            auto member = std::string(peek().lexeme);
            advance();
            return std::make_unique<NamespaceAccess>(loc, std::move(name), std::move(member));
        }

        // структурный литерал: Name { field: expr, ... }
        // отключен в контексте условия (if/while/for) из-за неоднозначности с блоком
        if (!no_struct_lit_ && peek(1).kind == TokenKind::LBrace) {
            auto& t2 = peek(2);
            auto& t3 = peek(3);
            bool looks_like_struct =
                (t2.kind == TokenKind::RBrace) || // пустой: Foo{}
                (t2.kind == TokenKind::Ident && t3.kind == TokenKind::Colon); // Foo{x: ...}
            if (looks_like_struct) {
                advance(); // consume ident
                return parse_struct_literal(loc, std::move(name));
            }
        }

        // просто идентификатор
        advance();
        return std::make_unique<IdentExpr>(loc, std::move(name));
    }

    default:
        error(peek(), "expected expression");
        return nullptr;
    }
}

// разбирает условие с отключенным распознаванием структурных литералов
static auto make_cond_guard(bool& flag) {
    struct Guard { bool& f; bool saved; ~Guard() { f = saved; } };
    Guard g{flag, flag};
    flag = true;
    return g;
}

// выражение if
ExprPtr Parser::parse_if_expr() {
    auto loc = peek().loc;
    expect(TokenKind::If, "expected 'if'");

    ExprPtr cond;
    { auto g = make_cond_guard(no_struct_lit_); cond = parse_expr(); }
    auto then_body = parse_block_expr();

    expect(TokenKind::Else, "expected 'else' in if expression");

    ExprPtr else_body;
    if (check(TokenKind::If)) {
        else_body = parse_if_expr();
    } else {
        else_body = parse_block_expr();
    }

    if (!cond || !then_body || !else_body) return nullptr;
    return std::make_unique<IfExpr>(loc, std::move(cond), std::move(then_body), std::move(else_body));
}

// выражение-блок
// грамматика: "{" { stmt } [ expr ] "}"
// ключевые слова -> parse_stmt(), остальные без ";" -> final_expr
ExprPtr Parser::parse_block_expr() {
    auto loc = peek().loc;
    expect(TokenKind::LBrace, "expected '{'");

    std::vector<StmtPtr> stmts;
    ExprPtr final_expr;

    while (!at_end() && !check(TokenKind::RBrace)) {
        // ключевые слова-начала инструкций идут в parse_stmt
        //  `if` на позиции инструкции -> IfStmt (else опционален)
        switch (peek().kind) {
        case TokenKind::Var:
        case TokenKind::Const:
        case TokenKind::If:
        case TokenKind::While:
        case TokenKind::For:
        case TokenKind::Return:
        case TokenKind::Break:
        case TokenKind::Continue:
        case TokenKind::Defer:
        case TokenKind::LBrace:
        case TokenKind::Semi: {
            auto stmt = parse_stmt();
            if (stmt) stmts.push_back(std::move(stmt));
            else      synchronize();
            continue;
        }
        default: break;
        }

        // позиция выражения: assign_stmt, expr_stmt или final_expr
        auto expr = parse_expr();
        if (!expr) { synchronize(); continue; }

        if (check(TokenKind::Eq)) {
            // assign_stmt: lvalue = expr ;
            advance();
            auto value = parse_expr();
            if (!value) { synchronize(); continue; }
            if (!expect(TokenKind::Semi, "expected ';' after assignment")) synchronize();
            stmts.push_back(std::make_unique<AssignStmt>(expr->loc, std::move(expr), std::move(value)));
        } else if (match(TokenKind::Semi)) {
            stmts.push_back(std::make_unique<ExprStmt>(expr->loc, std::move(expr)));
        } else {
            // нет ';' перед '}' (или конец файла) — хвостовое выражение блока
            final_expr = std::move(expr);
            break;
        }
    }

    expect(TokenKind::RBrace, "expected '}'");
    return std::make_unique<BlockExpr>(loc, std::move(stmts), std::move(final_expr));
}

// структурный литерал: Name { field: expr, ... }
// вызывается после того, как ident с именем типа уже съеден
ExprPtr Parser::parse_struct_literal(diag::SourceLocation loc, std::string type_name) {
    expect(TokenKind::LBrace, "expected '{'");

    std::vector<StructLitField> fields;
    while (!at_end() && !check(TokenKind::RBrace)) {
        auto floc = peek().loc;
        if (!check(TokenKind::Ident)) {
            error(peek(), "expected field name in struct literal");
            break;
        }
        auto fname = std::string(peek().lexeme);
        advance();
        expect(TokenKind::Colon, "expected ':' after field name");
        auto fval = parse_expr();
        if (fval) fields.push_back({std::move(fname), floc, std::move(fval)});
        if (!match(TokenKind::Comma)) break;
    }

    expect(TokenKind::RBrace, "expected '}'");
    return std::make_unique<StructLit>(loc, std::move(type_name), std::move(fields));
}

//разбор инструкций

// восстановление после паники: пропускаем до ближайшей точки синхронизации
// останавливается ПЕРЕД '}' и ключевыми словами; продвигается МИМО ';'
void Parser::synchronize() {
    while (!at_end()) {
        if (check(TokenKind::RBrace)) return;
        if (check(TokenKind::Semi))   { advance(); return; }
        switch (peek().kind) {
        case TokenKind::Fn:
        case TokenKind::Struct:
        case TokenKind::Namespace:
        case TokenKind::Impl:
        case TokenKind::Type:
        case TokenKind::Var:
        case TokenKind::Const:
        case TokenKind::If:
        case TokenKind::While:
        case TokenKind::For:
        case TokenKind::Return:
            return;
        default:
            advance();
        }
    }
}

StmtPtr Parser::parse_stmt() {
    auto loc = peek().loc;
    switch (peek().kind) {

    case TokenKind::Var: {
        auto decl = parse_var_decl();
        if (!decl) return nullptr;
        return std::make_unique<DeclStmt>(loc, std::move(decl));
    }
    case TokenKind::Const: {
        auto decl = parse_const_decl();
        if (!decl) return nullptr;
        return std::make_unique<DeclStmt>(loc, std::move(decl));
    }

    case TokenKind::If:       return parse_if_stmt();
    case TokenKind::While:    return parse_while_stmt();
    case TokenKind::For:      return parse_for_stmt();
    case TokenKind::Return:   return parse_return_stmt();
    case TokenKind::Defer:    return parse_defer_stmt();

    case TokenKind::Break:
        advance();
        expect(TokenKind::Semi, "expected ';' after 'break'");
        return std::make_unique<BreakStmt>(loc);

    case TokenKind::Continue:
        advance();
        expect(TokenKind::Semi, "expected ';' after 'continue'");
        return std::make_unique<ContinueStmt>(loc);

    case TokenKind::LBrace: {
        auto block = parse_block_expr();
        if (!block) return nullptr;
        return std::make_unique<BlockStmt>(loc, std::move(block));
    }

    case TokenKind::Semi:
        advance();
        return std::make_unique<EmptyStmt>(loc);

    default:
        return parse_assign_or_expr_stmt();
    }
}

// assign_stmt или expr_stmt - оба требуют ';' в конце
StmtPtr Parser::parse_assign_or_expr_stmt() {
    auto expr = parse_expr();
    if (!expr) { synchronize(); return nullptr; }
    auto loc = expr->loc;

    if (check(TokenKind::Eq)) {
        advance();
        auto value = parse_expr();
        if (!value) { synchronize(); return nullptr; }
        if (!expect(TokenKind::Semi, "expected ';' after assignment")) synchronize();
        return std::make_unique<AssignStmt>(loc, std::move(expr), std::move(value));
    }

    if (!expect(TokenKind::Semi, "expected ';' after expression")) synchronize();
    return std::make_unique<ExprStmt>(loc, std::move(expr));
}

// if_stmt (B.4: вызывается из позиции инструкции, else опционален)
StmtPtr Parser::parse_if_stmt() {
    auto loc = peek().loc;
    expect(TokenKind::If, "expected 'if'");

    ExprPtr cond;
    { auto g = make_cond_guard(no_struct_lit_); cond = parse_expr(); }
    if (!cond) { synchronize(); return nullptr; }

    auto then_body = parse_block_expr();
    if (!then_body) return nullptr;

    StmtPtr else_branch;
    if (match(TokenKind::Else)) {
        if (check(TokenKind::If)) {
            else_branch = parse_if_stmt();
        } else {
            auto eb = parse_block_expr();
            if (eb) else_branch = std::make_unique<BlockStmt>(eb->loc, std::move(eb));
        }
    }

    return std::make_unique<IfStmt>(loc, std::move(cond), std::move(then_body), std::move(else_branch));
}

StmtPtr Parser::parse_while_stmt() {
    auto loc = peek().loc;
    expect(TokenKind::While, "expected 'while'");

    ExprPtr cond;
    { auto g = make_cond_guard(no_struct_lit_); cond = parse_expr(); }
    if (!cond) { synchronize(); return nullptr; }

    auto body = parse_block_expr();
    if (!body) return nullptr;

    return std::make_unique<WhileStmt>(loc, std::move(cond), std::move(body));
}

StmtPtr Parser::parse_for_stmt() {
    auto loc = peek().loc;
    expect(TokenKind::For, "expected 'for'");

    if (!check(TokenKind::Ident)) {
        error(peek(), "expected loop variable name after 'for'");
        synchronize();
        return nullptr;
    }
    auto var_loc  = peek().loc;
    auto var_name = std::string(peek().lexeme);
    advance();

    expect(TokenKind::In, "expected 'in' after loop variable");

    ExprPtr range_expr;
    { auto g = make_cond_guard(no_struct_lit_); range_expr = parse_expr(); }
    if (!range_expr) { synchronize(); return nullptr; }

    auto body = parse_block_expr();
    if (!body) return nullptr;

    return std::make_unique<ForStmt>(loc, std::move(var_name), var_loc,
                                     std::move(range_expr), std::move(body));
}

StmtPtr Parser::parse_return_stmt() {
    auto loc = peek().loc;
    expect(TokenKind::Return, "expected 'return'");

    ExprPtr value;
    if (!check(TokenKind::Semi)) {
        value = parse_expr();
        if (!value) { synchronize(); return nullptr; }
    }

    expect(TokenKind::Semi, "expected ';' after return");
    return std::make_unique<ReturnStmt>(loc, std::move(value));
}

// defer_stmt ::= "defer" stmt - вложенная инструкция может быть любой
StmtPtr Parser::parse_defer_stmt() {
    auto loc = peek().loc;
    expect(TokenKind::Defer, "expected 'defer'");

    auto body = parse_stmt();
    if (!body) return nullptr;

    return std::make_unique<DeferStmt>(loc, std::move(body));
}

} // namespace mycc::parse
