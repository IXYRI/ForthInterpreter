# Forth Interpreter 橫向對比筆記

本文整合以下內容：

1. `forth.c` 與 `lbforth9.c` 的橫向對比。
2. `lbforth9.c` 與 `jonesforth/` 的橫向對比。
3. `threaded code` 的概念解釋。

涉及文件：

- `forth.c`
- `lbforth9.c`
- `jonesforth/jonesforth.S`
- `jonesforth/jonesforth.f`
- `jonesforth/README`

---

## 1. 三個實作的總體定位

這個工作區裡有三種不同層次的 Forth 實作或 Forth-like 實作。

### 1.1 `forth.c`

`forth.c` 是一個教學型、極簡型 Forth 解釋器。

它使用 C 結構體表示詞典，用 `malloc/realloc` 動態建立冒號定義的 body，用 C 函式實作 primitive。它追求的是短小、直觀、容易看懂。

一句話：

> `forth.c` 是用 C 寫的一個「像 Forth 的小直譯器」。

它適合用來理解：

- token 如何讀入；
- 詞典如何查找；
- 資料堆疊如何傳值；
- `:` 與 `;` 如何建立新詞；
- `if/else/then` 和迴圈如何透過 patch jump 實作。

但它不是完整、正統的 Forth 系統。

### 1.2 `lbforth9.c`

`lbforth9.c` 是一個更接近傳統 Forth 的微型編譯器/解釋器。

它明確說明自己是：

> A minimal Forth compiler in C  
> Based on Richard W.M. Jones' excellent Jonesforth sources/tutorial

它將詞典、資料堆疊、返回堆疊、系統變數和編譯區都放入一塊固定線性記憶體：

```c
byte memory[MEM_SIZE];
```

它有：

- `LATEST`
- `HERE`
- `BASE`
- `STATE`
- data stack
- return stack
- `DOCOL`
- `EXIT`
- `BRANCH`
- `0BRANCH`
- `LIT`
- `CREATE`
- `,`
- `C,`
- `@`
- `!`

一句話：

> `lbforth9.c` 是用 C 搭出的一個可移植 Jonesforth-like 微型 Forth 系統。

### 1.3 Jonesforth

`jonesforth/` 目錄包含：

- `jonesforth.S`
- `jonesforth.f`
- `README`

Jonesforth 是 Richard W.M. Jones 的 literate x86 assembly Forth 教程與實作。它針對 Linux/i386，用 assembly 實現真正的 indirect threaded code，再用 `jonesforth.f` 自舉出大量高階 Forth 詞。

一句話：

> Jonesforth 是原典：用 i386 assembly 展示一個 Forth 系統如何從極少 primitive 長出整個語言。

---

## 2. `forth.c` 詳細梳理

## 2.1 核心資料模型

`forth.c` 的核心結構是：

```c
struct Word {
    char *name;
    int imm;
    void (*prim)(void);
    Cell *body;
    int blen;
    Word *next;
};
```

每個 Forth 詞是一個 `Word`。

它同時承載：

- 詞名：`name`
- immediate 標記：`imm`
- C primitive 函式：`prim`
- 冒號定義 body：`body`
- body 長度：`blen`
- 詞典鏈結：`next`

詞典是單向鏈表：

```c
static Word *dict;
```

新詞插入表頭，因此後定義的詞會遮蔽前面的同名詞。這符合 Forth 詞典的常見語義。

## 2.2 資料堆疊

`forth.c` 只有一個資料堆疊：

```c
static Cell ds[256];
static int dsp;
```

基礎操作是：

```c
void push(Cell v);
Cell pop(void);
Cell peek(void);
```

它沒有返回堆疊。冒號定義呼叫靠 C 遞迴完成：

```c
if (w->body) {
    run(w->body, w->blen);
}
```

這讓實作很短，但也意味著它不是傳統 Forth 的 return-stack 執行模型。

## 2.3 詞典與查找

查找邏輯：

```c
Word *find(char const *s) {
    for (Word *w = dict; w; w = w->next)
        if (strcasecmp(w->name, s) == 0) return w;
    return NULL;
}
```

特點：

- 從最新詞向舊詞查找；
- 大小寫不敏感；
- 找不到時交給數字解析；
- 同名新詞可覆蓋舊詞。

## 2.4 編譯模型

