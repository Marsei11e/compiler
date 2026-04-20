# codegen.md — Генератор кода

---

## 1. Целевая платформа и пайплайн

Язык компилируется в **LLVM IR** — текстовое промежуточное представление LLVM. Пайплайн генерации исполняемого файла:

```
аннотированный AST
       │
       ▼
  [Lowering] ──────► внутренний IR (TAC)
       │
       ▼
  [Оптимизатор] ───► оптимизированный IR
       │
       ▼
  [LLVM IR codegen] ► text.ll   (текстовый LLVM IR)
       │
       ▼
  llc -filetype=obj ► text.o    (объектный файл x86-64)
       │
       ▼
  cc -o out text.o runtime.o    (линковка с рантаймом)
       │
       ▼
  out   (исполняемый файл)
```

Внешние инструменты: `llc` (входит в поставку LLVM) и системный компилятор C (`clang` или `gcc`) для линковки. Вызов производится через `std::system` или `fork`/`exec`.

---

## 2. Внутреннее представление (IR)

Между семантическим анализатором и генератором LLVM IR находится собственное линейное IR в форме **трёхадресного кода (TAC)**. Это представление служит базой для оптимизаций (см. `semantics.md`, §15) и упрощает генерацию LLVM IR.

### 2.1 Операнды

Операнд в IR — это либо:

- **константа** — целочисленное или вещественное значение, строковый литерал, `true`/`false`;
- **временная переменная** — обозначается как `%t0`, `%t1`, … (в текстовой печати);
- **именованная переменная** — локальная или параметр функции, обозначается `%имя`;
- **глобальный символ** — функция или глобальная переменная, `@имя`.

Каждый операнд имеет **тип**, соответствующий одному из типов языка.

### 2.2 Инструкции IR

| Категория | Инструкция | Пример (псевдоформат) |
|-----------|-----------|-----------------------|
| Арифметика | `add`, `sub`, `mul`, `div`, `mod` | `%t2 = add i32 %t0, %t1` |
| Сравнения | `eq`, `ne`, `lt`, `gt`, `le`, `ge` | `%t2 = lt i32 %t0, %t1` |
| Логические | `and`, `or`, `not` | `%t2 = and i1 %t0, %t1` |
| Приведение | `cast` | `%t1 = cast i32 to i64 %t0` |
| Работа с памятью | `load`, `store`, `alloca` | `store i32 %t0, ptr %x` |
| Доступ | `getelem` (массив), `getfield` (структура) | `%t1 = getfield %Point, %p, 0` |
| Управление | `jmp`, `br`, `ret` | `br %cond, label L1, label L2` |
| Вызов | `call` | `%t1 = call i32 @foo(%t0)` |
| Диапазон | `range_new`, `range_next` | `%r = range_new i32 %a, %b` |
| Метки | `label L:` | `L1:` |

Инструкция `range_next` — псевдоинструкция, раскрывающаяся в ходе LLVM IR codegen в проверку `%cur < %end` и инкремент. В собственном IR она присутствует для удобства оптимизаций.

### 2.3 Формат печати IR

IR поддерживает текстовую печать (флаг `--dump-ir`). Формат — читаемый, приближенный к LLVM IR по стилю, но с собственным синтаксисом. Пример:

```
function main() -> i32 {
entry:
    %x = alloca i32
    store i32 10, %x
    %t0 = load i32, %x
    %t1 = add i32 %t0, 5
    ret i32 %t1
}
```

### 2.4 SSA-форма — не используется

IR **не находится в SSA-форме**. Переменные могут переприсваиваться. Это упрощает lowering из AST и оптимизации. LLVM IR, напротив, требует SSA — преобразование происходит в ходе генерации LLVM IR с помощью `alloca`/`load`/`store` для всех локальных переменных (LLVM сам переводит их в SSA через проход `mem2reg`, который `llc` применяет на уровне `-O1` и выше).

---

## 3. Отображение типов языка в LLVM IR

