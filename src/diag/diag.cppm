module;

#include "diag/_pod.h"

export module mycc.diag;

export namespace mycc::diag {
    using ::mycc::diag::FileId;
    using ::mycc::diag::kInvalidFileId;
    using ::mycc::diag::SourceLocation;
    using ::mycc::diag::SourceFile;
    using ::mycc::diag::SourceManager;
    using ::mycc::diag::Severity;
    using ::mycc::diag::Diagnostic;
    using ::mycc::diag::DiagnosticEngine;
}
