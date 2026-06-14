// отображение типов языка L в типы LLVM IR - codegen.md §3
module;

#include <string>

module mycc.codegen;

import mycc.sema;

namespace mycc::cg {

std::string lower_type(sema::TypeId id, const sema::TypeInterner& types,
                       bool storage) {
    const auto& td = types.get(id);
    switch (td.kind) {
        case sema::TypeKind::I8:  case sema::TypeKind::U8:  return "i8";
        case sema::TypeKind::I16: case sema::TypeKind::U16: return "i16";
        case sema::TypeKind::I32: case sema::TypeKind::U32: return "i32";
        case sema::TypeKind::I64: case sema::TypeKind::U64: return "i64";
        case sema::TypeKind::F32: return "float";
        case sema::TypeKind::F64: return "double";
        case sema::TypeKind::Bool: return storage ? "i8" : "i1";
        case sema::TypeKind::Char: return "i32"; // 32-битный Unicode-кодпойнт
        case sema::TypeKind::Hollow: return "void";
        case sema::TypeKind::String: return "%string";
        case sema::TypeKind::Array:
            // элемент массива живёт в памяти -> storage-форма
            return "[" + std::to_string(td.array_size) + " x "
                 + lower_type(td.elem, types, /*storage=*/true) + "]";
        case sema::TypeKind::Range: {
            // пара (current, end) - оба поля в storage-форме
            std::string e = lower_type(td.elem, types, /*storage=*/true);
            return "{ " + e + ", " + e + " }";
        }
        case sema::TypeKind::Struct:
            return "%" + std::string(types.display_name(id));
    }
    return "void";
}

std::string string_type_def() {
    return "%string = type { ptr, i64 }";
}

} // namespace mycc::cg