| Тип языка | LLVM IR |
|-----------|---------|
| `int8` | `i8` |
| `int16` | `i16` |
| `int32` | `i32` |
| `int64` | `i64` |
| `uint8` | `i8` |
| `uint16` | `i16` |
| `uint32` | `i32` |
| `uint64` | `i64` |
| `float32` | `float` |
| `float64` | `double` |
| `bool` | `i1` (для значений), `i8` (для хранения в памяти) |
| `hollow` | `void` (только в типе возврата) |
| `string` | `%string = type { ptr, i64 }` |
| `array[T, N]` | `[N x T']` |
| `range[T]` | `{ T', T' }` (пара: current, end) |
| `struct S { f1: T1, f2: T2 }` | `%S = type { T1', T2' }` |

**Примечания:**

- Знаковость целых **не отражена в типе LLVM** — LLVM не различает `i32` знаковый и беззнаковый. Знаковость учитывается в **выборе инструкции**: `sdiv`/`udiv`, `sext`/`zext`, `slt`/`ult` и т.д.
- `bool` хранится в памяти как `i8` (LLVM требует, чтобы память адресовалась байтами), но в вычислениях используется `i1`. При `load`/`store` вставляются `trunc`/`zext`.
- Строки — указатель на байты + длина в байтах. Данные строковых литералов хранятся в `.rodata` как глобальные массивы `i8`.

---

## 4. Соглашения о вызовах

### 4.1 Соглашение

Используется стандартное для LLVM/x86-64 на Linux соглашение **System V AMD64 ABI** (`ccc` в терминах LLVM). `llc` самостоятельно реализует все детали: передачу через регистры `rdi, rsi, rdx, rcx, r8, r9` и `xmm0–xmm7`, возврат в `rax`/`xmm0`, выравнивание стека.

### 4.2 Скалярные типы и структуры

- Скалярные типы (`iN`, `float`, `double`) передаются и возвращаются напрямую.
- Структуры малого размера (≤ 16 байт) LLVM самостоятельно пакует в регистры.
- Структуры большего размера передаются по **указателю** с пометкой `byval` (копия создаётся на стороне вызывающего).
- Массивы `[N x T]` передаются по значению — LLVM копирует их через стек или `byval`.
- Возврат больших структур — через скрытый указатель (`sret`), LLVM делает это автоматически.

### 4.3 Соглашения о вызове для перегрузок

Каждая перегрузка функции становится **отдельной** LLVM-функцией с уникальным именем (см. §5 — mangling).

---

## 5. Mangling имён

Имена функций в LLVM IR получаются из имён исходного языка по правилу mangling'а. Это необходимо, чтобы:

- перегруженные функции с одним именем имели разные символы;
- функции в разных пространствах имён не конфликтовали;
- методы структур имели однозначные имена.

### 5.1 Схема mangling'а

```
_L <scope> _ <name> _ <arg_types>
```

Где:

- `_L` — префикс языка (во избежание конфликтов с символами из C).
- `<scope>` — последовательность `Nk<name>` для каждого уровня пространства имён или `Ik<StructName>` для `impl`-блока.
- `<name>` — имя функции (длина `n`, затем имя: `4main` для `main`).
- `<arg_types>` — сокращённые коды типов параметров, конкатенированные.

### 5.2 Коды типов

| Тип | Код |
|-----|-----|
| `int8` | `c` |
| `int16` | `s` |
| `int32` | `i` |
| `int64` | `l` |
| `uint8` | `Ch` |
| `uint16` | `Cs` |
| `uint32` | `Ci` |
| `uint64` | `Cl` |
| `float32` | `f` |
| `float64` | `d` |
| `bool` | `b` |
| `string` | `S` |
| `array[T, N]` | `A<N>_<code(T)>` |
| `range[T]` | `R<code(T)>` |
| Структура `Name` | `U<length><Name>` |

### 5.3 Примеры

```
fn main() -> int32
    → @_L_4main_                 -- нет аргументов
    -- часто сокращается до @main для точки входа (см. §5.4)

fn area(side: float64) -> float64
    → @_L_4area_d

fn area(w: float64, h: float64) -> float64
    → @_L_4area_dd

namespace Math { fn sqr(x: int32) -> int32 }
    → @_L_N4Math_3sqr_i

impl Point { fn distance(self: Point, other: Point) -> float64 }
    → @_L_I5Point_8distance_U5PointU5Point

impl Point { fn origin() -> Point }
    → @_L_I5Point_6origin_
```

