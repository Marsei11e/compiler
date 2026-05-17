#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

import mycc.diag;
import mycc.lexer;
import mycc.parser;
import mycc.sema;
import mycc.ir;

static constexpr std::string_view kVersion = "myc v0.0";

static int cmd_dump_tokens(const std::string& path) {
    mycc::diag::SourceManager sm;
    mycc::diag::FileId fid = sm.load_file(path);
    if (fid == mycc::diag::kInvalidFileId) {
        std::cerr << "error: cannot open file '" << path << "'\n";
        return 1;
    }
    mycc::diag::DiagnosticEngine diag(sm);

    const mycc::diag::SourceFile* sf = sm.get_file(fid);
    mycc::lex::Lexer lexer(sf->contents, fid, diag);
    auto tokens = lexer.tokenize();

    for (const auto& tok : tokens) {
        if (tok.kind == mycc::lex::TokenKind::Eof) break;
        std::cout << tok.loc.line << ':' << tok.loc.col
                  << ' ' << mycc::lex::token_kind_name(tok.kind)
                  << ' ' << tok.lexeme << '\n';
    }

    diag.emit_all(std::cerr);
    return diag.has_errors() ? 1 : 0;
}

static int cmd_dump_ast(const std::string& path) {
    mycc::diag::SourceManager sm;
    mycc::diag::FileId fid = sm.load_file(path);
    if (fid == mycc::diag::kInvalidFileId) {
        std::cerr << "error: cannot open file '" << path << "'\n";
        return 1;
    }
    mycc::diag::DiagnosticEngine diag(sm);

    const mycc::diag::SourceFile* sf = sm.get_file(fid);
    mycc::lex::Lexer lexer(sf->contents, fid, diag);
    auto tokens = lexer.tokenize();

    mycc::parse::Parser parser(tokens, diag);
    auto prog = parser.parse();

    mycc::ast::AstDumper dumper(std::cout);
    dumper.dump(prog);

    diag.emit_all(std::cerr);
    return diag.has_errors() ? 1 : 0;
}

static int cmd_dump_ir(const std::string& path, bool optimize) {
    mycc::diag::SourceManager sm;
    mycc::diag::FileId fid = sm.load_file(path);
    if (fid == mycc::diag::kInvalidFileId) {
        std::cerr << "error: cannot open file '" << path << "'\n";
        return 1;
    }
    mycc::diag::DiagnosticEngine diag(sm);

    const mycc::diag::SourceFile* sf = sm.get_file(fid);
    mycc::lex::Lexer lexer(sf->contents, fid, diag);
    auto tokens = lexer.tokenize();

    mycc::parse::Parser parser(tokens, diag);
    auto prog = parser.parse();
    if (diag.has_errors()) { diag.emit_all(std::cerr); return 1; }

    mycc::sema::Sema sema(diag, sm);
    if (!sema.analyze_pass1(prog)) { diag.emit_all(std::cerr); return 1; }
    if (!sema.analyze_pass2(prog)) { diag.emit_all(std::cerr); return 1; }

    auto mod = mycc::ir::lower_program(prog, sema, diag);
    if (optimize) mycc::ir::opt::optimize_module(*mod);
    mycc::ir::dump_module(*mod, std::cout);

    diag.emit_all(std::cerr);
    return diag.has_errors() ? 1 : 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: myc [--version] [--dump-tokens] [--dump-ast] [--dump-ir] <source>\n";
        return 1;
    }

    bool dump_tokens = false;
    bool dump_ast    = false;
    bool dump_ir     = false;
    bool optimize    = true;
    std::string source_file;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        if (arg == "--version") {
            std::cout << kVersion << '\n';
            return EXIT_SUCCESS;
        } else if (arg == "--dump-tokens") {
            dump_tokens = true;
        } else if (arg == "--dump-ast") {
            dump_ast = true;
        } else if (arg == "--dump-ir") {
            dump_ir = true;
        } else if (arg == "-O0" || arg == "--no-opt") {
            optimize = false;
        } else if (arg == "-O1" || arg == "-O2") {
            optimize = true;
        } else {
            source_file = std::string(arg);
        }
    }

    if (!source_file.empty()) {
        if (dump_tokens) return cmd_dump_tokens(source_file);
        if (dump_ast)    return cmd_dump_ast(source_file);
        if (dump_ir)     return cmd_dump_ir(source_file, optimize);
    }

    return EXIT_SUCCESS;
}
