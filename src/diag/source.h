/* source manager — хранит файлы и переводит байтовые смещения в строку/столбец */
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
    uint32_t line{0}; // с единицы
    uint32_t col{0}; // с единицы
};

struct SourceFile {
    FileId      id;
    std::string path;
    std::string contents;
    std::vector<uint32_t> line_offsets; // байтовое смещение первого символа каждой строки
};

/* владеет загруженными файлами, отдает стабильные указатели */
class SourceManager {
public:
    // загружает файл с диска, возвращает kInvalidFileId при ошибке
    FileId load_file(const std::string& path);

    // строит SourceLocation из байтового смещения внутри файла
    SourceLocation location_of(FileId fid, uint32_t offset) const;

    // возвращает строку "path:line:col" или "<unknown>" для невалидной позиции
    std::string loc_to_string(SourceLocation loc) const;

    const SourceFile* get_file(FileId fid) const;

private:
    std::vector<SourceFile> files_; // индекс 0 не используется (kInvalidFileId = 0)

    static std::vector<uint32_t> build_line_offsets(std::string_view src);
};

} // namespace mycc::diag