### 5.4 Точка входа

Функция `main` — особый случай. В LLVM IR она эмитится под именем `@main` (без mangling), чтобы быть совместимой с системным линкером:

```llvm
define i32 @main() { ... }
```

Если в исходном коде объявлены перегрузки `main`, это ошибка компиляции на уровне семантического анализатора (`main` не может быть перегружена — имя зарезервировано за точкой входа).

### 5.5 Имена встроенных функций

Встроенные функции компилируются в вызовы рантайма:

| Встроенное | LLVM-символ | Реализация |
|------------|-------------|------------|
| `print` (знаковый int: `int8..int64`) | `@rt_print_i64` | `runtime.c` |
| `print` (беззнаковый int: `uint8..uint64`) | `@rt_print_u64` | `runtime.c` |
| `print` (float: `float32`, `float64`) | `@rt_print_f64` | `runtime.c` |
| `print` (bool) | `@rt_print_bool` | `runtime.c` |
| `print` (string) | `@rt_print_string` | `runtime.c` |
| `println` (знаковый int: `int8..int64`) | `@rt_println_i64` | `runtime.c` |
| `println` (беззнаковый int: `uint8..uint64`) | `@rt_println_u64` | `runtime.c` |
| `println` (float: `float32`, `float64`) | `@rt_println_f64` | `runtime.c` |
| `println` (bool) | `@rt_println_bool` | `runtime.c` |
| `println` (string) | `@rt_println_string` | `runtime.c` |
| `println` (без аргументов) | `@rt_println_empty` | `runtime.c` |
| `input` | `@rt_input` | `runtime.c` |
| `exit` | `@rt_exit` | `runtime.c` |
| `panic` | `@rt_panic` | `runtime.c` |
| `len` | `@rt_strlen` | `runtime.c` |

Поскольку `print` и `println` работают с разными типами, в ходе codegen компилятор выбирает соответствующий рантайм-символ по типу аргумента. Это разрешается **внутри** codegen — с точки зрения пользователя это две функции `print` и `println`, перегруженные по типу.

**Правила выбора символа по типу аргумента:**

- `int8`, `int16`, `int32`, `int64` → `i64` через `sext` (для меньших ширин), вызов `@rt_print_i64` / `@rt_println_i64`.
- `uint8`, `uint16`, `uint32`, `uint64` → `i64` через `zext` (для меньших ширин), вызов `@rt_print_u64` / `@rt_println_u64`.
- `float32` → `double` через `fpext`, вызов `@rt_print_f64` / `@rt_println_f64`.
- `float64` → напрямую, вызов `@rt_print_f64` / `@rt_println_f64`.
- `bool` → `i32` через `zext` (поскольку C ABI), вызов `@rt_print_bool` / `@rt_println_bool`.
- `string` → передача `%string`-структуры, вызов `@rt_print_string` / `@rt_println_string`.

Различение `i64` (знаковый) и `u64` (беззнаковый) символов рантайма важно потому, что значения `uint64 > 2^63 - 1` неотличимы от отрицательных `int64` на уровне битового представления — без отдельного символа корректный десятичный вывод невозможен.

---

## 6. Генерация кода по конструкциям

### 6.1 Функции

Каждая функция исходного языка превращается в `define` в LLVM IR:

```
fn add(a: int32, b: int32) -> int32 @pure {
    return a + b;
}
```

становится:

```llvm
define i32 @_L_3add_ii(i32 %a, i32 %b) {
entry:
    %a.addr = alloca i32
    %b.addr = alloca i32
    store i32 %a, ptr %a.addr
    store i32 %b, ptr %b.addr
    %t0 = load i32, ptr %a.addr
    %t1 = load i32, ptr %b.addr
    %t2 = add i32 %t0, %t1
    ret i32 %t2
}
```

**Замечания:**

- Все параметры копируются в локальные `alloca`-слоты — это унифицирует работу с параметрами и локальными переменными.
- Проход `-mem2reg` в LLVM заменит `alloca`/`load`/`store` регистрами SSA — вручную этого делать не надо.
- Аннотации эффектов (`@pure` и т.д.) **не** переносятся в LLVM IR. Они — compile-time атрибуты, проверенные на фазе семантического анализа.