`forth.c` 使用兩個全域狀態：

```c
static int compiling;
static Word *cur;
```

遇到 `:`：

1. 讀下一個 token 作為新詞名。
2. 建立 `Word`。
3. 設定 `cur`。
4. 進入編譯狀態。

遇到 `;`：

1. 編入 `W_RET`。
2. 離開編譯狀態。
3. 清空 `cur`。

主迴圈中：

```c
if (compiling && !w->imm)
    cw(w);
else
    exec(w);
```

因此：

- 直譯狀態下，詞立即執行；
- 編譯狀態下，普通詞編入 body；
- immediate 詞即使在編譯狀態下也立即執行。

數字在編譯狀態下會編譯為：

```text
W_LIT, number
```

在直譯狀態下直接 push 到資料堆疊。

## 2.5 執行模型

已編譯的 body 是一串 `Cell`：

- 一般項目是 `Word *`；
- `W_LIT` 後面跟字面量；
- `W_JMP` / `W_JZ` 後面跟跳轉目標；
- `W_RET` 表示返回。

內部偽詞：

```c
static Word *W_LIT, *W_JMP, *W_JZ, *W_RET;
```

核心執行器：

```c
void run(Cell *code, int len)
```

概念上：

```text
取下一個 Word*
如果是 W_RET，返回
如果是 W_LIT，讀下一格並 push
如果是 W_JZ，根據 pop 結果跳轉
如果是 W_JMP，無條件跳轉
如果是 primitive，呼叫 C 函式
如果是冒號定義，遞迴 run 它的 body
```

這是簡化的 threaded-code/bytecode 混合模型。

## 2.6 控制流

`forth.c` 直接用 C primitive 實作控制流詞：

- `if`
- `else`
- `then`
- `begin`
- `until`
- `again`
- `while`
- `repeat`

例如 `if`：

```c
cw(W_JZ);
cc(0);
push(cur->blen - 1);
```

它做三件事：

1. 編入條件跳轉 `W_JZ`；
2. 留下一個暫時為 `0` 的洞位；
3. 把洞位位置 push 到堆疊，之後由 `else` 或 `then` 回填。

這種方法簡單直接，但把「控制流編譯堆疊」借用了資料堆疊。

## 2.7 內建詞覆蓋面

`forth.c` 支援：

- 算術：`+ - * / mod`
- 輸出：`. emit cr ."`
- 堆疊：`dup drop swap over rot .s`
- 比較：`= < >`
- 位元：`invert and or`
- 定義：`: ; literal`
- 控制流：`if else then begin until again while repeat`

它缺少：

- `@ ! C@ C!`
- `HERE`
- `LATEST`
- `BASE`
- `STATE`
- `CREATE`
- `VARIABLE`
- `CONSTANT`
- 返回堆疊
- double-cell
- `IMMEDIATE` 詞自身
- 標準字串編譯模型
- 完整註解詞

## 2.8 `forth.c` 的性質

優點：

- 短小；
- 直觀；
- 容易修改；
- 適合理解 Forth 最基本概念。

缺點：

- 錯誤處理很少；
- 無堆疊 underflow/overflow 檢查；
- 無返回堆疊；
- 不是真正 threaded Forth；
- 使用 `strcasecmp/strdup`，Windows/MSVC 可攜性需注意；
- 後續加入正統 Forth 功能會需要較大重構。

---

## 3. `lbforth9.c` 詳細梳理

## 3.1 設計目標

`lbforth9.c` 是低依賴、可移植、單文件的微型 Forth。

它只包含：

```c
#include <stdio.h>
```

並刻意避免：

- `malloc`
- `string.h`
- `ctype.h`
- 平台 syscall
- inline assembly

它用 C 模擬 Jonesforth 的核心結構。

## 3.2 Cell 與記憶體配置

預設型別：

```c
#define CELL_BASE_TYPE        short
#define DOUBLE_CELL_BASE_TYPE long
```

基礎型別：

```c
typedef CELL_BASE_TYPE                 scell;
typedef DOUBLE_CELL_BASE_TYPE          dscell;
typedef unsigned CELL_BASE_TYPE        cell;
typedef unsigned DOUBLE_CELL_BASE_TYPE dcell;
typedef unsigned char                  byte;
```

主記憶體：

```c
byte memory[MEM_SIZE];
```

