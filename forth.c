#include <stdio.h>        /* 標準輸入輸出：scanf、printf、putchar、puts、stderr。 */
#include <stdlib.h>       /* 通用工具：malloc、realloc、strtol。 */
#include <string.h>       /* 字串工具：strdup；strcasecmp 在部分平台也由此宣告。 */

typedef long        Cell; /* Forth 的一個儲存單位；同時承載整數與轉型後的指標。 */

typedef struct Word Word; /* 先宣告 Word，讓結構內可持有指向下一個 Word 的指標。 */

struct Word {             /* 字典中的一個詞條：Forth 裡每個可執行名字都是一個 Word。 */
	char *name;           /* 詞名，例如 "+"、"dup"、使用者定義的 "square"。 */
	int   imm;            /* immediate 標記；為 1 時即使在編譯狀態也立即執行。 */
	void  (*prim)(void);  /* C 實作的原語函式；非 NULL 表示這個詞是 primitive。 */
	Cell *body;           /* 使用者定義詞的編譯結果；內容是 Word* 與立即數混合的執行串。 */
	int   blen;           /* body 目前包含多少個 Cell。 */
	Word *next;           /* 字典以單向鏈結串列串起；新詞插在表頭以支援覆蓋舊詞。 */
};

static Cell  ds [256];                             /* 資料堆疊；Forth 運算主要透過這個 stack 傳值。 */
static int   dsp;                                  /* data stack pointer；指向下一個可寫位置，也等於堆疊深度。 */
static Word *dict;                                 /* 字典表頭；find 會從此處往後找詞。 */
static int   compiling;                            /* 直譯/編譯狀態旗標；0 是立即執行，1 是把詞編入 cur。 */
static Word *cur;                                  /* 目前正在用冒號定義建立的詞；只有 compiling 時有效。 */

static Word *W_LIT, *W_JMP, *W_JZ, *W_RET;         /* 內部偽詞：字面量、跳躍、條件跳躍、返回。 */

void         push(Cell v) { ds [dsp++] = v; }      /* 將值寫到堆疊頂端，再把深度加一。 */

Cell         pop(void) { return ds [--dsp]; }      /* 先將深度減一，再取出原本的頂端值。 */

Cell         peek(void) { return ds [dsp - 1]; }   /* 讀取頂端值但不改變堆疊。 */

Word        *find(char const *s) {                 /* 在字典中查找名字 s。 */
	for (Word *w = dict; w; w = w->next)           /* 從最新詞一路沿 next 掃描到最舊詞。 */
		if (strcasecmp(w->name, s) == 0) return w; /* Forth 通常不分大小寫；找到即返回。 */
	return NULL;                                   /* 找不到時回傳 NULL，交給呼叫者當數字解析。 */
}

Word *mkword(char const *s) {                           /* 建立一個新詞條並插入字典。 */
	Word *w = malloc(sizeof *w);                        /* 配置 Word 結構所需記憶體。 */
	*w      = (Word) {.name = strdup(s), .next = dict}; /* 複製名稱；其他欄位藉初始化歸零。 */
	dict    = w;                                        /* 新詞成為字典表頭，使同名新詞優先被找到。 */
	return w;                                           /* 回傳新詞，供呼叫者補上 prim、imm 或 body。 */
}

void defprim(char const *s, void (*fn)(void), int imm) { /* 登錄一個 C 原語詞。 */
	Word *w = mkword(s);                                 /* 先建立字典詞條。 */
	w->prim = fn;                                        /* 將詞條綁定到 C 函式。 */
	w->imm  = imm;                                       /* 設定是否為 immediate 詞。 */
}

void cc(Cell v) { /* compile cell：把一個 Cell 編入目前詞。 */
	cur->body               = realloc(cur->body, (cur->blen + 1) * sizeof(Cell)); /* body 擴張一格。 */
	cur->body [cur->blen++] = v;                                                  /* 寫入新 Cell，並遞增長度。 */
}

void cw(Word *w) { cc(( Cell ) w); }      /* compile word：把 Word* 轉成 Cell 編入執行串。 */

void run(Cell *code, int len) {           /* 執行一段已編譯的 body。 */
	for (int pc = 0; pc < len;) {         /* pc 是 program counter；每輪取一個 Word。 */
		Word *w = ( Word * ) code [pc++]; /* body 中一般存 Word*；取出後 pc 前進。 */
		if (w == W_RET) return;           /* 遇到返回偽詞，結束這段 body。 */
		if (w == W_LIT) {
			push(code [pc++]);
			continue;
		} /* 字面量：下一格是數值，推入堆疊。 */
		if (w == W_JZ) {
			int d = code [pc++];
			if (!pop()) pc = d;
			continue;
		} /* 條件跳：彈出旗標，為 0 時跳到目標位址。 */
		if (w == W_JMP) {
			pc = code [pc];
			continue;
		} /* 無條件跳：下一格是目標 pc。 */
		if (w->prim) {
			w->prim();
			continue;
		}                                       /* C 原語：直接呼叫綁定函式。 */
		if (w->body) { run(w->body, w->blen); } /* 使用者定義詞：遞迴執行它的 body。 */
	}
}