### 6.2 Локальные переменные

Каждая локальная переменная получает `alloca`-слот на входе в функцию. Все `alloca` размещаются в блоке `entry` — это требование LLVM для корректной работы `mem2reg`.

```
var x: int32 = 10;
x = x + 1;
```

```llvm
    %x = alloca i32
    store i32 10, ptr %x
    %t0 = load i32, ptr %x
    %t1 = add i32 %t0, 1
    store i32 %t1, ptr %x
```

### 6.3 Структуры и доступ к полям

```
struct Point { x: float64, y: float64 }
var p: Point = Point{ x: 3.0, y: 4.0 };
var dx: float64 = p.x;
```

```llvm
%Point = type { double, double }

    %p = alloca %Point
    %p.x.ptr = getelementptr %Point, ptr %p, i32 0, i32 0
    store double 3.0, ptr %p.x.ptr
    %p.y.ptr = getelementptr %Point, ptr %p, i32 0, i32 1
    store double 4.0, ptr %p.y.ptr

    %t0.ptr = getelementptr %Point, ptr %p, i32 0, i32 0
    %t0 = load double, ptr %t0.ptr
    %dx = alloca double
    store double %t0, ptr %dx
```

### 6.4 Массивы

```
var arr: array[int32, 3] = [10, 20, 30];
var v: int32 = arr[1];
```

```llvm
    %arr = alloca [3 x i32]
    %arr.0 = getelementptr [3 x i32], ptr %arr, i32 0, i32 0
    store i32 10, ptr %arr.0
    %arr.1 = getelementptr [3 x i32], ptr %arr, i32 0, i32 1
    store i32 20, ptr %arr.1
    %arr.2 = getelementptr [3 x i32], ptr %arr, i32 0, i32 2
    store i32 30, ptr %arr.2

    ; индексирование с проверкой границ:
    call void @rt_check_bounds(i64 1, i64 3, i32 <line>)
    %v.ptr = getelementptr [3 x i32], ptr %arr, i32 0, i64 1
    %v = load i32, ptr %v.ptr
```

**Проверка границ** — обязательный вызов `@rt_check_bounds` перед каждым индексированием. Аргументы: индекс, размер, номер строки для сообщения об ошибке. При выходе за границы рантайм печатает:

```
runtime error: index out of bounds at line <N>
```

и завершает программу с кодом `1`.

### 6.5 Арифметика

Выбор LLVM-инструкции зависит от типа операнда:

| Операция | `int` (знаковый) | `uint` (беззнаковый) | `float` |
|----------|------------------|----------------------|---------|
| `+` | `add` | `add` | `fadd` |
| `-` | `sub` | `sub` | `fsub` |
| `*` | `mul` | `mul` | `fmul` |
| `/` | `sdiv` | `udiv` | `fdiv` |
| `%` | `srem` | `urem` | — |
| `<` | `icmp slt` | `icmp ult` | `fcmp olt` |
| `>` | `icmp sgt` | `icmp ugt` | `fcmp ogt` |
| `==` | `icmp eq` | `icmp eq` | `fcmp oeq` |

**Проверка деления на ноль:** перед каждой инструкцией `sdiv`/`udiv`/`srem`/`urem` вставляется вызов `@rt_check_div_zero`:

```llvm
    call void @rt_check_div_zero(i64 %divisor_as_i64, i32 <line>)
    %t = sdiv i32 %a, %b
```

`rt_check_div_zero` при `divisor == 0` печатает `runtime error: division by zero at line <N>` и завершает программу.

**Оптимизация:** если семантический анализатор или constant folding установил, что делитель заведомо ненулевой, проверка пропускается. Это отмечается флагом в IR-инструкции `div`/`mod`.

### 6.6 Приведения типов (`cast`)

| Приведение | LLVM-инструкция |
|------------|-----------------|
| `intN → intM`, `M > N`, знаковое | `sext` |
| `intN → intM`, `M > N`, беззнаковое | `zext` |
| `intN → intM`, `M < N` | `trunc` |
| `int → float` | `sitofp` / `uitofp` |
| `float → int` | `fptosi` / `fptoui` |
| `float32 → float64` | `fpext` |
| `float64 → float32` | `fptrunc` |
| `int → bool` | `icmp ne <x>, 0`, затем `zext` при хранении |
| `bool → int` | `zext` из `i1` в нужный размер |

