module;

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module mycc.lexer;

import mycc.diag;

export namespace mycc::lex {

enum class TokenKind {
    // литералы
    IntLit,
    FloatLit,
    StringLit,
    CharLit,

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
    KwChar, // 'char'

    // идентификатор
    Ident,

    // атрибуты эффектов
    AtPure, // @pure
    AtIo, // @io
    AtPanics, // @panics
    AtUnknown, // @<что угодно еще> - лексер ругается, парсер может отвергнуть

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

// данные литеральных значений

enum class IntSuffix { None, U, L, UL };

struct IntLiteralData {
    uint64_t value{0};
    IntSuffix suffix{IntSuffix::None};
    bool is_hex{false};
};

struct FloatLiteralData {
    double value{0.0};
    bool is_f32{false}; // true если есть суффикс 'f' - это float32
};

struct CharLiteralData {
    uint32_t codepoint{0}; // Unicode scalar value (char у нас - 32-битный кодпойнт)
};

// хранит декодированное значение, для нелитеральных токенов - monostate
using TokenLiteral = std::variant<std::monostate, IntLiteralData, FloatLiteralData,
                                  CharLiteralData, std::string>;

// токен

struct Token {
    TokenKind        kind{TokenKind::Error};
    diag::SourceLocation loc{};
    std::string_view lexeme{}; // срез исходного буфера
    TokenLiteral     value{}; // раскодированное значение (в строках эскейпы уже раскрыты)
};

// читаемое имя токена, используется при --dump-tokens и в диагностике
std::string_view token_kind_name(TokenKind k);

class Lexer {
public:
    Lexer(std::string_view source,
          diag::FileId file_id,
          diag::DiagnosticEngine& diag);

    // токенизирует весь исходник, в конце всегда добавляет Eof
    std::vector<Token> tokenize();

private:
    std::string_view    source_;
    diag::FileId    file_id_;
    diag::DiagnosticEngine& diag_;

    std::size_t pos_{0};
    uint32_t    line_{1};
    uint32_t    col_{1};

    // вспомогательные методы
    bool    at_end(std::size_t offset = 0) const;
    char    peek(std::size_t offset = 0) const;
    char    advance(); // съедает символ, обновляет строку/столбец
    bool    match(char expected); // съедает если совпадает
    diag::SourceLocation current_loc() const;

    // под-лексеры
    Token lex_one();
    Token lex_number(std::size_t start, diag::SourceLocation start_loc);
    Token lex_string(std::size_t start, diag::SourceLocation start_loc);
    Token lex_char(std::size_t start, diag::SourceLocation start_loc);
    Token lex_ident_or_kw(std::size_t start, diag::SourceLocation start_loc);
    Token lex_effect_attr(std::size_t start, diag::SourceLocation start_loc);
    void  skip_line_comment();

    // создание токенов
    Token make_tok(TokenKind kind, diag::SourceLocation loc, std::size_t start, std::size_t end,
    TokenLiteral val = {}) const;
    void error(diag::SourceLocation loc, std::string msg);
};

} // export namespace mycc::lex
