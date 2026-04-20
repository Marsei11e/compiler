#include "lexer.h"

#include <cassert>
#include <charconv>
#include <cmath>
#include <string>
#include <unordered_map>

namespace mycc::lex {

// таблица ключевых слов

static const std::unordered_map<std::string_view, TokenKind>& keyword_table() {
    static const std::unordered_map<std::string_view, TokenKind> kw {
        {"fn",        TokenKind::Fn},
        {"var",       TokenKind::Var},
        {"const",     TokenKind::Const},
        {"return",    TokenKind::Return},
        {"if",        TokenKind::If},
        {"else",      TokenKind::Else},
        {"while",     TokenKind::While},
        {"for",       TokenKind::For},
        {"in",        TokenKind::In},
        {"break",     TokenKind::Break},
        {"continue",  TokenKind::Continue},
        {"struct",    TokenKind::Struct},
        {"impl",      TokenKind::Impl},
        {"self",      TokenKind::Self},
        {"namespace", TokenKind::Namespace},
        {"type",      TokenKind::Type},
        {"cast",      TokenKind::Cast},
        {"true",      TokenKind::True},
        {"false",     TokenKind::False},
        {"hollow",    TokenKind::Hollow},
        {"defer",     TokenKind::Defer},
        // конструкторы типов (B.1)
        {"array",     TokenKind::Array},
        {"range",     TokenKind::Range},
        // встроенные типы (B.2)
        {"int8",      TokenKind::Int8},
        {"int16",     TokenKind::Int16},
        {"int32",     TokenKind::Int32},
        {"int64",     TokenKind::Int64},
        {"uint8",     TokenKind::Uint8},
        {"uint16",    TokenKind::Uint16},
        {"uint32",    TokenKind::Uint32},
        {"uint64",    TokenKind::Uint64},
        {"float32",   TokenKind::Float32},
        {"float64",   TokenKind::Float64},
        {"bool",      TokenKind::KwBool},
        {"string",    TokenKind::KwString},
    };
    return kw;
}

// token_kind_name

std::string_view token_kind_name(TokenKind k) {
    switch (k) {
    case TokenKind::IntLit:      return "int_lit";
    case TokenKind::FloatLit:    return "float_lit";
    case TokenKind::StringLit:   return "string_lit";
    case TokenKind::Fn:          return "fn";
    case TokenKind::Var:         return "var";
    case TokenKind::Const:       return "const";
    case TokenKind::Return:      return "return";
    case TokenKind::If:          return "if";
    case TokenKind::Else:        return "else";
    case TokenKind::While:       return "while";
    case TokenKind::For:         return "for";
    case TokenKind::In:          return "in";
    case TokenKind::Break:       return "break";
    case TokenKind::Continue:    return "continue";
    case TokenKind::Struct:      return "struct";
    case TokenKind::Impl:        return "impl";
    case TokenKind::Self:        return "self";
    case TokenKind::Namespace:   return "namespace";
    case TokenKind::Type:        return "type";
    case TokenKind::Cast:        return "cast";
    case TokenKind::True:        return "true";
    case TokenKind::False:       return "false";
    case TokenKind::Hollow:      return "hollow";
    case TokenKind::Defer:       return "defer";
    case TokenKind::Array:       return "array";
    case TokenKind::Range:       return "range";
    case TokenKind::Int8:        return "int8";
    case TokenKind::Int16:       return "int16";
    case TokenKind::Int32:       return "int32";
    case TokenKind::Int64:       return "int64";
    case TokenKind::Uint8:       return "uint8";
    case TokenKind::Uint16:      return "uint16";
    case TokenKind::Uint32:      return "uint32";
    case TokenKind::Uint64:      return "uint64";
    case TokenKind::Float32:     return "float32";
    case TokenKind::Float64:     return "float64";
    case TokenKind::KwBool:      return "bool";
    case TokenKind::KwString:    return "string";
    case TokenKind::Ident:       return "ident";
    case TokenKind::AtPure:      return "@pure";
    case TokenKind::AtIo:        return "@io";
    case TokenKind::AtPanics:    return "@panics";
    case TokenKind::AtUnknown:   return "@?";
    case TokenKind::EqEq:        return "==";
    case TokenKind::BangEq:      return "!=";
    case TokenKind::LtEq:        return "<=";
    case TokenKind::GtEq:        return ">=";
    case TokenKind::AmpAmp:      return "&&";
    case TokenKind::PipePipe:    return "||";
    case TokenKind::PipeGt:      return "|>";
    case TokenKind::DotDot:      return "..";
    case TokenKind::Arrow:       return "->";
    case TokenKind::ColonColon:  return "::";
    case TokenKind::Plus:        return "+";
    case TokenKind::Minus:       return "-";
    case TokenKind::Star:        return "*";
    case TokenKind::Slash:       return "/";
    case TokenKind::Percent:     return "%";
    case TokenKind::Lt:          return "<";
    case TokenKind::Gt:          return ">";
    case TokenKind::Eq:          return "=";
    case TokenKind::Bang:        return "!";
    case TokenKind::Pipe:        return "|";
    case TokenKind::Amp:         return "&";
    case TokenKind::Dot:         return ".";
    case TokenKind::Comma:       return ",";
    case TokenKind::Colon:       return ":";
    case TokenKind::Semi:        return ";";
    case TokenKind::LParen:      return "(";
    case TokenKind::RParen:      return ")";
    case TokenKind::LBrace:      return "{";
    case TokenKind::RBrace:      return "}";
    case TokenKind::LBracket:    return "[";
    case TokenKind::RBracket:    return "]";
    case TokenKind::Eof:         return "eof";
    case TokenKind::Error:       return "error";
    }
    return "?";
}

// конструктор лексера

Lexer::Lexer(std::string_view source,
             diag::FileId      file_id,
             diag::DiagnosticEngine& diag)
    : source_(source), file_id_(file_id), diag_(diag) {}

// вспомогалки

bool Lexer::at_end(std::size_t offset) const {
    return pos_ + offset >= source_.size();
}

char Lexer::peek(std::size_t offset) const {
    if (at_end(offset)) return '\0';
    return source_[pos_ + offset];
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    else           { ++col_; }
    return c;
}

bool Lexer::match(char expected) {
    if (at_end() || source_[pos_] != expected) return false;
    advance();
    return true;
}

diag::SourceLocation Lexer::current_loc() const {
    return {file_id_, line_, col_};
}

Token Lexer::make_tok(TokenKind kind,
                      diag::SourceLocation loc,
                      std::size_t start,
                      std::size_t end,
                      TokenLiteral val) const {
    return Token{kind, loc, source_.substr(start, end - start), std::move(val)};
}

void Lexer::error(diag::SourceLocation loc, std::string msg) {
    diag_.report({diag::Severity::Error, loc, std::move(msg)});
}

void Lexer::skip_line_comment() {
    // '--' уже съели, пропускаем до конца строки (не включительно)
    while (!at_end() && peek() != '\n') advance();
}

// лексинг чисел

Token Lexer::lex_number(std::size_t start, diag::SourceLocation start_loc) {
    bool is_hex = false;

    // шестнадцатеричный префикс?
    if (peek(-1) == '0' && (peek() == 'x' || peek() == 'X')) {
        advance(); // съедаем 'x'/'X'
        is_hex = true;
        if (at_end() || !std::isxdigit(static_cast<unsigned char>(peek()))) {
            error(current_loc(), "invalid hex literal: no digits after '0x'");
            return make_tok(TokenKind::Error, start_loc, start, pos_);
        }
        while (!at_end() && std::isxdigit(static_cast<unsigned char>(peek()))) advance();
    } else {
        // десятичные цифры уже частично съедены (первая цифра — в вызывающем)
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }

    // может быть вещественным — проверяем '.' с цифрой или экспоненту 'e'/'E'
    bool is_float = false;
    if (!is_hex) {
        if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
            is_float = true;
            advance(); // съедаем '.'
            while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }
        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            advance();
            if (peek() == '+' || peek() == '-') advance();
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                error(current_loc(), "invalid float literal: expected digits after exponent");
                return make_tok(TokenKind::Error, start_loc, start, pos_);
            }
            while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }
    }

    if (is_float) {
        bool is_f32 = false;
        if (peek() == 'f') { is_f32 = true; advance(); }
        std::string_view lexeme = source_.substr(start, pos_ - start);
        // парсим значение из числовой части (без суффикса 'f')
        std::string_view num_part = is_f32 ? lexeme.substr(0, lexeme.size() - 1) : lexeme;
        double val = 0.0;
        // std::from_chars для float требует C++17, используем stod для совместимости
        try {
            val = std::stod(std::string(num_part));
        } catch (...) {
            error(start_loc, "invalid float literal");
            return make_tok(TokenKind::Error, start_loc, start, pos_);
        }
        return make_tok(TokenKind::FloatLit, start_loc, start, pos_,
                        FloatLiteralData{val, is_f32});
    }

    // суффикс целочисленного литерала
    IntSuffix suffix = IntSuffix::None;
    if (peek() == 'u' || peek() == 'U') {
        advance();
        if (peek() == 'L') { advance(); suffix = IntSuffix::UL; }
        else                { suffix = IntSuffix::U; }
    } else if (peek() == 'L') {
        advance();
        if (peek() == 'u' || peek() == 'U') { advance(); suffix = IntSuffix::UL; }
        else                                  { suffix = IntSuffix::L; }
    }

    std::string_view raw = source_.substr(start, pos_ - start);
    // убираем суффикс, оставляем числовую часть
    std::size_t num_len = raw.size();
    if (suffix == IntSuffix::UL) num_len -= 2;
    else if (suffix != IntSuffix::None) num_len -= 1;
    std::string_view num_part = raw.substr(0, num_len);

    uint64_t val = 0;
    if (is_hex) {
        // пропускаем '0x'
        auto [ptr, ec] = std::from_chars(num_part.data() + 2, num_part.data() + num_part.size(),
                                         val, 16);
        if (ec != std::errc{}) {
            error(start_loc, "invalid hex literal");
            return make_tok(TokenKind::Error, start_loc, start, pos_);
        }
    } else {
        auto [ptr, ec] = std::from_chars(num_part.data(), num_part.data() + num_part.size(),
                                         val, 10);
        if (ec != std::errc{}) {
            error(start_loc, "integer literal out of range");
            return make_tok(TokenKind::Error, start_loc, start, pos_);
        }
    }
    return make_tok(TokenKind::IntLit, start_loc, start, pos_,
                    IntLiteralData{val, suffix, is_hex});
}

