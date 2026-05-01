#pragma once

#include "diag/source.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace mycc::lex {

enum class TokenKind {
    // литералы
    IntLit,
    FloatLit,
    StringLit,

    // ключевые слова
    Fn, Var, Const, Return,
    If, Else,
    While, For, In,
    Break, Continue,
    Struct, Impl, Self,
    Namespace,
    Type, // псевдоним типа
    Cast,
    True, False, // булевы литералы, они же ключевые слова
    Hollow,
    Defer,

    // конструкторы типов
    Array, Range,

    // встроенные типы 
    Int8, Int16, Int32, Int64,
    Uint8, Uint16, Uint32, Uint64,
    Float32, Float64,
    KwBool, // 'bool' (чтобы не пересекаться с C++ bool)
    KwString, // 'string'

    // идентификатор
    Ident,

    // атрибуты эффектов
    AtPure, // @pure
    AtIo, // @io
    AtPanics, // @panics
    AtUnknown, // @<что угодно еще> — лексер ругается, парсер может отвергнуть

    // многосимвольные операторы
    EqEq, // ==
    BangEq, // !=
    LtEq, // <=
    GtEq, // >=
    AmpAmp, // &&
    PipePipe, // ||
    PipeGt, // |>
    DotDot, // ..
    Arrow, // ->
    ColonColon, // ::

    // односимвольные операторы и пунктуация
    Plus, // +
    Minus, // -
    Star, // *
    Slash, // /
    Percent, // %
    Lt, // <
    Gt, // >
    Eq, // =
    Bang, // !
    Pipe, // | (bare; парсер отвергает)
    Amp, // & (bare; парсер отвергает)
    Dot, // .
    Comma, // ,
    Colon, // :
    Semi, // ;
    LParen, // (
    RParen, // )
    LBrace, // {
    RBrace, // }
    LBracket, // [
    RBracket, // ]

    // специальные
    Eof,
    Error,
};

// читаемое имя токена, используется при --dump-tokens и в диагностике
std::string_view token_kind_name(TokenKind k);

// данные литеральных значений

enum class IntSuffix { None, U, L, UL };

struct IntLiteralData {
    uint64_t value{0};
    IntSuffix suffix{IntSuffix::None};
    bool is_hex{false};
};

struct FloatLiteralData {
    double value{0.0};
    bool is_f32{false}; // true если есть суффикс 'f' — это float32
};

// хранит декодированное значение, для нелитеральных токенов — monostate
using TokenLiteral = std::variant<std::monostate, IntLiteralData, FloatLiteralData, std::string>;

// токен

struct Token {
    TokenKind        kind{TokenKind::Error};
    diag::SourceLocation loc{};
    std::string_view lexeme{}; // срез исходного буфера
    TokenLiteral     value{}; // раскодированное значение (в строках эскейпы уже раскрыты)
};

} // namespace mycc::lex