這塊 `memory` 容納：

- input word buffer；
- 系統變數；
- data stack；
- return stack；
- dictionary；
- compiled code。

啟動時檢查：

```c
if (DCELL_SIZE != 2 * CELL_SIZE) {
    tell("Configuration error: DCELL_SIZE != 2*CELL_SIZE\n");
    return 1;
}
```

## 3.3 記憶體佈局

核心 layout：

```c
#define LATEST_POSITION INPUT_LINE_SIZE
#define HERE_POSITION   (LATEST_POSITION + CELL_SIZE)
#define BASE_POSITION   (HERE_POSITION + CELL_SIZE)
#define STATE_POSITION  (BASE_POSITION + CELL_SIZE)
#define STACK_POSITION  (STATE_POSITION + CELL_SIZE)
#define RSTACK_POSITION (STACK_POSITION + STACK_SIZE * CELL_SIZE)
#define HERE_START      (RSTACK_POSITION + RSTACK_SIZE * CELL_SIZE)
```

大致結構：

```text
memory start
│
├─ WORD buffer
├─ LATEST
├─ HERE
├─ BASE
├─ STATE
├─ data stack
├─ return stack
└─ dictionary / compiled code area
```

這比 `forth.c` 更接近傳統 Forth：Forth 系統自身的狀態也暴露為可操作的記憶體。

## 3.4 系統變數

`lbforth9.c` 以 C 指標指向 `memory` 內的位置：

```c
cell *latest;
cell *here;
cell *base;
cell *state;
cell *sp;
cell *stack;
cell *rsp;
cell *rstack;
```

含義：

- `latest`：最新詞典項地址；
- `here`：下一個可編譯地址；
- `base`：數字解析與列印基數；
- `state`：直譯/編譯狀態；
- `sp` / `stack`：資料堆疊；
- `rsp` / `rstack`：返回堆疊。

## 3.5 詞典模型

詞典項 layout 類似 Jonesforth：

```text
link field
flags/name-length byte
name bytes
padding
code field
body...
```

相關函式：

```c
void createWord(char const *name, byte len, byte flags);
cell findWord(cell address, cell len);
cell getCfa(cell address);
```

特點：

- 詞典存在 `memory` 裡；
- `LATEST` 指向最新詞；
- 每個詞有 link 指向上一個詞；
- 名字大小寫不敏感；
- 支援 immediate flag；
- 支援 hidden flag。

## 3.6 Builtin 機制

C primitive 用 builtin ID 表示：

```c
#define BUILTIN(id, name, c_name, flags) ...
#define ADD_BUILTIN(c_name) ...
```

函式指標表：

```c
builtin builtins[MAX_BUILTIN_ID];
```

詞典中的 code field 對 builtin 而言存的是小整數 ID，不是真實機器地址。

執行時：

```c
if (command < MAX_BUILTIN_ID)
    builtins[command]();
```

這是 `lbforth9.c` 與 Jonesforth 的核心差異之一：

- Jonesforth 存真正 codeword 地址；
- `lbforth9.c` 存 builtin ID，由 C 查表 dispatch。

## 3.7 解釋器與執行狀態

核心狀態：

```c
int exitReq;
int errorFlag;
cell next;
cell lastIp;
cell quit_address;
cell commandAddress;
cell maxBuiltinAddress;
```

`QUIT` 是主要 outer interpreter，也含有 inner interpreter 的 dispatch。

流程大致是：

1. `WORD` 讀取下一個 token；
2. `FIND` 查詞典；
3. 若找到詞：
   - 編譯狀態且非 immediate：編譯其 CFA 或 builtin ID；
   - 否則立即執行；
4. 若找不到詞：
   - 嘗試用目前 `BASE` 解析數字；
   - 編譯狀態下編譯 `LIT` 與數值；
   - 直譯狀態下 push 數值；
5. 若錯誤，重置資料堆疊與返回堆疊。

## 3.8 `DOCOL` 與返回堆疊

`lbforth9.c` 有 return stack：

```c
cell rpop(void);
void rpush(cell data);
```

`DOCOL`：

```c
BUILTIN(0, "RUNDOCOL", docol, 0) {
    rpush(lastIp);
    next = commandAddress + CELL_SIZE;
}
```

`EXIT`：

```c
BUILTIN(7, "EXIT", doExit, 0) {
    next = rpop();
}
```

