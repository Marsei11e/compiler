module;

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

export module mycc.diag;

export namespace mycc::diag {

using FileId = uint32_t;
inline constexpr FileId kInvalidFileId = 0;

struct SourceLocation {
    FileId   file_id{kInvalidFileId};
    uint32_t line{0};
    uint32_t col{0};
};

struct SourceFile {
    FileId      id;
    std::string path;
    std::string contents;
    std::vector<uint32_t> line_offsets;
};

class SourceManager {
public:
    FileId load_file(const std::string& path);
    SourceLocation location_of(FileId fid, uint32_t offset) const;
    std::string loc_to_string(SourceLocation loc) const;
    const SourceFile* get_file(FileId fid) const;

private:
    std::vector<SourceFile> files_;

    static std::vector<uint32_t> build_line_offsets(std::string_view src);
};

enum class Severity { Error, Warning, Note };

struct Diagnostic {
    Severity       severity;
    SourceLocation location;
    std::string    message;
};

class DiagnosticEngine {
public:
    static constexpr int kMaxErrors = 20;

    explicit DiagnosticEngine(const SourceManager& sm) : sm_(sm) {}

    void report(Diagnostic d);

    int  error_count() const { return error_count_; }
    bool has_errors()  const { return error_count_ > 0; }

    void emit_all(std::ostream& out) const;

private:
    const SourceManager&    sm_;
    std::vector<Diagnostic> diags_;
    int error_count_     {0};
    int suppressed_count_{0};

    static std::string_view severity_label(Severity s);
};

} // export namespace mycc::diag
