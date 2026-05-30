module;

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <utility>
#include <vector>

export module mycc.ir;

import mycc.diag;
import mycc.parser;
import mycc.sema;

export namespace mycc::ir {

// Op

enum class Op : uint8_t {
    // арифметика
    Add, Sub, Mul,
    SDiv, UDiv, FDiv,
    SRem, URem,
    Neg,

    // сравнения (типизированные: signed/unsigned/float)
    IEq, INe,                   // работает для любых int / bool / string
    SLt, SGt, SLe, SGe,
    ULt, UGt, ULe, UGe,
    FEq, FNe, FLt, FGt, FLe, FGe,

    // логические (i1)
    LAnd, LOr, LNot,

    // касты (тип в Inst.cast_kind)
    Cast,

    // память
    Alloca,
    Load,
    Store,

    // доступ к агрегатам
    GetField, // %t = getfield <struct>, <slot>, <field idx>
    GetElem,  // %t = getelem <array>, <slot>, <idx>

    // поток управления
    Jmp, // jmp label
    Br,  // br <cond>, then=label, else=label
    Ret, // ret [val]

    // вызовы (тип в Inst.call_kind)
    Call,

    // ссылка на строковый литерал (операнд 0 - string id; результат - %string)
    StrLit,

    // псевдо-опы range (codegen раскрывает)
    RangeNew,  // %r = range_new T %from, %to
    RangeNext, // %v = range_next T %r - псевдо управления потоком (продвигает cur,
    // ветвится: есть-следующий -> result = cur, прыжок в тело;
     // исчерпан -> прыжок в done)

    // псевдо-маркеры defer (codegen разворачивает в LIFO-порядке на выходах)
    DeferPush, // defer_push <body_label>
    DeferEmit, // defer_emit - на выходе из скопа/петли/функции, запускает все активные defers
};

// подвиды Op::Cast (семантика §3, codegen §6.6)
enum class CastKind : uint8_t {
    SExt,     // signed-extend: меньший int -> больший int (signed)
    ZExt,     // zero-extend: меньший int -> больший int (unsigned / bool)
    Trunc,    // truncate: больший int -> меньший int
    FPExt,    // float widen: float32 -> float64
    FPTrunc,  // float narrow: float64 -> float32
    SIToFP,   // signed int -> float
    UIToFP,   // unsigned int -> float
    FPToSI,   // float -> signed int
    FPToUI,   // float -> unsigned int
    BoolToInt,
    IntToBool,
    Bitcast,  // identity / no-op
};

// подвиды Op::Call (помогает codegen выбрать символы рантайма)
enum class CallKind : uint8_t {
    User,    // функция пользователя (callee = исходное имя)
    Method,  // метод пользователя (callee = имя метода; получатель = arg 0)
    Runtime, // вызов рантайма (callee уже начинается с "rt_")
    Builtin, // встроенный языка, не опущенный (разрешается codegen)
};

// Operand

struct Operand {
    enum class Kind : uint8_t {
        None,
        ConstInt,    // signed/unsigned различается по полю type
        ConstFloat,
        ConstBool,
        ConstString, // индекс в Module.strings (string_id)
        Temp,        // %tN
        Named,       // %name (слот alloca, параметр)
        Global,      // @name (функция или глобал)
        Label,       // метка базового блока (цели Jmp/Br)
    };

    Kind        kind{Kind::None};
    sema::TypeId type{sema::kInvalidTypeId};
    int64_t     ci{0};
    uint64_t    cu{0};
    double      cf{0.0};
    bool        cb{false};
    uint32_t    temp_id{0};
    uint32_t    string_id{0};
    std::string name; // Named / Global / Label