這模擬了 Jonesforth 的冒號定義呼叫機制：

- 呼叫 colon word 時保存舊 instruction pointer；
- 將 instruction pointer 切到新詞 body；
- `EXIT` 時恢復舊 instruction pointer。

## 3.9 自舉腳本 `initScript`

`lbforth9.c` 內嵌一段 Forth 程式：

```c
char const* initScript =
    ": DECIMAL 10 BASE ! ;\n"
    ...;
```

輸入函式：

```c
int llkey(void) {
    if (*initscript_pos) return *(initscript_pos++);
    return getchar();
}
```

因此啟動後會先把 `initScript` 餵給 Forth 解釋器，等腳本跑完才開始讀標準輸入。

`initScript` 定義了許多高階詞，例如：

- `DECIMAL`
- `HEX`
- `OCTAL`
- `2DUP`
- `2DROP`
- `NIP`
- `TUCK`
- `/`
- `MOD`
- `CR`
- `SPACE`
- `NEGATE`
- `CELLS`
- `ALLOT`
- `TRUE`
- `FALSE`
- `IF THEN ELSE`
- `BEGIN UNTIL AGAIN WHILE REPEAT`
- `DO LOOP +LOOP I`
- `SPACES`
- `ABS`
- `.`
- `.S`
- `TYPE`
- `s"`
- `."`
- `CONSTANT`
- `VARIABLE`

這保留了 Forth 的核心精神：C 只提供最小 kernel，其餘語言層由 Forth 自己長出來。

## 3.10 錯誤處理

比 `forth.c` 更完整：

- data stack underflow；
- data stack overflow；
- return stack underflow；
- return stack overflow；
- memory address 檢查；
- builtin ID 超限；
- builtin ID 重複；
- unknown word；
- 部分 arithmetic overflow。

仍需注意：

- `readMem/writeMem` 的邊界檢查可以更嚴格；
- 指標轉型可能有對齊與 strict aliasing 風險；
- `short` cell 下地址空間與符號行為需要小心。

---

## 4. Jonesforth 詳細梳理

## 4.1 文件組成

`jonesforth/` 包含：

```text
jonesforth.S
jonesforth.f
README
```

`README` 說明這是 Richard W.M. Jones 的 literate x86 assembly Forth 實作鏡像。

## 4.2 `jonesforth.S` 的定位

`jonesforth.S` 是 Linux/i386 上的 Forth compiler/tutorial。

它既是程式，也是教程，詳細解釋：

- Forth 的動機；
- 詞典 layout；
- direct threaded code；
- indirect threaded code；
- `NEXT`；
- `DOCOL`；
- data stack；
- return stack；
- primitive 定義；
- outer interpreter；
- compiler；
- system calls。

構建方式：

```text
gcc -m32 -nostdlib -static -Wl,-Ttext,0 -Wl,--build-id=none -o jonesforth jonesforth.S
```

執行方式：

```text
cat jonesforth.f - | ./jonesforth
```

這代表它強綁定：

- Linux；
- i386；
- GNU assembler；
- 32-bit toolchain。

## 4.3 真正的 `NEXT`

Jonesforth 的核心：

```asm
.macro NEXT
    lodsl
    jmp *(%eax)
.endm
```

其中：

- `%esi` 是 Forth instruction pointer；
- `lodsl` 從 `%esi` 取一個 cell 到 `%eax`，並讓 `%esi += 4`；
- `jmp *(%eax)` 跳到該詞 codeword 指向的機器碼。

這是真正的 indirect threaded code。

## 4.4 `DOCOL`

Jonesforth 的 `DOCOL`：

```asm
DOCOL:
    PUSHRSP %esi
    addl $4,%eax
    movl %eax,%esi
    NEXT
```

當執行冒號定義詞時：

1. 把舊 `%esi` 保存到 return stack；
2. `%eax` 指向該詞 codeword；
3. `addl $4,%eax` 得到 body 第一項；
4. `%esi` 切到 body；
5. `NEXT` 執行 body 第一個詞。

這是 Forth 冒號定義呼叫的經典模型。

## 4.5 暫存器分工

Jonesforth 直接使用 i386 暫存器作為 Forth VM 狀態：

```text
%esp = data stack
%ebp = return stack
%esi = instruction pointer
%eax = current target / temporary
```

