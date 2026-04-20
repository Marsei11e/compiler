#pragma once

#include "sema/symbol.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace mycc::sema {

using ScopeEntry = std::variant<VarSymbol, OverloadSet, StructSymbol, NamespaceSymbol>;

class Scope {
public:
    explicit Scope(Scope* parent = nullptr) : parent_(parent) {}

    Scope* parent() const { return parent_; }

    bool declare(const std::string& name, VarSymbol sym) {
        if (entries_.count(name)) return false;
        entries_.emplace(name, std::move(sym));
        return true;
    }

    
    bool declare(const std::string& name, FnSymbol sym) {
        auto it = entries_.find(name);
        if (it == entries_.end()) {
            fn_storage_.push_back(std::make_unique<FnSymbol>(std::move(sym)));
            OverloadSet os;
            os.overloads.push_back(fn_storage_.back().get());
            entries_.emplace(name, std::move(os));
            return true;
        }
        auto* os = std::get_if<OverloadSet>(&it->second);
        if (!os) return false;
        fn_storage_.push_back(std::make_unique<FnSymbol>(std::move(sym)));
        os->overloads.push_back(fn_storage_.back().get());
        return true;
    }

    bool declare(const std::string& name, StructSymbol sym) {
        if (entries_.count(name)) return false;
        entries_.emplace(name, std::move(sym));
        return true;
    }

    bool declare(const std::string& name, NamespaceSymbol sym) {
        if (entries_.count(name)) return false;
        entries_.emplace(name, std::move(sym));
        return true;
    }

    
    ScopeEntry* lookup(const std::string& name) {
        auto it = entries_.find(name);
        if (it != entries_.end()) return &it->second;
        if (parent_) return parent_->lookup(name);
        return nullptr;
    }

    const ScopeEntry* lookup(const std::string& name) const {
        auto it = entries_.find(name);
        if (it != entries_.end()) return &it->second;
        if (parent_) return parent_->lookup(name);
        return nullptr;
    }

    ScopeEntry* lookup_local(const std::string& name) {
        auto it = entries_.find(name);
        return it != entries_.end() ? &it->second : nullptr;
    }

private:
    Scope*                                        parent_;
    std::unordered_map<std::string, ScopeEntry>   entries_;
    std::vector<std::unique_ptr<FnSymbol>>         fn_storage_;
};

} 
