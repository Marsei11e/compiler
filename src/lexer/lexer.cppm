module;

#include "lexer/_pod.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module mycc.lexer;

import mycc.diag;

export namespace mycc::lex {

using ::mycc::lex::TokenKind;
using ::mycc::lex::IntSuffix;
using ::mycc::lex::IntLiteralData;
using ::mycc::lex::FloatLiteralData;
using ::mycc::lex::TokenLiteral;
using ::mycc::lex::Token;

// читаемое имя токена, используется при --dump-tokens и в диагностике
std::string_view token_kind_name(TokenKind k);

class Lexer {
public:
    Lexer(std::string_view source,
          diag::FileId      file_id,
          diag::DiagnosticEngine& diag);

    // токенизирует весь исходник, в конце всегда добавляет Eof
    std::vector<Token> tokenize();

private:
    std::string_view        source_;
    diag::FileId            file_id_;
    diag::DiagnosticEngine& diag_;

    std::size_t pos_{0};
    uint32_t    line_{1};
    uint32_t    col_{1};

    // вспомогательные методы
    bool        at_end(std::size_t offset = 0) const;
    char        peek(std::size_t offset = 0) const;
    char        advance(); // съедает символ, обновляет строку/столбец
    bool        match(char expected); // съедает если совпадает
    diag::SourceLocation current_loc() const;

    // под-лексеры
    Token lex_one();
    Token lex_number(std::size_t start, diag::SourceLocation start_loc);
    Token lex_string(std::size_t start, diag::SourceLocation start_loc);
    Token lex_ident_or_kw(std::size_t start, diag::SourceLocation start_loc);
    Token lex_effect_attr(std::size_t start, diag::SourceLocation start_loc);
    void  skip_line_comment();

    // создание токенов
    Token make_tok(TokenKind kind,
                   diag::SourceLocation loc,
                   std::size_t start,
                   std::size_t end,
                   TokenLiteral val = {}) const;

    void error(diag::SourceLocation loc, std::string msg);
};

} // export namespace mycc::lex