這非常高效，也非常不可移植。

## 4.6 詞典 macro

Jonesforth 使用 assembly macro 定義詞典項：

```asm
defword
defcode
```

含義：

- `defword`：定義 Forth 寫成的詞，codeword 指向 `DOCOL`；
- `defcode`：定義 assembly primitive，codeword 指向該 primitive 的機器碼。

詞典 layout：

```text
link field
length/flags byte
name bytes
padding
codeword
body...
```

這與 `lbforth9.c` 的 layout 高度相似。

## 4.7 `jonesforth.S` 的 primitive

`jonesforth.S` 定義大量底層詞，包括：

- stack：`DROP SWAP DUP OVER ROT -ROT 2DROP 2DUP 2SWAP ?DUP`
- arithmetic：`+ - * /MOD 1+ 1- 4+ 4-`
- comparison：`= <> < > <= >= 0= 0<> 0< 0> 0<= 0>=`
- bit：`AND OR XOR INVERT`
- memory：`! @ +! -! C! C@ C@C! CMOVE`
- return stack：`>R R> RSP@ RSP! RDROP`
- data stack introspection：`DSP@ DSP!`
- I/O：`KEY EMIT`
- parser/compiler：`WORD NUMBER FIND >CFA >DFA CREATE , [ ] : ; IMMEDIATE HIDDEN HIDE ' CHAR`
- branch/string：`BRANCH 0BRANCH LITSTRING TELL`
- interpreter：`QUIT INTERPRET EXECUTE`
- Linux system：`SYSCALL0 SYSCALL1 SYSCALL2 SYSCALL3`

## 4.8 `jonesforth.f`

`jonesforth.f` 是高階 Forth 自舉層。

它定義了大量由 primitive 組合出來的詞，包括：

- arithmetic helpers：`/ MOD NEGATE`
- constants：`BL '\n'`
- boolean：`TRUE FALSE NOT`
- compiler words：`LITERAL [COMPILE] RECURSE`
- control flow：`IF THEN ELSE BEGIN UNTIL AGAIN WHILE REPEAT UNLESS`
- comments：`( ... )`
- stack helpers：`NIP TUCK PICK`
- base：`DECIMAL HEX`
- printing：`U. . .R U.R .S`
- strings：`S" ."`
- data definition：`CONSTANT VARIABLE VALUE TO +TO`
- dictionary introspection：`WORDS ID. ?HIDDEN ?IMMEDIATE`
- forgetting：`FORGET`
- memory dump：`DUMP`
- case：`CASE OF ENDOF ENDCASE`
- decompiler：`SEE`
- anonymous words：`:NONAME`
- quotations：`[']`
- exceptions：`CATCH THROW ABORT`
- stack trace：`PRINT-STACK-TRACE`
- C strings：`Z" STRLEN CSTRING`
- argv/env：`ARGC ARGV ENVIRON`
- file I/O：`OPEN-FILE CREATE-FILE CLOSE-FILE READ-FILE PERROR`
- memory growth：`BRK MORECORE UNUSED`
- inline assembler：`;CODE NEXT EAX ECX ... PUSH POP`
- inlining：`INLINE`

Jonesforth 因此不只是小解釋器，而是一個教學型、可自省、可擴展的完整小 Forth 環境。

---

## 5. `forth.c` 與 `lbforth9.c` 橫向對比

