#pragma once

#include "parser/ast.h"
#include "sema/type.h"

#include <string>
#include <vector>

namespace mycc::sema {

class Scope;

struct VarSymbol {
    std::string name;
    TypeId      type{};
    bool        is_const{false};
    bool        is_initialized{false};
    ast::Decl*  decl{nullptr};
};

struct FnSymbol {
    struct Param {
        TypeId      type{};
        std::string name;
    };
    std::string                  mangled_name;
    std::string                  name;
    std::vector<Param>           params;
    TypeId                       return_ty{};
    std::vector<ast::EffectKind> effects;
    ast::FnDecl*                 decl{nullptr};
};

struct OverloadSet {
    std::vector<FnSymbol*> overloads;
};

struct StructSymbol {
    struct Field {
        std::string name;
        TypeId      type{};
    };
    std::string        name;
    TypeId             type_id{kInvalidTypeId};
    std::vector<Field> fields;
    OverloadSet        methods;
    // владеет объектами FnSymbol, на которые ссылается methods.overloads
    std::vector<std::unique_ptr<FnSymbol>> method_storage;
};

struct NamespaceSymbol {
    std::string name;
    Scope*      scope{nullptr};
};

} // namespace mycc::sema