### 6.7 Короткое замыкание `&&` / `||`

Операторы `&&` и `||` не компилируются в прямые LLVM-инструкции (их в LLVM нет) — вместо этого строится граф блоков:

```
-- a && b

    %ta = <вычисление a>
    br i1 %ta, label %and_rhs, label %and_end

and_rhs:
    %tb = <вычисление b>
    br label %and_end

and_end:
    %result = phi i1 [ false, %entry ], [ %tb, %and_rhs ]
```

Аналогично для `||`.

### 6.8 `if` (инструкция и выражение)

```
if cond { A } else { B }
```

становится:

```llvm
    %c = <вычисление cond>
    br i1 %c, label %if_then, label %if_else

if_then:
    <A>
    br label %if_end

if_else:
    <B>
    br label %if_end

if_end:
```

Для `if` в позиции выражения результат собирается через `phi`:

```llvm
if_end:
    %result = phi i32 [ %ta, %if_then ], [ %tb, %if_else ]
```

### 6.9 `while`

```
while cond { body }
```

```llvm
    br label %while_cond

while_cond:
    %c = <вычисление cond>
    br i1 %c, label %while_body, label %while_end

while_body:
    <body>
    br label %while_cond

while_end:
```

### 6.10 `for` по диапазону

```
for i in 0..10 { body }
```

Диапазон компилируется в структуру `{ i32, i32 }`:

```llvm
    %range = alloca { i32, i32 }
    %r.cur.ptr = getelementptr { i32, i32 }, ptr %range, i32 0, i32 0
    store i32 0, ptr %r.cur.ptr
    %r.end.ptr = getelementptr { i32, i32 }, ptr %range, i32 0, i32 1
    store i32 10, ptr %r.end.ptr

    br label %for_cond

for_cond:
    %cur = load i32, ptr %r.cur.ptr
    %end = load i32, ptr %r.end.ptr
    %c = icmp slt i32 %cur, %end
    br i1 %c, label %for_body, label %for_end

for_body:
    %i = alloca i32
    store i32 %cur, ptr %i
    <body>
    %cur2 = load i32, ptr %r.cur.ptr
    %cur3 = add i32 %cur2, 1
    store i32 %cur3, ptr %r.cur.ptr
    br label %for_cond

for_end:
```

### 6.11 `for` по массиву

```
for x in arr { body }
```

Раскрывается в цикл по индексу `0..N`, где `N` зафиксировано в типе массива:

```llvm
    %i = alloca i64
    store i64 0, ptr %i
    br label %for_cond

for_cond:
    %ci = load i64, ptr %i
    %c = icmp ult i64 %ci, <N>
    br i1 %c, label %for_body, label %for_end

for_body:
    %e.ptr = getelementptr [<N> x T'], ptr %arr_copy, i32 0, i64 %ci
    %x = alloca T'
    %e = load T', ptr %e.ptr
    store T' %e, ptr %x
    <body>
    %ci2 = load i64, ptr %i
    %ci3 = add i64 %ci2, 1
    store i64 %ci3, ptr %i
    br label %for_cond

for_end:
```

Массив копируется в локальный `arr_copy` перед циклом (value semantics).

### 6.12 `break` и `continue`

- `break` — безусловный переход на `label %<loop>_end` текущего цикла.
- `continue` — безусловный переход на `label %<loop>_cond` текущего цикла (или на блок инкремента в `for`).

Перед переходом эмитятся все активные `defer`-инструкции покидаемых блоков (§6.14).

### 6.13 `return`

```
return expr;
```

Сначала выполняются все активные `defer` покидаемых блоков, затем:

```llvm
    %rv = <вычисление expr>
    ret <T> %rv
```

Для функций с типом возврата `hollow`:

```llvm
    ret void
```

### 6.14 `defer`

Каждый блок AST получает ассоциированный **список отложенных действий**. При компиляции блока:

1. При встрече `defer stmt` — AST `stmt` добавляется в список отложенных действий текущего блока (но не эмитится в IR сразу).
2. В каждой точке выхода из блока (`}`, `return`, `break`, `continue`) перед переходом эмитятся **все** накопленные отложенные действия **в обратном порядке** (LIFO).