| 維度 | `forth.c` | `lbforth9.c` |
|---|---|---|
| 定位 | 教學型小解釋器 | 微型但較完整的 Forth 系統 |
| 程式規模 | 很短 | 明顯更大 |
| 依賴 | `stdio.h`、`stdlib.h`、`string.h` | 幾乎只依賴 `stdio.h` |
| 記憶體管理 | `malloc/realloc/strdup` | 固定 `memory[65536]`，無 malloc |
| 詞典表示 | C `struct Word` 鏈表 | 線性記憶體中的 Forth 詞典 |
| primitive 表示 | 詞條直接持有 C 函式指標 | 詞典存 builtin ID，C 陣列映射函式 |
| 冒號定義 | `Cell *body` 動態陣列 | 編譯進 Forth 記憶體 |
| 執行模型 | C 遞迴執行 body | `next/lastIp` + return stack |
| 返回堆疊 | 無 | 有 |
| 資料堆疊 | C 陣列 `ds[256]` | 位於 `memory` 中 |
| 編譯狀態 | C 全域 `compiling` | Forth 變數 `STATE` |
| `HERE/LATEST/BASE` | 無正式詞 | 有正式 Forth 變數 |
| 數字解析 | 固定十進位 | 依 `BASE`，支援 double |
| 控制流 | C primitive 直接 patch body | 多數由 initScript 用 Forth 定義 |
| 字串 | `."` 直接讀取並輸出 | `s"` 編譯字串，`."` 用 `TYPE` |
| `CREATE/VARIABLE/CONSTANT` | 無 | 有 |
| `IMMEDIATE` | C 層 `imm` flag，無一般 `IMMEDIATE` 詞 | 有 `IMMEDIATE` |
| 註解 | 無 | 支援 `\` 行註解與 `( ... )` |
| 錯誤處理 | 極少 | 較完整 |
| 可攜性 | `strcasecmp/strdup` 可能麻煩 | 刻意低依賴，適合移植 |
| 可理解性 | 非常容易讀 | 需要理解 Forth memory/threading |
| 可擴展性 | 適合加少量 primitive | 更適合用 Forth 自身擴展 |
| 正統 Forth 味道 | 中等 | 高 |

## 5.1 核心差異

`forth.c` 是 C 主導：

- 詞典是 C struct；
- 詞 body 是 C 動態陣列；
- 控制流由 C 函式 patch；
- primitive 是 C 函式指標；
- 呼叫冒號定義靠 C 遞迴。

`lbforth9.c` 是 Forth 主導：

- 詞典在 Forth 記憶體中；
- `HERE` 指向可編譯區；
- `,` 和 `C,` 真正改變編譯指標；
- `@ ! C@ C!` 能讀寫 Forth 記憶體；
- `CREATE` 建立真正的詞典項；
- 控制流詞多由 Forth 腳本定義；
- `CONSTANT`、`VARIABLE` 等由 Forth 自舉出來。

---

## 6. `lbforth9.c` 與 Jonesforth 橫向對比

| 維度 | `lbforth9.c` | Jonesforth |
|---|---|---|
| 語言 | C | i386 assembly + Forth |
| 文件組成 | 單個 C 文件 | `jonesforth.S` + `jonesforth.f` |
| 目標 | 可移植、低依賴、小型 Forth | 教學、展示真正 Forth 內核 |
| 平台 | 理論上多平台 | Linux/i386 |
| cell | 預設 16-bit `short` | 32-bit i386 word |
| 記憶體 | 固定 `memory[65536]` | 真實進程地址空間/資料段/堆 |
| 詞典 | 放在 C 陣列 `memory` 中 | 真實記憶體中的 linked dictionary |
| primitive 表示 | builtin ID | 真實 codeword 地址 |
| dispatch | C loop + builtin table | `NEXT = lodsl; jmp *(%eax)` |
| threaded code | 模擬 | 真正 indirect threaded code |
| `DOCOL` | C 函式模擬 | assembly 實作 |
| data stack | `memory` 中的 cell 陣列 | CPU `%esp` |
| return stack | `memory` 中的 cell 陣列 | CPU `%ebp` |
| input | `getchar` + 內嵌 initScript | Linux read syscall + 外部 `.f` |
| output | `putchar` | Linux write syscall |
| syscalls | 無通用 syscall 詞 | 有 `SYSCALL0..3` |
| 高階 Forth | 內嵌 C 字串 | 獨立 `jonesforth.f` |
| 自舉規模 | 中小 | 很大 |
| introspection | 基本 | `WORDS`、`SEE`、`FORGET` 等 |
| exception | 無完整模型 | 有 `CATCH THROW ABORT` |
| file I/O | 無 | 有 |
| inline assembly | 無 | 有 |
| 教學價值 | 適合看 C 如何模擬 Forth | 適合理解 Forth 真正原理 |
| 可移植性 | 高 | 低 |
| 性能 | 較低 | 較高 |
| 可讀性 | C 程式員較容易讀 | 需懂 i386 與 Forth |

## 6.1 最根本差異：地址機器 vs ID 機器

Jonesforth 詞典中保存的是 codeword 地址。執行時直接跳轉：

```asm
jmp *(%eax)
```

`lbforth9.c` 詞典中對 builtin 保存的是小整數 ID。執行時查表：

```c
builtins[command]();
```

因此：

- Jonesforth 是真正的 indirect threaded code；
- `lbforth9.c` 是 C 層模擬 threaded interpreter。

## 6.2 CPU 使用方式

Jonesforth 把 CPU 暫存器直接分配給 Forth VM：

```text
%esp = data stack
%ebp = return stack
%esi = instruction pointer
%eax = current word
```

`lbforth9.c` 把這些變成 C 變數與 C 陣列：

```text
sp/rsp = stack pointers
next = instruction pointer
command = current word / builtin id
```

前者快、直接、不可移植。後者慢些、間接、容易移植。

## 6.3 高階 Forth 層

`lbforth9.c` 的 `initScript` 明顯借鑑了 `jonesforth.f`，但大幅縮短。

兩者都有：

- `/`
- `MOD`
- `BL`
- `CR`
- `NEGATE`
- `IF THEN ELSE`
- `BEGIN UNTIL AGAIN WHILE REPEAT`
- `SPACES`
- `.`
- `.S`
- `TYPE`
- `S"` / `s"`
- `."`
- `CONSTANT`
- `VARIABLE`