void exec(Word *w) {                         /* 在直譯狀態執行單一詞。 */
	if (w->prim) w->prim();                  /* 原語詞直接呼叫 C 函式。 */
	else if (w->body) run(w->body, w->blen); /* 冒號定義詞交給 run 執行其編譯串。 */
}

/* --- primitives：以下函式都是 Forth 詞的 C 實作，透過資料堆疊取參數與回傳值。 --- */
static void p_add(void) {
	Cell b = pop(), a = pop();
	push(a + b);
} /* + ：取 a b，推入 a+b。 */

static void p_sub(void) {
	Cell b = pop(), a = pop();
	push(a - b);
} /* - ：取 a b，推入 a-b。 */

static void p_mul(void) {
	Cell b = pop(), a = pop();
	push(a * b);
} /* * ：取 a b，推入 a*b。 */

static void p_div(void) {
	Cell b = pop(), a = pop();
	push(a / b);
} /* / ：取 a b，推入 a/b。 */

static void p_mod(void) {
	Cell b = pop(), a = pop();
	push(a % b);
}                                                    /* mod：取 a b，推入 a%b。 */

static void p_dot(void) { printf("%ld ", pop()); }   /* . ：彈出並以十進位列印。 */

static void p_emit(void) { putchar(( int ) pop()); } /* emit：彈出數值並當字元輸出。 */

static void p_cr(void) { putchar('\n'); }            /* cr：輸出換行。 */

static void p_dup(void) { push(peek()); }            /* dup：複製堆疊頂端。 */

static void p_drop(void) { pop(); }                  /* drop：丟棄堆疊頂端。 */

static void p_swap(void) {
	Cell b = pop(), a = pop();
	push(b);
	push(a);
}                                                /* swap：交換頂端兩個值。 */

static void p_over(void) { push(ds [dsp - 2]); } /* over：複製次頂端值到頂端。 */

static void p_rot(void) {
	Cell c = pop(), b = pop(), a = pop();
	push(b);
	push(c);
	push(a);
} /* rot：a b c 變成 b c a。 */

static void p_eq(void) {
	Cell b = pop(), a = pop();
	push(a == b ? -1 : 0);
} /* = ：相等推 -1，否則 0；沿用 Forth 的 true 全位元為 1。 */

static void p_lt(void) {
	Cell b = pop(), a = pop();
	push(a < b ? -1 : 0);
} /* < ：a 小於 b 時為 true。 */

static void p_gt(void) {
	Cell b = pop(), a = pop();
	push(a > b ? -1 : 0);
}                                         /* > ：a 大於 b 時為 true。 */

static void p_inv(void) { push(~pop()); } /* invert：逐位元反相。 */

static void p_and(void) {
	Cell b = pop(), a = pop();
	push(a & b);
} /* and：逐位元 AND。 */

static void p_or(void) {
	Cell b = pop(), a = pop();
	push(a | b);
}                                                         /* or：逐位元 OR。 */

static void p_ds(void) {                                  /* .s：檢視整個資料堆疊。 */
	printf("<%d> ", dsp);                                 /* 先輸出目前堆疊深度。 */
	for (int i = 0; i < dsp; i++) printf("%ld ", ds [i]); /* 由底到頂列出每個 Cell。 */
	putchar('\n');                                        /* 以換行結束顯示。 */
}

static char tok [64];                    /* 輸入 token 緩衝區；scanf 最多讀 63 字元並保留 NUL。 */

static void p_colon(void) {              /* : ：開始一個新的冒號定義。 */
	if (scanf("%63s", tok) != 1) return; /* 讀取下一個 token 作為新詞名字；失敗則放棄。 */
	cur       = mkword(tok);             /* 在字典中建立新詞，接下來把 body 編進它。 */
	compiling = 1;                       /* 進入編譯狀態：普通詞不執行，而是編入 cur。 */
}

static void p_semi(void) {
	cw(W_RET);
	compiling = 0;
	cur       = NULL;
} /* ; ：編入返回，離開編譯狀態。 */

static void p_if(void) {
	cw(W_JZ);
	cc(0);
	push(cur->blen - 1);
}                                 /* if：編入條件跳，先留下待回填洞位。 */

static void p_else(void) {        /* else：結束 if 的真分支，開始假分支。 */
	cw(W_JMP);
	cc(0);                        /* 真分支尾端需要無條件跳過假分支。 */
	Cell hole        = pop();     /* 取出 if 留下的條件跳目標洞位。 */
	cur->body [hole] = cur->blen; /* if 為 false 時跳到 else 後第一格。 */
	push(cur->blen - 1);          /* 保存 jmp 的洞位，等 then 回填。 */
}

static void p_then(void) { cur->body [pop()] = cur->blen; } /* then：把最近的洞位回填為目前 body 結尾。 */

static void p_begin(void) { push(cur->blen); }              /* begin：記錄迴圈起點 pc。 */

static void p_until(void) {
	cw(W_JZ);
	cc(pop());
} /* until：條件為 false 時跳回 begin。 */

