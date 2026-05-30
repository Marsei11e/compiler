#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>

import mycc.diag;
import mycc.lexer;
import mycc.parser;
import mycc.sema;
import mycc.ir;
import mycc.codegen;

static constexpr std::string_view kVersion = "myc v0.0";

// общий пайплайн компиляции: исходник -> строка LLVM IR.
// возвращает nullopt при ошибке (диагностики уже напечатаны в stderr).
static std::optional<std::string>
compile_to_ll(const std::string& path, bool optimize) {
    mycc::diag::SourceManager sm;
    mycc::diag::FileId fid = sm.load_file(path);
    if (fid == mycc::diag::kInvalidFileId) {
        std::cerr << "error: cannot open file '" << path << "'\n";
        return std::nullopt;
    }
    mycc::diag::DiagnosticEngine diag(sm);

    const mycc::diag::SourceFile* sf = sm.get_file(fid);
    mycc::lex::Lexer lexer(sf->contents, fid, diag);
    auto tokens = lexer.tokenize();

    mycc::parse::Parser parser(tokens, diag);
    auto prog = parser.parse();
    if (diag.has_errors()) { diag.emit_all(std::cerr); return std::nullopt; }

    mycc::sema::Sema sema(diag, sm);
    if (!sema.analyze_pass1(prog)) { diag.emit_all(std::cerr); return std::nullopt; }
    if (!sema.analyze_pass2(prog)) { diag.emit_all(std::cerr); return std::nullopt; }

    auto mod = mycc::ir::lower_program(prog, sema, diag);
    if (optimize) mycc::ir::opt::optimize_module(*mod);

    mycc::cg::LlvmEmitter emitter;
    std::string ll = emitter.emit(*mod);

    diag.emit_all(std::cerr);
    if (diag.has_errors()) return std::nullopt;
    return ll;
}

// вспомогательные команды для отладочного вывода
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

// вспомогательные функции драйвера
// имя файла без директории и расширения
static std::string source_stem(const std::string& source) {
    return std::filesystem::path(source).stem().string();
}

// одинарное экранирование строки для безопасной передачи в shell
static std::string sq(const std::string& s) {
    std::string r = "'";
    for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
    return r + "'";
}

// запустить shell-команду, захватить stdout+stderr, вернуть код выхода
static int run_cmd(const std::string& cmd, std::string& captured) {
    FILE* f = popen((cmd + " 2>&1").c_str(), "r");
    if (!f) { captured = "popen failed"; return 127; }
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) captured += buf;
    return WEXITSTATUS(pclose(f));
}

// создать уникальный временный файл с заданным суффиксом (вызывающий удаляет)
static std::string make_temp(const char* suffix) {
    // mkstemps - POSIX-расширение, доступно на Linux/glibc
    std::string tmpl = "/tmp/myc-XXXXXX";
    tmpl += suffix;
    int fd = mkstemps(tmpl.data(), static_cast<int>(std::string_view(suffix).size()));
    if (fd >= 0) close(fd);
    return tmpl;
}

// найти libmycrt.a во время выполнения.
// порядок поиска: $MYC_RT -> рядом с исполняемым (установленный) -> <exe>/../runtime/ (build-дерево) -> пусто (не найдено)
static std::string find_runtime() {
    if (const char* e = std::getenv("MYC_RT"))
        if (std::filesystem::exists(e)) return e;

    char exe_buf[4096]{};
    if (readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1) > 0) {
        std::filesystem::path exe(exe_buf);
        auto try_path = [&](std::filesystem::path p) -> std::string {
            p = p.lexically_normal();
            return std::filesystem::exists(p) ? p.string() : "";
        };
        if (auto r = try_path(exe.parent_path() / "libmycrt.a"); !r.empty())
            return r;
        if (auto r = try_path(exe.parent_path() / "../runtime/libmycrt.a"); !r.empty())
            return r;
    }
    return {};
}

// команды частичной компиляции (только эмиссия файла)

static int cmd_emit_ll(const std::string& source, bool optimize,
                        const std::string& out_path) {
    auto ll = compile_to_ll(source, optimize);
    if (!ll) return 1;
    std::ofstream f(out_path);
    if (!f) {
        std::cerr << "error: cannot write '" << out_path << "'\n";
        return 1;
    }
    f << *ll;
    return 0;
}

static int cmd_dump_llvm(const std::string& source, bool optimize) {
    auto ll = compile_to_ll(source, optimize);
    if (!ll) return 1;
    std::cout << *ll;
    return 0;
}

