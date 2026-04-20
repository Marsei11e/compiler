#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mycc::diag {

using FileId = uint32_t;
static constexpr FileId kInvalidFileId = 0;

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

} 
