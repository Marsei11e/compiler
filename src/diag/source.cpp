#include "source.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace mycc::diag {

std::vector<uint32_t> SourceManager::build_line_offsets(std::string_view src) {
    std::vector<uint32_t> offsets;
    offsets.push_back(0);
    for (uint32_t i = 0; i < static_cast<uint32_t>(src.size()); ++i) {
        if (src[i] == '\n') {
            offsets.push_back(i + 1);
        }
    }
    return offsets;
}

FileId SourceManager::load_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return kInvalidFileId;

    std::ostringstream buf;
    buf << ifs.rdbuf();

    SourceFile sf;
    sf.id           = static_cast<FileId>(files_.size() + 1);
    sf.path         = path;
    sf.contents     = buf.str();
    sf.line_offsets = build_line_offsets(sf.contents);
    files_.push_back(std::move(sf));
    return files_.back().id;
}

SourceLocation SourceManager::location_of(FileId fid, uint32_t offset) const {
    const SourceFile* sf = get_file(fid);
    if (!sf) return {kInvalidFileId, 0, 0};

    auto it = std::upper_bound(sf->line_offsets.begin(), sf->line_offsets.end(), offset);
    --it;
    uint32_t line = static_cast<uint32_t>(it - sf->line_offsets.begin()) + 1;
    uint32_t col  = offset - *it + 1;
    return {fid, line, col};
}

std::string SourceManager::loc_to_string(SourceLocation loc) const {
    const SourceFile* sf = get_file(loc.file_id);
    if (!sf) return "<unknown>";
    return sf->path + ':' + std::to_string(loc.line) + ':' + std::to_string(loc.col);
}

const SourceFile* SourceManager::get_file(FileId fid) const {
    if (fid == kInvalidFileId || fid > static_cast<FileId>(files_.size())) return nullptr;
    return &files_[fid - 1];
}

} 