// полный пайплайн компиляции: исходник -> (опц.) .ll -> .o -> исполняемый файл
static int cmd_compile(const std::string& source, bool optimize,
                        bool emit_obj, const std::string& out_file) {
    // Фаза 1: компиляция в LLVM IR
    auto ll_opt = compile_to_ll(source, optimize);
    if (!ll_opt) return 1;
    const std::string& ll = *ll_opt;

    // записываем IR во временный файл
    std::string ll_path = make_temp(".ll");
    {
        std::ofstream f(ll_path);
        if (!f) {
            std::cerr << "error: cannot create temp file '" << ll_path << "'\n";
            return 1;
        }
        f << ll;
    }

    // Фаза 2: llc - LLVM IR -> объектный файл
    const char* llc_env = std::getenv("MYC_LLC");
    std::string llc = llc_env ? llc_env : "llc";

    std::string obj_path;
    bool obj_is_temp = !emit_obj;
    if (emit_obj) {
        obj_path = out_file.empty() ? source_stem(source) + ".o" : out_file;
    } else {
        obj_path = make_temp(".o");
    }

    std::string tool_out;
    int rc = run_cmd(llc + " -filetype=obj -relocation-model=pic -o "
                         + sq(obj_path) + " " + sq(ll_path),
                     tool_out);
    std::filesystem::remove(ll_path);

    if (rc != 0) {
        std::cerr << "internal compiler error: llc failed with code " << rc << '\n'
                  << tool_out;
        if (obj_is_temp) std::filesystem::remove(obj_path);
        return 2;
    }

    if (emit_obj) return 0;

    // Фаза 3: cc - линковка объектного файла с рантаймом -> исполняемый файл
    std::string rt = find_runtime();
    if (rt.empty()) {
        std::cerr << "error: cannot find libmycrt.a; set MYC_RT=<path>\n";
        std::filesystem::remove(obj_path);
        return 1;
    }

    const char* cc_env = std::getenv("MYC_CC");
    std::string cc = cc_env ? cc_env : "cc";
    std::string exe_out = out_file.empty() ? source_stem(source) : out_file;

    rc = run_cmd(cc + " " + sq(obj_path) + " " + sq(rt) + " -o " + sq(exe_out),
                 tool_out);
    std::filesystem::remove(obj_path);

    if (rc != 0) {
        std::cerr << "internal compiler error: cc failed with code " << rc << '\n'
                  << tool_out;
        return 2;
    }

    return 0;
}

// точка входа

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: myc [--version] [options] <source>\n"
                     "  --dump-tokens    print token stream and exit\n"
                     "  --dump-ast       print AST and exit\n"
                     "  --dump-ir        print internal IR and exit\n"
                     "  --dump-llvm-ir   print LLVM IR to stdout and exit\n"
                     "  --emit-ll        write LLVM IR to file (.ll) and exit\n"
                     "  --emit-obj       compile to object file (.o) and exit\n"
                     "  -o <out>         output file name\n"
                     "  -O0/--no-opt     disable optimizations\n"
                     "  -O1/-O2          enable optimizations (default)\n";
        return 1;
    }

    bool dump_tokens  = false;
    bool dump_ast     = false;
    bool dump_ir      = false;
    bool dump_llvm_ir = false;
    bool emit_ll      = false;
    bool emit_obj     = false;
    bool optimize     = true;
    std::string source_file;
    std::string out_file;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        if (arg == "--version") {
            std::cout << kVersion << '\n';
            return 0;
        } else if (arg == "--dump-tokens") { dump_tokens  = true; }
        else if (arg == "--dump-ast")      { dump_ast     = true; }
        else if (arg == "--dump-ir")       { dump_ir      = true; }
        else if (arg == "--dump-llvm-ir")  { dump_llvm_ir = true; }
        else if (arg == "--emit-ll")       { emit_ll      = true; }
        else if (arg == "--emit-obj")      { emit_obj     = true; }
        else if (arg == "-o") { if (i + 1 < argc) out_file = argv[++i]; }
        else if (arg == "-O0" || arg == "--no-opt") { optimize = false; }
        else if (arg == "-O1" || arg == "-O2")      { optimize = true;  }
        else { source_file = std::string(arg); }
    }

    if (source_file.empty()) {
        std::cerr << "error: no source file specified\n";
        return 1;
    }

    if (dump_tokens)  return cmd_dump_tokens(source_file);
    if (dump_ast)     return cmd_dump_ast(source_file);
    if (dump_ir)      return cmd_dump_ir(source_file, optimize);
    if (dump_llvm_ir) return cmd_dump_llvm(source_file, optimize);
    if (emit_ll) {
        std::string out = out_file.empty() ? source_stem(source_file) + ".ll" : out_file;
        return cmd_emit_ll(source_file, optimize, out);
    }

    // по умолчанию (и --emit-obj): полный пайплайн компиляции
    return cmd_compile(source_file, optimize, emit_obj, out_file);
}
