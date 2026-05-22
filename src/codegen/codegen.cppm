//codegen - текстовый эмиттер LLVM IR
module;

#include <cstdint>
#include <span>
#include <string>

export module mycc.codegen;

import mycc.sema;
import mycc.ir;

export namespace mycc::cg {
std::string lower_type(sema::TypeId id, const sema::TypeInterner& types,
                       bool storage = false);

// определение именованного типа строки - эмитится один раз в преамбуле модуля
std::string string_type_def();

// §5 -  один уровень вложенности для mangling: namespace или impl-блок.
struct MangleScope {
    enum class Kind : uint8_t { Namespace, Impl };
    Kind        kind;
    std::string name;
};

// полное mangled-имя функции по схеме §5.1: "_L_<scope>_<name>_<arg_types>".
// scopes - путь вложенности от внешнего к внутреннему (пустой для top-level).
std::string mangle(const sema::FnSymbol& fn,
                   std::span<const MangleScope> scopes,
                   const sema::TypeInterner& types);

// символ функции в LLVM IR: как mangle(), но точка входа main эмитится
// как "main" без mangling - для совместимости с системным линкером (§5.4).
std::string symbol_name(const sema::FnSymbol& fn,
                        std::span<const MangleScope> scopes,
                        const sema::TypeInterner& types);

// каркас текстового эмиттера
class LlvmEmitter {
public:
    LlvmEmitter() = default;

    // эмитит модуль в текст LLVM IR
    std::string emit(const ir::Module& mod);
};

} // export namespace mycc::cg