**Реализация через вспомогательную функцию:**

Для функции, содержащей `defer`, генератор ведёт стек списков отложенных действий, соответствующий вложенности блоков. Перед каждым управляющим переходом:

- `return` — вычисляется возвращаемое значение, сохраняется во временный слот, затем эмитятся все `defer` из всех активных блоков (от самого внутреннего к функции), затем `ret`.
- `break`/`continue` — эмитятся все `defer` покидаемых блоков (от самого внутреннего до блока, содержащего цикл), затем `br`.
- Естественное завершение блока — эмитятся все `defer` этого блока.

**Пример:**

```
fn foo() -> int32 {
    defer println("1");
    {
        defer println("2");
        return 42;
    }
}
```

```llvm
define i32 @_L_3foo_() {
entry:
    %retval = alloca i32
    store i32 42, ptr %retval

    ; defer-и внутреннего блока (LIFO):
    call void @rt_println_string(...)  ; println("2")

    ; defer-и внешнего блока:
    call void @rt_println_string(...)  ; println("1")

    %r = load i32, ptr %retval
    ret i32 %r
}
```

**Важно:** `defer`, зарегистрированный только в одной ветке `if`, должен эмититься только при выходе из этой ветки. Для этого генератор отслеживает регистрацию `defer` на уровне каждого блока базовых блоков, и при слиянии ветвей сбрасывает список зарегистрированных `defer` до общего предка.

### 6.15 Pipe-оператор `|>`

К моменту codegen оператор `|>` уже раскрыт семантическим анализатором в обычный вызов функции:

```
x |> f  ≡ вызов f(x)
x |> f(y, z)  ≡ вызов f(x, y, z)
```

Отдельной генерации не требуется — это форма вызова.

### 6.16 Вызов методов

`obj.method(args)` к моменту codegen разрешён в конкретную перегрузку метода. Генерация — обычный вызов функции с `obj` в качестве первого аргумента:

```
p.distance(q)
```

становится:

```llvm
    %p.val = load %Point, ptr %p
    %q.val = load %Point, ptr %q
    %t = call double @_L_I5Point_8distance_U5PointU5Point(%Point %p.val, %Point %q.val)
```

### 6.17 Диапазоны и их потребление

Move-семантика для `range[T]` проверяется на фазе семантического анализа. К моменту codegen переменные типа `range[T]` либо перемещаются, либо нет — это выражено в AST. На уровне LLVM IR перемещение = обычное копирование `{ T', T' }`, поскольку в runtime перемещение индентично копированию (память одна). Никаких дополнительных действий при codegen для диапазонов не требуется.

### 6.18 Строки

Строковые литералы хранятся в `.rodata` как глобальные массивы `i8`, включая завершающий `\00` (для совместимости с C-функциями рантайма):

```llvm
@.str.0 = private unnamed_addr constant [14 x i8] c"Hello, world!\00"
```

Значение типа `string` в регистрах представляется как `%string = { ptr, i64 }`. Литерал создаётся как:

```llvm
    %s = alloca %string
    %s.data.ptr = getelementptr %string, ptr %s, i32 0, i32 0
    store ptr @.str.0, ptr %s.data.ptr
    %s.len.ptr = getelementptr %string, ptr %s, i32 0, i32 1
    store i64 13, ptr %s.len.ptr
```

**Конкатенация `s1 + s2`** компилируется в вызов рантайм-функции:

```llvm
    %s3 = call %string @rt_string_concat(%string %s1, %string %s2)
```

Результат — свежая строка в куче (рантайм выделяет память через `malloc`).

**Сравнение строк (`==`, `!=`)** — вызов `@rt_string_eq`, возвращающий `i1`.

**Управление памятью строк:** в базовой версии языка строки, возвращённые `rt_string_concat` и `rt_input`, **не освобождаются**. Это утечка, но для учебного компилятора приемлемо. В `report.md` отмечается как известное ограничение.

---

## 7. Рантайм (`runtime.c`)

Рантайм — отдельный модуль на C, компилируемый один раз и линкуемый с каждой программой. Функции рантайма:

```c
// ===== Строки =====
typedef struct {
    const char* data;
    int64_t len;
} rt_string;

rt_string rt_string_concat(rt_string a, rt_string b);
int32_t rt_string_eq(rt_string a, rt_string b);
int32_t rt_strlen(rt_string s);

// ===== Ввод/вывод =====
// print — без перевода строки
void rt_print_i64(int64_t x);
void rt_print_u64(uint64_t x);
void rt_print_f64(double x);
void rt_print_bool(int32_t b);
void rt_print_string(rt_string s);

// println — с переводом строки
void rt_println_i64(int64_t x);
void rt_println_u64(uint64_t x);
void rt_println_f64(double x);
void rt_println_bool(int32_t b);
void rt_println_string(rt_string s);
void rt_println_empty(void);

rt_string rt_input(void);

// ===== Завершение =====
void rt_exit(int32_t code) __attribute__((noreturn));
void rt_panic(rt_string msg, int32_t line) __attribute__((noreturn));

// ===== Runtime checks =====
void rt_check_div_zero(int64_t divisor, int32_t line);
void rt_check_bounds(int64_t index, int64_t size, int32_t line);
```

**Замечания:**

- `rt_check_div_zero` и `rt_check_bounds` при нарушении условия печатают в `stderr` сообщение формата `runtime error: ... at line <N>` и вызывают `exit(1)`.
- `rt_panic` делает то же самое, но с пользовательским сообщением.
- `rt_print_*` используют `printf` без `\n`. `rt_println_*` — то же, но с завершающим `\n`. `print(bool)` и `println(bool)` печатают `true` или `false`.
- `rt_input` читает строку через `getline` или аналог, убирает завершающий `\n`.

**Линковка:** файл `runtime.c` компилируется в `runtime.o` при сборке компилятора, и этот объект линкуется с каждой откомпилированной программой:

```
cc -o out program.o runtime.o
```

---

## 8. Генерация кода для пространств имён

Пространства имён не создают отдельных структур в LLVM IR — они влияют только на имя функции через mangling (§5). Объявления внутри пространства имён эмитятся как обычные глобальные функции с соответствующим mangled-именем.

Вложенные пространства имён обрабатываются рекурсивно — префикс mangling'а накапливается.

---

## 9. Генерация кода для `impl`-блоков

Методы и ассоциированные функции из `impl`-блоков компилируются как обычные глобальные функции с mangled-именами по схеме `I<StructName>`. Параметр `self` передаётся как первый обычный параметр — никакого специального соглашения для `self` нет.

При перегрузке методов mangling разделяет их по типам параметров, как и для обычных функций.

---

## 10. Оптимизации

Внутренние оптимизации языка (constant folding, dead code elimination) выполняются **на уровне собственного IR**, до генерации LLVM IR. Это описано в `semantics.md`, §15.

На уровне LLVM IR компилятор **не** полагается на внутренние оптимизации — сгенерированный IR должен быть корректен сам по себе. Дополнительно полагается на стандартные проходы LLVM, запускаемые `llc`:

- `llc -O0` — минимум оптимизаций, читаемый выход;
- `llc -O2` — `mem2reg`, инлайнинг, устранение мёртвого кода, и др.

Выбор уровня оптимизации задаётся флагом компилятора:

```
myc source.lang -O0       -- без оптимизаций LLVM
myc source.lang -O2       -- с оптимизациями (по умолчанию)
```

Собственные оптимизации (constant folding, DCE) применяются **всегда**, независимо от флага `-O`.

---

## 11. Интерфейс командной строки codegen

### 11.1 Флаги, управляющие выходом

| Флаг | Действие |
|------|----------|
| `--dump-ir` | Вывести внутренний IR в `stdout` и завершить работу. |
| `--dump-llvm-ir` | Вывести текст LLVM IR в `stdout` и завершить работу. |
| `--emit-ll` | Записать LLVM IR в файл `<out>.ll`, не вызывать `llc`. |
| `--emit-obj` | Записать объектный файл `<out>.o`, не линковать. |
| (по умолчанию) | Полный пайплайн: LLVM IR → `llc` → `cc` → исполняемый файл. |

### 11.2 Выходные файлы

