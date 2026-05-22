// mangling имён функций - codegen.md §5
module;

#include <span>
#include <string>
#include <string_view>

module mycc.codegen;

import mycc.sema;

namespace mycc::cg {

namespace {

// сегмент "<длина><имя>" в стиле Itanium ABI: "Point" -> "5Point"
std::string len_name(std::string_view s) {
    return std::to_string(s.size()) + std::string(s);
}

// сокращённый код типа параметра - §5.2
std::string type_code(sema::TypeId id, const sema::TypeInterner& types) {
    const auto& td = types.get(id);
    switch (td.kind) {
        case sema::TypeKind::I8:  return "c";
        case sema::TypeKind::I16: return "s";
        case sema::TypeKind::I32: return "i";
        case sema::TypeKind::I64: return "l";
        case sema::TypeKind::U8:  return "Ch";
        case sema::TypeKind::U16: return "Cs";
        case sema::TypeKind::U32: return "Ci";
        case sema::TypeKind::U64: return "Cl";
        case sema::TypeKind::F32: return "f";
        case sema::TypeKind::F64: return "d";
        case sema::TypeKind::Bool:   return "b";
        case sema::TypeKind::String: return "S";
        case sema::TypeKind::Hollow: return "v";  // среди параметров не встречается
        case sema::TypeKind::Array:
            return "A" + std::to_string(td.array_size) + "_"
                 + type_code(td.elem, types);
        case sema::TypeKind::Range:
            return "R" + type_code(td.elem, types);
        case sema::TypeKind::Struct:
            return "U" + len_name(types.display_name(id));
    }
    return "?";
}

} // namespace

std::string mangle(const sema::FnSymbol& fn,
                   std::span<const MangleScope> scopes,
                   const sema::TypeInterner& types) {
    std::string out = "_L_";
    for (const auto& s : scopes) {
        out += (s.kind == MangleScope::Kind::Namespace) ? 'N' : 'I';
        out += len_name(s.name);
        out += '_';
    }
    out += len_name(fn.name);
    out += '_';
    for (const auto& p : fn.params)
        out += type_code(p.type, types);
    return out;
}

std::string symbol_name(const sema::FnSymbol& fn,
                        std::span<const MangleScope> scopes,
                        const sema::TypeInterner& types) {
    // §5.4: точка входа эмитится как @main без mangling
    if (fn.name == "main" && scopes.empty())
        return "main";
    return mangle(fn, scopes, types);
}

} // namespace mycc::cg