Jonesforth 還有：

- `VALUE`
- `TO`
- `WORDS`
- `FORGET`
- `DUMP`
- `CASE`
- `SEE`
- `:NONAME`
- `[']`
- exception system
- stack trace
- argv/env
- file I/O
- inline assembler
- inlining

所以 `lbforth9.c` 是 Jonesforth 精神的壓縮 C 版，不是功能等價版。

---

## 7. Threaded Code 解釋

## 7.1 基本定義

`threaded code` 是 Forth 常用的一種執行表示法：

> 把一個詞的定義編譯成一串「要執行的詞/程式片段的地址」或索引，而不是編譯成普通機器指令序列。

例如：

```forth
: SQUARE DUP * ;
```

在 threaded code 裡可能被編譯成：

```text
[DUP 的地址] [* 的地址] [EXIT 的地址]
```

執行時，Forth 的內部解釋器逐個取出這些地址，跳過去執行。

這裡的 `threaded` 不是多執行緒，而是「用一根線把很多小程式片段串起來」。

## 7.2 與普通機器碼的差別

普通 C 程式：

```c
f() {
    a();
    b();
    c();
}
```

普通編譯器可能生成：

```asm
call a
call b
call c
ret
```

threaded code 更像：

```text
&a
&b
&c
&exit
```

再由一個極小執行器做：

```c
while (true) {
    word = *ip++;
    execute(word);
}
```

其中 `ip` 是 instruction pointer，指向下一個要執行的 Forth 詞。

## 7.3 `NEXT`

Forth 執行器最核心的動作通常叫 `NEXT`。

概念上：

```c
w = *ip++;
jump_to(w);
```

Jonesforth 的 i386 版本：

```asm
lodsl
jmp *(%eax)
```

它做三件事：

1. 從 `%esi` 指向的位置取出下一個詞地址到 `%eax`；
2. `%esi` 自動前進一個 cell；
3. 跳到該詞 codeword 指向的機器碼。

## 7.4 Direct threaded code

Direct threaded code 中，body 直接保存可執行程式片段地址：

```text
[code_DUP] [code_ADD] [code_EXIT]
```

執行器取出地址後直接跳過去。

概念上：

```c
target = *ip++;
goto *target;
```

優點：

- 快；
- 結構簡單。

缺點：

- 對機器地址與編譯器支援要求高；
- 可移植性差。

## 7.5 Indirect threaded code

Indirect threaded code 多一層間接性。

body 保存的是詞的 code field address：

```text
[DUP 的 CFA] [+ 的 CFA] [EXIT 的 CFA]
```

每個詞的 CFA 裡再存真正要跳的程式碼地址：

```text
DUP:
    codeword -> code_DUP

DOUBLE:
    codeword -> DOCOL
    body     -> DUP + EXIT
```

執行時：

1. 取出 DUP 的 CFA；
2. 讀 CFA 裡的 codeword；
3. 跳到 codeword。

Jonesforth 使用的就是 indirect threaded code。

好處是同一套機制可以處理：

- primitive 詞：codeword 指向原生機器碼；
- colon 詞：codeword 指向 `DOCOL`。

## 7.6 `DOCOL`