// лексинг строк

Token Lexer::lex_string(std::size_t start, diag::SourceLocation start_loc) {
    // открывающая '"' уже съедена
    std::string decoded;
    while (true) {
        if (at_end() || peek() == '\n') {
            error(start_loc, "unterminated string literal");
            return make_tok(TokenKind::Error, start_loc, start, pos_);
        }
        char c = advance();
        if (c == '"') break;
        if (c == '\\') {
            if (at_end()) {
                error(current_loc(), "unterminated escape sequence");
                return make_tok(TokenKind::Error, start_loc, start, pos_);
            }
            char esc = advance();
            switch (esc) {
            case '"':  decoded += '"';  break;
            case '\\': decoded += '\\'; break;
            case 'n':  decoded += '\n'; break;
            case 't':  decoded += '\t'; break;
            case 'r':  decoded += '\r'; break;
            case '0':  decoded += '\0'; break;
            default:
                error(current_loc(),
                      std::string("unknown escape sequence '\\") + esc + "'");
                return make_tok(TokenKind::Error, start_loc, start, pos_);
            }
        } else {
            decoded += c;
        }
    }
    return make_tok(TokenKind::StringLit, start_loc, start, pos_, std::move(decoded));
}

// идентификатор или ключевое слово

Token Lexer::lex_ident_or_kw(std::size_t start, diag::SourceLocation start_loc) {
    while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
        advance();
    std::string_view text = source_.substr(start, pos_ - start);
    auto it = keyword_table().find(text);
    TokenKind kind = (it != keyword_table().end()) ? it->second : TokenKind::Ident;
    return make_tok(kind, start_loc, start, pos_);
}

