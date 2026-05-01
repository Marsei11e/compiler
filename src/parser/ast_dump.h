/* рекурсивный принтер для узлов АСТ */
#pragma once

#include "ast.h"
#include <iosfwd>

namespace mycc::ast {

class AstDumper {
public:
    explicit AstDumper(std::ostream& out, int indent_step = 2)
        : out_(out), step_(indent_step) {}

    void dump(const Program& prog);

private:
    std::ostream& out_;
    int           step_;
    int           depth_{0};

    void indent();
    void dump_decl(const Decl& d);
    void dump_stmt(const Stmt& s);
    void dump_expr(const Expr& e);
    void dump_type(const TypeNode& t);
    void dump_fn  (const FnDecl& fn);
    void dump_params(const std::vector<ParamDecl>& params);
    void dump_effects(const std::vector<EffectKind>& eff);
};

} // namespace mycc::ast
