module;

#include <ostream>
#include <string>
#include <string_view>
#include <utility>

module mycc.diag;

namespace mycc::diag {

void DiagnosticEngine::report(Diagnostic d) {
    if (d.severity == Severity::Error) {
        ++error_count_;
        if (error_count_ > kMaxErrors) {
            ++suppressed_count_;
            return;
        }
    }
    diags_.push_back(std::move(d));
}

std::string_view DiagnosticEngine::severity_label(Severity s) {
    switch (s) {
        case Severity::Error:   return "error";
        case Severity::Warning: return "warning";
        case Severity::Note:    return "note";
    }
    return "error";
}

void DiagnosticEngine::emit_all(std::ostream& out) const {
    for (const Diagnostic& d : diags_) {
        out << sm_.loc_to_string(d.location)
            << ": " << severity_label(d.severity)
            << ": " << d.message << '\n';
    }
    if (suppressed_count_ > 0) {
        out << "... " << suppressed_count_ << " more errors suppressed\n";
    }
}

} // namespace mycc::diag
