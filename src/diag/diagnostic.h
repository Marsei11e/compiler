/* движок диагностики - собирает и выводит ошибки компилятора */
#pragma once

#include "source.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace mycc::diag {

enum class Severity { Error, Warning, Note };

struct Diagnostic {
    Severity       severity;
    SourceLocation location;
    std::string    message;
};

/* накапливает диагностику и выводит её, кап — kMaxErrors ошибок */
class DiagnosticEngine {
public:
    static constexpr int kMaxErrors = 20;

    explicit DiagnosticEngine(const SourceManager& sm) : sm_(sm) {}

    void report(Diagnostic d);

    int  error_count()   const { return error_count_; }
    bool has_errors()    const { return error_count_ > 0; }

    // выводит все собранные диагностики в `out`, вызывать один раз
    void emit_all(std::ostream& out) const;

private:
    const SourceManager&    sm_;
    std::vector<Diagnostic> diags_;
    int                     error_count_     {0};
    int                     suppressed_count_{0};

    static std::string_view severity_label(Severity s);
};

} // namespace mycc::diag