// атрибут эффекта

Token Lexer::lex_effect_attr(std::size_t start, diag::SourceLocation start_loc) {
    // '@' уже съели, читаем идентификатор
    std::size_t id_start = pos_;
    if (at_end() || !(std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_')) {
        error(start_loc, "expected identifier after '@'");
        return make_tok(TokenKind::Error, start_loc, start, pos_);
    }
    while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
        advance();
    std::string_view name = source_.substr(id_start, pos_ - id_start);
    TokenKind kind;
    if (name == "pure")   kind = TokenKind::AtPure;
    else if (name == "io")     kind = TokenKind::AtIo;
    else if (name == "panics") kind = TokenKind::AtPanics;
    else {
        diag_.report({diag::Severity::Warning, start_loc,
                      std::string("unknown effect attribute '@") + std::string(name) + "'"});
        kind = TokenKind::AtUnknown;
    }
    return make_tok(kind, start_loc, start, pos_);
}

// главный цикл лексинга

Token Lexer::lex_one() {
    // пропускаем пробелы
    while (!at_end() && std::isspace(static_cast<unsigned char>(peek()))) advance();
    if (at_end()) return make_tok(TokenKind::Eof, current_loc(), pos_, pos_);

    std::size_t start = pos_;
    diag::SourceLocation loc = current_loc();
    char c = advance();

    switch (c) {
    // однозначные односимвольные
    case '+': return make_tok(TokenKind::Plus,     loc, start, pos_);
    case '*': return make_tok(TokenKind::Star,     loc, start, pos_);
    case '%': return make_tok(TokenKind::Percent,  loc, start, pos_);
    case ',': return make_tok(TokenKind::Comma,    loc, start, pos_);
    case ';': return make_tok(TokenKind::Semi,     loc, start, pos_);
    case '(': return make_tok(TokenKind::LParen,   loc, start, pos_);
    case ')': return make_tok(TokenKind::RParen,   loc, start, pos_);
    case '{': return make_tok(TokenKind::LBrace,   loc, start, pos_);
    case '}': return make_tok(TokenKind::RBrace,   loc, start, pos_);
    case '[': return make_tok(TokenKind::LBracket, loc, start, pos_);
    case ']': return make_tok(TokenKind::RBracket, loc, start, pos_);

    // многосимвольные: - -> --комментарий
    case '-':
        if (peek() == '>') { advance(); return make_tok(TokenKind::Arrow,   loc, start, pos_); }
        if (peek() == '-') { advance(); skip_line_comment(); return lex_one(); }
        return make_tok(TokenKind::Minus, loc, start, pos_);

    // /
    case '/': return make_tok(TokenKind::Slash, loc, start, pos_);

    // = ==
    case '=':
        if (match('=')) return make_tok(TokenKind::EqEq,  loc, start, pos_);
        return make_tok(TokenKind::Eq, loc, start, pos_);

    // ! !=
    case '!':
        if (match('=')) return make_tok(TokenKind::BangEq, loc, start, pos_);
        return make_tok(TokenKind::Bang, loc, start, pos_);

    // < <=
    case '<':
        if (match('=')) return make_tok(TokenKind::LtEq, loc, start, pos_);
        return make_tok(TokenKind::Lt, loc, start, pos_);

    // > >=
    case '>':
        if (match('=')) return make_tok(TokenKind::GtEq, loc, start, pos_);
        return make_tok(TokenKind::Gt, loc, start, pos_);

    // & &&
    case '&':
        if (match('&')) return make_tok(TokenKind::AmpAmp, loc, start, pos_);
        return make_tok(TokenKind::Amp, loc, start, pos_);

    // | || |>
    case '|':
        if (match('|')) return make_tok(TokenKind::PipePipe, loc, start, pos_);
        if (match('>')) return make_tok(TokenKind::PipeGt,   loc, start, pos_);
        return make_tok(TokenKind::Pipe, loc, start, pos_);

    // . ..
    case '.':
        if (match('.')) return make_tok(TokenKind::DotDot, loc, start, pos_);
        return make_tok(TokenKind::Dot, loc, start, pos_);

    // : ::
    case ':':
        if (match(':')) return make_tok(TokenKind::ColonColon, loc, start, pos_);
        return make_tok(TokenKind::Colon, loc, start, pos_);

    // @ атрибут эффекта
    case '@': return lex_effect_attr(start, loc);

    // строка
    case '"': return lex_string(start, loc);

    // число
    default:
        if (std::isdigit(static_cast<unsigned char>(c)))
            return lex_number(start, loc);
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            return lex_ident_or_kw(start, loc);

        error(loc, std::string("unexpected character '") + c + "'");
        return make_tok(TokenKind::Error, loc, start, pos_);
    }
}

// токенизация

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token tok = lex_one();
        // ошибочные токены в поток не добавляем, они уже залогированы
        if (tok.kind != TokenKind::Error)
            tokens.push_back(tok);
        if (tok.kind == TokenKind::Eof) break;
    }
    return tokens;
}

} // namespace mycc::lex