Если не указан флаг `-o`, имя выходного файла определяется по имени исходного:

```
myc program.lang           → ./program
myc program.lang -o app    → ./app
myc program.lang --emit-ll → ./program.ll
```

### 11.3 Зависимости от внешних инструментов

Компилятор требует наличия в `PATH`:

- `llc` (LLVM 16+ рекомендуется);
- `cc` или `clang` (для линковки).

Путь к этим инструментам можно переопределить переменными окружения:

- `MYC_LLC` — путь к `llc`;
- `MYC_CC` — путь к компилятору C для линковки.

Если инструмент недоступен, компилятор выводит сообщение об ошибке и завершается с ненулевым кодом.

---

## 12. Диагностика на этапе codegen

Фаза codegen не должна порождать диагностику **об исходной программе** — все проверки уже сделаны в семантическом анализаторе. Диагностика на этапе codegen ограничивается:

- ошибками внешних инструментов (`llc` вернул ненулевой код — это bug компилятора, выводится как internal error);
- ошибками файловой системы (нет прав на запись, нет места).

Если `llc` или линкер возвращают ошибку, компилятор выводит:

```
internal compiler error: <tool> failed with code <N>
<stderr output of tool>
```

и завершается с кодом `2` (отличающимся от `1` — ошибок в исходной программе).

---

## 13. Примеры сгенерированного кода

### 13.1 Факториал

Исходник:

```
fn factorial(n: int32) -> int32 @pure {
    if n <= 1 {
        return 1;
    } else {
        return n * factorial(n - 1);
    }
}

fn main() -> int32 {
    println(factorial(10));
    return 0;
}
```

LLVM IR (упрощённо, без `alloca`-обёрток для ясности):

```llvm
define i32 @_L_9factorial_i(i32 %n) {
entry:
    %c = icmp sle i32 %n, 1
    br i1 %c, label %then, label %else
then:
    ret i32 1
else:
    %nm1 = sub i32 %n, 1
    %r = call i32 @_L_9factorial_i(i32 %nm1)
    %m = mul i32 %n, %r
    ret i32 %m
}

define i32 @main() {
    %f = call i32 @_L_9factorial_i(i32 10)
    %fl = sext i32 %f to i64
    call void @rt_println_i64(i64 %fl)
    ret i32 0
}
```

### 13.2 Цикл по диапазону

Исходник:

```
fn main() -> int32 {
    var total: int32 = 0;
    for i in 0..5 {
        total = total + i;
    }
    println(total);
    return 0;
}
```

LLVM IR:

```llvm
define i32 @main() {
entry:
    %total = alloca i32
    store i32 0, ptr %total

    %cur = alloca i32
    %end = alloca i32
    store i32 0, ptr %cur
    store i32 5, ptr %end
    br label %for_cond

for_cond:
    %c1 = load i32, ptr %cur
    %c2 = load i32, ptr %end
    %cmp = icmp slt i32 %c1, %c2
    br i1 %cmp, label %for_body, label %for_end

for_body:
    %i = alloca i32
    store i32 %c1, ptr %i
    %t = load i32, ptr %total
    %iv = load i32, ptr %i
    %sum = add i32 %t, %iv
    store i32 %sum, ptr %total
    %inc = add i32 %c1, 1
    store i32 %inc, ptr %cur
    br label %for_cond

for_end:
    %t2 = load i32, ptr %total
    %t2l = sext i32 %t2 to i64
    call void @rt_println_i64(i64 %t2l)
    ret i32 0
}
```

---

## 14. Ограничения текущей реализации

Следующие особенности явно **не поддерживаются** базовым генератором и отражены в `report.md`:

- Отладочная информация (DWARF) не эмитится — отладчик не сможет показывать исходный код.
- Строки, выделенные в куче (`rt_string_concat`, `rt_input`), никогда не освобождаются — утечка памяти по дизайну.
- Позиционно-независимый код (PIC) не обязателен — ожидается статическая линковка.
- Генерация под другие ОС (Windows, macOS) не тестировалась — целевая ОС — Linux x86-64.
- Все целочисленные значения сохраняются в LLVM как `iN`, и арифметика не различает знаковые/беззнаковые — различие только в выборе инструкций.