static void p_again(void) {
	cw(W_JMP);
	cc(pop());
} /* again：無條件跳回 begin。 */

static void p_while(void) {
	cw(W_JZ);
	cc(0);
	push(cur->blen - 1);
}                                /* while：條件為 false 時跳出，目標稍後由 repeat 回填。 */

static void p_repeat(void) {     /* repeat：完成 begin while repeat 結構。 */
	Cell wh = pop(), bg = pop(); /* wh 是 while 洞位，bg 是 begin 起點。 */
	cw(W_JMP);
	cc(bg);                      /* 迴圈體末端跳回 begin。 */
	cur->body [wh] = cur->blen;  /* while 為 false 時跳到 repeat 之後。 */
}

static void p_lit(void) {
	cw(W_LIT);
	cc(pop());
}                                   /* literal：把編譯期堆疊頂端值編成執行期字面量。 */

static void p_dotq(void) {          /* ." ：立即輸出直到下一個雙引號的字串。 */
	int c;                          /* getchar 回傳 int，才能表示 EOF。 */
	while ((c = getchar()) == ' '); /* 跳過開頭空白，取得字串第一個字元。 */
	while (c != '"' && c != EOF) {
		putchar(c);
		c = getchar();
	} /* 持續輸出直到遇到結束引號或 EOF。 */
}

void init(void) {                   /* 初始化字典，註冊所有內建詞。 */
	defprim("+", p_add, 0);
	defprim("-", p_sub, 0);         /* 算術加減。 */
	defprim("*", p_mul, 0);
	defprim("/", p_div, 0);         /* 算術乘除。 */
	defprim("mod", p_mod, 0);
	defprim(".", p_dot, 0);         /* 取餘與數字輸出。 */
	defprim("emit", p_emit, 0);
	defprim("cr", p_cr, 0);         /* 字元輸出與換行。 */
	defprim("dup", p_dup, 0);
	defprim("drop", p_drop, 0);     /* 基本堆疊操作。 */
	defprim("swap", p_swap, 0);
	defprim("over", p_over, 0);     /* 雙元素堆疊操作。 */
	defprim("rot", p_rot, 0);
	defprim("=", p_eq, 0);          /* 三元素旋轉與相等比較。 */
	defprim("<", p_lt, 0);
	defprim(">", p_gt, 0);          /* 大小比較。 */
	defprim("invert", p_inv, 0);
	defprim("and", p_and, 0);       /* 位元反相與 AND。 */
	defprim("or", p_or, 0);
	defprim(".s", p_ds, 0);         /* 位元 OR 與堆疊列印。 */
	defprim(".\"", p_dotq, 0);      /* 字串輸出詞。 */
	defprim(":", p_colon, 0);
	defprim(";", p_semi, 1);        /* 冒號定義；分號必須 immediate。 */
	defprim("if", p_if, 1);
	defprim("else", p_else, 1);     /* 條件控制詞；編譯時改寫控制流。 */
	defprim("then", p_then, 1);
	defprim("begin", p_begin, 1);   /* 條件結束與迴圈起點。 */
	defprim("until", p_until, 1);
	defprim("again", p_again, 1);   /* 迴圈出口與無限迴圈回跳。 */
	defprim("while", p_while, 1);
	defprim("repeat", p_repeat, 1); /* begin while repeat 結構。 */
	defprim("literal", p_lit, 1);   /* 編譯期把數值塞入執行期程式。 */

	W_LIT = mkword("(lit)");
	W_JMP = mkword("(jmp)"); /* 建立內部偽詞：字面量與無條件跳。 */
	W_JZ  = mkword("(jz)");
	W_RET = mkword("(ret)"); /* 建立內部偽詞：零跳與返回。 */
}

int main(void) {                             /* 程式入口：一個簡單的 token 直譯器。 */
	init();                                  /* 先建立內建字典。 */
	puts("ok");                              /* 顯示啟動完成提示。 */
	while (scanf("%63s", tok) == 1) {        /* 持續從標準輸入讀取以空白分隔的 token。 */
		Word *w = find(tok);                 /* 先嘗試把 token 當作 Forth 詞名。 */
		if (w) {                             /* 若字典中有這個詞，依目前狀態處理。 */
			if (compiling && !w->imm) cw(w); /* 編譯狀態下，非 immediate 詞只編入 body。 */
			else exec(w);                    /* 直譯狀態或 immediate 詞則立即執行。 */
			continue;                        /* 詞已處理完，讀下一個 token。 */
		}
		char *end;                           /* strtol 會把解析停止處寫入 end。 */
		Cell  n = strtol(tok, &end, 10);     /* 若不是詞，嘗試按十進位整數解析。 */
		if (*end == '\0')                    /* end 指向字串結尾，表示整個 token 都是數字。 */
			if (compiling) {
				cw(W_LIT);
				cc(n);
			}             /* 編譯中：把數字編為 W_LIT + 數值。 */
			else push(n); /* 直譯中：數字直接推入資料堆疊。 */
		else /* 既不是已知詞，也不是合法整數。 */ fprintf(stderr, "? %s\n", tok); /* 以 Forth 風格報告未知 token。 */
	}
}