    bool is_none()  const { return kind == Kind::None; }
    bool is_const() const {
        return kind == Kind::ConstInt || kind == Kind::ConstFloat ||
               kind == Kind::ConstBool || kind == Kind::ConstString;
    }
};

inline Operand none_op() { return Operand{}; }

inline Operand temp(uint32_t id, sema::TypeId t) {
    Operand o; o.kind = Operand::Kind::Temp; o.temp_id = id; o.type = t; return o;
}
inline Operand named(std::string n, sema::TypeId t) {
    Operand o; o.kind = Operand::Kind::Named; o.name = std::move(n); o.type = t; return o;
}
inline Operand global(std::string n, sema::TypeId t) {
    Operand o; o.kind = Operand::Kind::Global; o.name = std::move(n); o.type = t; return o;
}
inline Operand label(std::string n) {
    Operand o; o.kind = Operand::Kind::Label; o.name = std::move(n); return o;
}
inline Operand const_int(int64_t v, sema::TypeId t) {
    Operand o; o.kind = Operand::Kind::ConstInt; o.ci = v;
    o.cu = static_cast<uint64_t>(v); o.type = t; return o;
}
inline Operand const_uint(uint64_t v, sema::TypeId t) {
    Operand o; o.kind = Operand::Kind::ConstInt; o.cu = v;
    o.ci = static_cast<int64_t>(v); o.type = t; return o;
}
inline Operand const_float(double v, sema::TypeId t) {
    Operand o; o.kind = Operand::Kind::ConstFloat; o.cf = v; o.type = t; return o;
}
inline Operand const_bool(bool v, sema::TypeId t) {
    Operand o; o.kind = Operand::Kind::ConstBool; o.cb = v; o.type = t; return o;
}
inline Operand const_string(uint32_t id, sema::TypeId t) {
    Operand o; o.kind = Operand::Kind::ConstString; o.string_id = id; o.type = t; return o;
}

// Inst

struct Inst {
    Op op{};
    diag::SourceLocation loc{};
    sema::TypeId type{sema::kInvalidTypeId}; // тип результата/операнда
    Operand result; // None если нет результата
    std::vector<Operand> args;

    // данные, специфичные для op
    CastKind cast_kind{CastKind::Bitcast};  // Cast
    CallKind call_kind{CallKind::User};     // Call
    std::string callee;    // Call (имя функции)
    std::vector<sema::TypeId> callee_param_types; // Call: сигнатура выбранной перегрузки (для mangling)
    uint32_t field_index{0}; // GetField
    std::string  then_label; // Br/Jmp
    std::string  else_label;// Br
    uint32_t   defer_id{0}; // DeferPush
    std::string  defer_body_label;  // DeferPush
};

// BasicBlock

struct BasicBlock {
    std::string label;
    std::vector<Inst> insts;
};

// Function

struct FnParam {
    std::string  name;
    sema::TypeId type{sema::kInvalidTypeId};
};

struct DeferEntry {
    uint32_t id{0};
    std::string body_label; // базовый блок с телом defer
};

// один уровень вложенности (namespace или impl-блок) для mangling.
// заполняется lower_program; emit-фаза превращает в cg::MangleScope
struct FnScopeEntry {
    std::string name;
    bool        is_impl{false};
};

struct Function {
    std::string source_name;  // имя на уровне языка
    std::string  mangled_name;
    std::string  short_name; // только имя функции (без префиксов namespace/impl)
    std::vector<FnScopeEntry> scopes; // путь вложенности от внешнего к внутреннему
    std::vector<FnParam>    params;
    sema::TypeId return_ty{sema::kInvalidTypeId};
    std::vector<BasicBlock> blocks;
    std::vector<DeferEntry> defer_table;
    bool is_main{false};
    diag::SourceLocation  loc{};
};

// Module

struct StringLiteral {
    uint32_t id{0};
    std::string value;
};

struct GlobalVar {
    std::string  name;
    sema::TypeId type{sema::kInvalidTypeId};
    bool  is_const{false};
    // если инициализатор - простой литерал (int/float/bool/string), он сохраняется здесь
    // codegen эмитит глобал как LLVM constant. Если init.is_none(), глобал получает 'zeroinitializer'.
    Operand init;
};

struct Module {
    std::vector<std::unique_ptr<Function>> functions;
    std::vector<StringLiteral>  strings;
    std::vector<GlobalVar>   globals;
    const sema::TypeInterner*   types{nullptr}; // заимствован
};

/* принимает аннотированный АСТ от Sema (в каждом Expr заполнен resolved_type_id,
поток управления и эффекты проверены) и эмитирует линейный трехадресный
Module, готовый для оптимизаций и эмиссии LLVM IR.*/
std::unique_ptr<Module> lower_program(ast::Program& prog,
sema::Sema& sema,
diag::DiagnosticEngine& diag);

/* текстовый принтер IR, формат описан в codegen.md §2.3 */
void dump_module(const Module& mod, std::ostream& os);

} // export namespace mycc::ir

export namespace mycc::ir::opt {

/* IR-оптимизации: constant folding и dead code elimination (semantics §15)
optimize_module запускает обе фазы до фикспоинта. Каждая возвращает true,
если что-то изменлось, чтобы драйвер мог итерироваться.*/
bool const_fold(Module& mod);
bool dce(Module& mod);
void optimize_module(Module& mod);

} // export namespace mycc::ir::opt