對 primitive，例如 `DUP`，codeword 直接指向 primitive 的機器碼。

但對冒號定義：

```forth
: DOUBLE DUP + ;
```

它不是一段原生機器碼，而是一串 threaded code。因此它的 codeword 指向特殊例程 `DOCOL`。

`DOCOL` 做的事：

1. 把目前 `ip` 存到 return stack；
2. 把 `ip` 改成這個冒號定義 body 的開始；
3. 執行 `NEXT`。

概念上：

```c
void DOCOL(word *body) {
    rpush(ip);
    ip = body;
    NEXT();
}
```

遇到 `EXIT` 時：

```c
ip = rpop();
NEXT();
```

所以 colon word 呼叫與返回靠 `DOCOL` 和 `EXIT` 完成。

## 7.7 三個文件中的 threaded code 層次

### Jonesforth

Jonesforth 是真正的 indirect threaded code。

它有：

```asm
NEXT:
    lodsl
    jmp *(%eax)
```

並且：

- `%esi` 是 instruction pointer；
- `%esp` 是 data stack；
- `%ebp` 是 return stack；
- primitive 是 assembly；
- colon word 的 codeword 指向 `DOCOL`。

### `lbforth9.c`

`lbforth9.c` 模仿 threaded code，但用 C 實作。

它有：

```c
cell next;
cell lastIp;
```

builtin 用 ID 表示：

```c
builtins[command]();
```

它保留了：

- `DOCOL`
- `EXIT`
- return stack
- `BRANCH`
- `0BRANCH`
- `LIT`
- code field/body 結構

但沒有真正用 `jmp *(address)`。

### `forth.c`

`forth.c` 更簡化。

它把冒號定義 body 存成：

```text
Word*
Word*
Word*
W_RET
```

核心執行器：

```c
Word *w = (Word *) code[pc++];
```

然後用 C `if` 判斷執行。

它是 threaded-code 風格的簡化 bytecode 解釋器，但不是傳統 threaded Forth，因為它用 C 遞迴呼叫 colon word，而不是 `DOCOL` + return stack。

## 7.8 Threaded code 的優缺點

優點：

1. 編譯器極簡。  
   Forth 編譯一個詞時，通常只要把找到的詞地址寫進 `HERE`。

2. 自舉容易。  
   控制流也能用 Forth 自己定義。

3. 記憶體小。  
   早期機器上比重複 `CALL` 指令更省空間。

4. primitive 和高階詞能統一。  
   primitive 是原生碼，colon word 是地址串，兩者通過 codeword 統一。

缺點：

1. 不如現代原生碼快。  
   每個詞之間都要 dispatch。

2. 真正高效實作可移植性差。  
   常依賴 assembly、computed goto 或特定 ABI。

3. debug 不直觀。  
   執行流不是普通 C call stack，而是 Forth 自己的 `ip` 與 return stack。

4. C 編譯器難以跨詞優化。

---

## 8. 總結

三者可以排成一條清晰的理解路徑：

```text
forth.c
  ↓
lbforth9.c
  ↓
Jonesforth
```

或者反過來看成從原典逐步 C 化、簡化：

```text
Jonesforth
  ↓ 提取核心思想，去掉 i386/ Linux 強綁定
lbforth9.c
  ↓ 再簡化為教學型 C struct 解釋器
forth.c
```

最終定位：

- `forth.c`：最容易讀，最適合理解 Forth 基礎概念。
- `lbforth9.c`：最適合理解如何用 C 模擬一個傳統 Forth 內核。
- Jonesforth：最適合理解 Forth 真正的 threaded code、`NEXT`、`DOCOL`、自舉與元編程精神。

如果目標是學習：

- 先讀 `forth.c`，理解詞典、堆疊、冒號定義、literal、branch。
- 再讀 `lbforth9.c`，理解 `memory`、`HERE`、`LATEST`、`STATE`、return stack、`DOCOL`。
- 最後讀 Jonesforth，理解真正 machine-level threaded code 和 Forth 自舉哲學。

最短結論：

> `forth.c` 是簡化模型。  
> `lbforth9.c` 是 C 化的 Forth kernel。  
> Jonesforth 是真正貼著機器跑的 Forth 原典。  
> Threaded code 是 Forth 能用極小 kernel 長出整個語言的核心技術。
