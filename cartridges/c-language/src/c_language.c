/*
 * PRG32 C - a small C interpreter cartridge for the PRG32 console.
 *
 * Type a C program with a Bluetooth keyboard, press F5, and watch it run.
 * The cartridge contains four classic parts of a language system:
 *
 *   1. an EDITOR with syntax colouring (the program source lives in src[]);
 *   2. a LEXER that cuts the source into tokens (next());
 *   3. a one-pass COMPILER (expr(), stmt(), ...) that turns tokens directly
 *      into instructions for a small stack machine, in the spirit of
 *      Robert Swierczek's "C in four functions" (c4);
 *   4. a VIRTUAL MACHINE (vm_run_batch()) that executes those instructions.
 *
 * The virtual machine never touches real RISC-V addresses: C pointers are
 * byte offsets inside the private array vm_mem[], and every load, store and
 * stack push is bounds-checked. A buggy student program therefore stops with
 * a readable error instead of crashing the console.
 *
 * The VM runs a slice of instructions in every update() call and can pause
 * inside getchar(), readline(), sleep() and frame(), so programs can wait for
 * keys or animate graphics while the PRG32 main loop keeps running.
 *
 * Supported C subset: int, char, pointers, 1-D arrays, functions with
 * recursion and prototypes, if/else, while, do/while, for, break, continue,
 * return, the complete C operator set (including ?:, compound assignment and
 * pointer arithmetic), sizeof(type), casts, enum, #define NAME constant,
 * string and character literals, // and block comments. Local variables are
 * visible from their declaration to the end of the function.
 *
 * Cartridge rules followed here: one C file, no C library (the few helpers
 * needed are written below), no switch jump tables, and no initialized
 * tables of pointers, because portable cartridges are not relocated.
 */

#include "prg32.h"

#include <stddef.h>
#include <stdint.h>

#ifndef CL_TRACE_PUTC
#define CL_TRACE_PUTC(c) ((void)0) /* host tests capture console output */
#endif

/* ======================================================================== */
/* Sizes (the cartridge must fit the 64 KiB executable RAM profile)         */
/* ======================================================================== */

#define CL_SRC_MAX 5120      /* program text, bytes                      */
#define CL_CODE_MAX 2560     /* compiled program, 32-bit words           */
#define CL_MEM_SIZE 10240    /* VM memory: globals, strings, heap, stack */
#define CL_MEM_NULL_GUARD 16 /* addresses below this are invalid         */
#define CL_STACK_MIN 1024    /* bytes always kept free for the stack     */
#define CL_SYM_MAX 160       /* names: keywords, built-ins, user names   */
#define CL_LINES_MAX 384     /* code address -> source line table        */
#define CL_LOOP_MAX 16       /* nested loops                             */
#define CL_DEPTH_MAX 20      /* nested expressions + statements          */
#define CL_SLICE_MS 12u      /* VM time per frame                        */

#define CON_COLS 40
#define CON_ROWS 24          /* row 24 of the 320x200 view is the status */
#define ED_ROWS 24           /* editor text rows below the title bar     */
#define KEYQ_LEN 32
#define HL_MAX 160           /* highlighted characters per line          */

/* ======================================================================== */
/* Freestanding helpers                                                     */
/* ======================================================================== */

/* Keep GCC from turning these loops back into memset/memcpy calls. */
#if defined(__GNUC__) && !defined(__clang__)
#define CL_NO_LIBCALL __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define CL_NO_LIBCALL
#endif

CL_NO_LIBCALL static void cl_fill(void *dst, int value, int n) {
  unsigned char *d = (unsigned char *)dst;
  while (n-- > 0) {
    *d++ = (unsigned char)value;
  }
}

CL_NO_LIBCALL static void cl_move(void *dst, const void *src, int n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  if (d < s) {
    while (n-- > 0) {
      *d++ = *s++;
    }
  } else if (d > s) {
    while (n-- > 0) {
      d[n] = s[n];
    }
  }
}

#ifndef CL_HOST_TEST
/* GCC may emit calls to these for structure copies even in freestanding
 * mode, and portable cartridges are linked without a C library. */
void *memset(void *dst, int value, size_t n) {
  cl_fill(dst, value, (int)n);
  return dst;
}
void *memcpy(void *dst, const void *src, size_t n) {
  cl_move(dst, src, (int)n);
  return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
  cl_move(dst, src, (int)n);
  return dst;
}
#endif

static int cl_strlen(const char *s) {
  int n = 0;
  while (s[n]) {
    n++;
  }
  return n;
}

static int cl_memeq(const char *a, const char *b, int n) {
  for (int i = 0; i < n; ++i) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

static int is_alpha(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(int c) {
  return c >= '0' && c <= '9';
}

/* Is the word s[0..len) one of the space-separated words in list? Returns
 * its index or -1. Used for keywords, built-ins and syntax colouring. */
static int word_index(const char *list, const char *s, int len) {
  int index = 0;
  while (*list) {
    int n = 0;
    while (list[n] && list[n] != ' ') {
      n++;
    }
    if (n == len && cl_memeq(list, s, len)) {
      return index;
    }
    list += n;
    while (*list == ' ') {
      list++;
    }
    index++;
  }
  return -1;
}

/* Small decimal formatter for status lines (no printf in cartridges). */
static int fmt_uint(char *out, uint32_t v) {
  char tmp[12];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + v % 10u);
    v /= 10u;
  } while (v);
  for (int i = 0; i < n; ++i) {
    out[i] = tmp[n - 1 - i];
  }
  out[n] = '\0';
  return n;
}

/* Append text to a fixed buffer, always NUL terminated. */
static void str_append(char *dst, int cap, const char *text) {
  int n = cl_strlen(dst);
  while (*text && n + 1 < cap) {
    dst[n++] = *text++;
  }
  dst[n] = '\0';
}

static void str_append_uint(char *dst, int cap, uint32_t v) {
  char num[12];
  fmt_uint(num, v);
  str_append(dst, cap, num);
}

/* ======================================================================== */
/* Colours                                                                  */
/* ======================================================================== */

/* The 16 classic CGA colours for textcolor(0..15), as RGB565. */
static const uint16_t k_con_palette[16] = {
    0x0000, 0x0015, 0x0540, 0x0555, 0xA800, 0xA815, 0xAAA0, 0xAD55,
    0x52AA, 0x52BF, 0x57EA, 0x57FF, 0xFAAA, 0xFABF, 0xFFEA, 0xFFFF,
};

/* Syntax colouring classes. */
enum { HL_TEXT, HL_KEYWORD, HL_BUILTIN, HL_NUMBER, HL_STRING, HL_COMMENT,
       HL_PREPROC, HL_CONST };
static const uint16_t k_hl_colors[8] = {
    0xFFFF, 0xFFEA, 0xFABF, 0x57FF, 0x57EA, 0x8410, 0xFD20, 0x07FF,
};

#define COLOR_BAR_BG 0x0015
#define COLOR_BAR_FG 0xFFFF
#define COLOR_ERROR_BG 0xA800

/* ======================================================================== */
/* Word lists shared by the compiler and the syntax colouring               */
/* ======================================================================== */

/* Keyword order must match the T_CHAR..T_VOID tokens below. */
static const char k_keywords[] =
    "char else enum if int return sizeof while for do break continue void";
static const char k_int_words[] = "long short";
static const char k_skip_words[] =
    "const static unsigned signed register volatile extern";

/* Built-in functions: the order must match the SYS_* numbers below. */
static const char k_builtins[] =
    "putchar puts printf getchar getkey readline strlen strcmp strcpy strcat "
    "atoi abs rand srand malloc free memset exit cls gotoxy textcolor clear "
    "pixel rect line text rgb frame btn keydown ms sleep note";
/* Argument count of each built-in; 255 = variable (printf). */
static const uint8_t k_builtin_argc[] = {
    1, 1, 255, 0, 0, 2, 1, 2, 2, 2, 1, 1, 0, 1, 1, 1, 3,
    1, 0, 2, 1, 1, 3, 5, 5, 4, 3, 0, 0, 1, 0, 1, 2,
};

/* Predefined constants and their values. */
static const char k_constants[] =
    "KEY_UP KEY_DOWN KEY_LEFT KEY_RIGHT KEY_ENTER KEY_ESC KEY_BACKSPACE "
    "KEY_TAB BTN_LEFT BTN_RIGHT BTN_UP BTN_DOWN BTN_A BTN_B BTN_SELECT "
    "BLACK WHITE RED GREEN BLUE YELLOW CYAN MAGENTA NULL";
static const int32_t k_constant_values[] = {
    PRG32_KEY_UP, PRG32_KEY_DOWN, PRG32_KEY_LEFT, PRG32_KEY_RIGHT,
    PRG32_KEY_ENTER, PRG32_KEY_ESCAPE, PRG32_KEY_BACKSPACE, PRG32_KEY_TAB,
    PRG32_BTN_LEFT, PRG32_BTN_RIGHT, PRG32_BTN_UP, PRG32_BTN_DOWN,
    PRG32_BTN_A, PRG32_BTN_B, PRG32_BTN_SELECT,
    PRG32_COLOR_BLACK, PRG32_COLOR_WHITE, PRG32_COLOR_RED, PRG32_COLOR_GREEN,
    PRG32_COLOR_BLUE, PRG32_COLOR_YELLOW, PRG32_COLOR_CYAN,
    PRG32_COLOR_MAGENTA, 0,
};

enum {
  SYS_PUTCHAR, SYS_PUTS, SYS_PRINTF, SYS_GETCHAR, SYS_GETKEY, SYS_READLINE,
  SYS_STRLEN, SYS_STRCMP, SYS_STRCPY, SYS_STRCAT, SYS_ATOI, SYS_ABS,
  SYS_RAND, SYS_SRAND, SYS_MALLOC, SYS_FREE, SYS_MEMSET, SYS_EXIT, SYS_CLS,
  SYS_GOTOXY, SYS_TEXTCOLOR, SYS_CLEAR, SYS_PIXEL, SYS_RECT, SYS_LINE,
  SYS_TEXT, SYS_RGB, SYS_FRAME, SYS_BTN, SYS_KEYDOWN, SYS_MS, SYS_SLEEP,
  SYS_NOTE, SYS_COUNT
};

/* ======================================================================== */
/* Example programs (F3 cycles through them)                                */
/* ======================================================================== */

/* One string, examples separated by '\0': no pointer table is needed. */
static const char k_examples[] =
    "/* Hello, PRG32! Edit me, then press F5.\n"
    "   F3 = next example, F1 = help. */\n"
    "int main() {\n"
    "  int i;\n"
    "  printf(\"Hello, PRG32!\\n\\n\");\n"
    "  for (i = 1; i <= 10; i++) {\n"
    "    textcolor(i + 5);\n"
    "    printf(\"%2d x %2d = %3d\\n\", i, i, i * i);\n"
    "  }\n"
    "  textcolor(15);\n"
    "  return 0;\n"
    "}\n"
    "\0"
    "/* Recursion: the Fibonacci numbers */\n"
    "int fib(int n) {\n"
    "  if (n < 2) return n;\n"
    "  return fib(n - 1) + fib(n - 2);\n"
    "}\n"
    "\n"
    "int main() {\n"
    "  int n;\n"
    "  for (n = 0; n < 20; n++)\n"
    "    printf(\"fib(%d) = %d\\n\", n, fib(n));\n"
    "  return 0;\n"
    "}\n"
    "\0"
    "/* Keyboard input: guess the number */\n"
    "int main() {\n"
    "  char line[16];\n"
    "  int secret = rand() % 100 + 1;\n"
    "  int tries = 0, guess = 0;\n"
    "  printf(\"I think of a number 1..100\\n\");\n"
    "  while (guess != secret) {\n"
    "    printf(\"Your guess? \");\n"
    "    readline(line, 16);\n"
    "    guess = atoi(line);\n"
    "    tries++;\n"
    "    if (guess < secret) printf(\"Too small!\\n\");\n"
    "    else if (guess > secret) printf(\"Too big!\\n\");\n"
    "  }\n"
    "  printf(\"Right in %d tries!\\n\", tries);\n"
    "  return 0;\n"
    "}\n"
    "\0"
    "/* Pointers and strings */\n"
    "void reverse(char *s) {\n"
    "  char *e = s + strlen(s) - 1;\n"
    "  while (s < e) {\n"
    "    char c = *s;\n"
    "    *s++ = *e;\n"
    "    *e-- = c;\n"
    "  }\n"
    "}\n"
    "\n"
    "int main() {\n"
    "  char word[32];\n"
    "  strcpy(word, \"RISC-V rocks\");\n"
    "  printf(\"%s\\n\", word);\n"
    "  reverse(word);\n"
    "  printf(\"%s\\n\", word);\n"
    "  return 0;\n"
    "}\n"
    "\0"
    "/* Arrays: bubble sort */\n"
    "int data[10] = {42, 7, 19, 3, 88, 25, 61, 14, 5, 30};\n"
    "\n"
    "void show(int *a, int n) {\n"
    "  int i;\n"
    "  for (i = 0; i < n; i++) printf(\"%d \", a[i]);\n"
    "  printf(\"\\n\");\n"
    "}\n"
    "\n"
    "int main() {\n"
    "  int i, j, t;\n"
    "  show(data, 10);\n"
    "  for (i = 0; i < 9; i++)\n"
    "    for (j = 0; j < 9 - i; j++)\n"
    "      if (data[j] > data[j + 1]) {\n"
    "        t = data[j];\n"
    "        data[j] = data[j + 1];\n"
    "        data[j + 1] = t;\n"
    "      }\n"
    "  show(data, 10);\n"
    "  return 0;\n"
    "}\n"
    "\0"
    "/* Graphics: LEFT/RIGHT move the paddle,\n"
    "   ESC stops. frame() waits for the next frame. */\n"
    "#define W 320\n"
    "#define H 200\n"
    "\n"
    "int main() {\n"
    "  int x = 160, y = 40, dx = 3, dy = 2, px = 140;\n"
    "  int score = 0;\n"
    "  char msg[20];\n"
    "  while (1) {\n"
    "    if (keydown(KEY_LEFT) && px > 0) px -= 6;\n"
    "    if (keydown(KEY_RIGHT) && px < W - 40) px += 6;\n"
    "    x += dx;\n"
    "    y += dy;\n"
    "    if (x < 4 || x > W - 4) dx = -dx;\n"
    "    if (y < 20) dy = -dy;\n"
    "    if (dy > 0 && y > H - 14 && x > px && x < px + 40) {\n"
    "      dy = -dy;\n"
    "      score++;\n"
    "      note(72, 60);\n"
    "    }\n"
    "    if (y > H) { y = 30; score = 0; note(48, 200); }\n"
    "    clear(rgb(0, 0, 64));\n"
    "    rect(px, H - 8, 40, 4, YELLOW);\n"
    "    rect(x - 3, y - 3, 6, 6, RED);\n"
    "    strcpy(msg, \"SCORE \");\n"
    "    msg[6] = '0' + score / 10 % 10;\n"
    "    msg[7] = '0' + score % 10;\n"
    "    msg[8] = 0;\n"
    "    text(4, 4, msg, WHITE);\n"
    "    frame();\n"
    "  }\n"
    "  return 0;\n"
    "}\n"
    "\0";

#define CL_EXAMPLE_COUNT 6

/* One screen of help, lines separated by '\n'. */
static const char k_help[] =
    "PRG32 C - KEYS\n"
    "F5 / CTRL+R  run the program\n"
    "ESC / CTRL+C stop the running program\n"
    "F2 / CTRL+N  new empty program\n"
    "F3 / CTRL+E  load the next example\n"
    "ARROWS HOME END PGUP PGDN  move\n"
    "JOYSTICK: A run, SELECT example\n"
    "\n"
    "BUILT-IN FUNCTIONS\n"
    "putchar puts printf getchar getkey\n"
    "readline(buf,max) atoi strlen strcmp\n"
    "strcpy strcat abs rand srand exit\n"
    "malloc free memset cls gotoxy(x,y)\n"
    "textcolor(0..15) ms() sleep(ms)\n"
    "clear(c) pixel(x,y,c) rect(x,y,w,h,c)\n"
    "line(x0,y0,x1,y1,c) text(x,y,s,c)\n"
    "rgb(r,g,b) frame() btn() keydown(k)\n"
    "note(midi,ms)\n"
    "\n"
    "CONSTANTS: KEY_UP.. KEY_ESC BTN_A..\n"
    "RED GREEN BLUE YELLOW WHITE BLACK..\n"
    "\n"
    "PRESS ANY KEY";

/* ======================================================================== */
/* Global state                                                             */
/* ======================================================================== */

enum { MODE_EDIT, MODE_RUN, MODE_ENDED, MODE_HELP };
static int g_mode;
static uint32_t g_joy_last;

/* Program text. */
static char src[CL_SRC_MAX];
static int src_len;

/* Compiled program and VM memory. */
static int32_t code[CL_CODE_MAX];
static int code_len;
static uint8_t vm_mem[CL_MEM_SIZE];

/* Output console. */
static char con_ch[CON_ROWS][CON_COLS];
static uint8_t con_fg[CON_ROWS][CON_COLS];
static int con_x, con_y;
static uint8_t con_color = 15;
static int con_dirty;

/* Keys typed while a program runs. */
static int keyq[KEYQ_LEN];
static int keyq_head, keyq_tail;

/* ======================================================================== */
/* Output console                                                           */
/* ======================================================================== */

static void con_clear(void) {
  cl_fill(con_ch, ' ', sizeof(con_ch));
  cl_fill(con_fg, con_color, sizeof(con_fg));
  con_x = 0;
  con_y = 0;
  con_dirty = 1;
}

static void con_scroll(void) {
  cl_move(con_ch[0], con_ch[1], (CON_ROWS - 1) * CON_COLS);
  cl_move(con_fg[0], con_fg[1], (CON_ROWS - 1) * CON_COLS);
  cl_fill(con_ch[CON_ROWS - 1], ' ', CON_COLS);
  cl_fill(con_fg[CON_ROWS - 1], con_color, CON_COLS);
  con_y = CON_ROWS - 1;
}

static void con_newline(void) {
  con_x = 0;
  if (++con_y >= CON_ROWS) {
    con_scroll();
  }
}

static void con_putc(int c) {
  CL_TRACE_PUTC(c);
  con_dirty = 1;
  if (c == '\n') {
    con_newline();
  } else if (c == '\r') {
    con_x = 0;
  } else if (c == '\b') {
    if (con_x > 0) {
      con_x--;
      con_ch[con_y][con_x] = ' ';
    }
  } else if (c == '\t') {
    do {
      con_putc(' ');
    } while (con_x % 4 != 0);
  } else if (c >= 32 && c < 256) {
    if (con_x >= CON_COLS) {
      con_newline();
    }
    con_ch[con_y][con_x] = (char)c;
    con_fg[con_y][con_x] = con_color;
    con_x++;
  }
}

/* ======================================================================== */
/* Key queue for running programs                                           */
/* ======================================================================== */

static void keyq_clear(void) {
  keyq_head = 0;
  keyq_tail = 0;
}

static void keyq_push(int key) {
  int next = (keyq_head + 1) % KEYQ_LEN;
  if (next != keyq_tail) {
    keyq[keyq_head] = key;
    keyq_head = next;
  }
}

static int keyq_pop(void) {
  if (keyq_head == keyq_tail) {
    return 0;
  }
  int key = keyq[keyq_tail];
  keyq_tail = (keyq_tail + 1) % KEYQ_LEN;
  return key;
}

/* ======================================================================== */
/* Compiler: tokens, types, instructions, symbols                           */
/* ======================================================================== */

/* Tokens above 127; operators are listed in increasing precedence, which is
 * what the precedence-climbing loop in expr() relies on. */
enum {
  T_NUM = 128, T_FUN, T_SYS, T_GLO, T_LOC, T_ID, T_FWD,
  T_CHAR, T_ELSE, T_ENUM, T_IF, T_INT, T_RETURN, T_SIZEOF, T_WHILE,
  T_FOR, T_DO, T_BREAK, T_CONTINUE, T_VOID, T_SKIP,
  T_ASSIGN, T_COND, T_LOR, T_LAN, T_OR, T_XOR, T_AND, T_EQ, T_NE,
  T_LT, T_GT, T_LE, T_GE, T_SHL, T_SHR, T_ADD, T_SUB, T_MUL, T_DIV, T_MOD,
  T_INC, T_DEC, T_BRAK
};

/* Types: CHAR or INT plus TY_PTR for each level of '*'. */
#define TY_CHAR 0
#define TY_INT 1
#define TY_PTR 2

/* Stack machine instructions. The accumulator `a` holds the latest value;
 * binary operators combine the top of the stack with `a`. OP_OR..OP_MOD are
 * in the same order as T_OR..T_MOD. */
enum {
  OP_LEA, OP_IMM, OP_JMP, OP_JSR, OP_BZ, OP_BNZ, OP_ENT, OP_ADJ, OP_LEV,
  OP_LI, OP_LC, OP_SI, OP_SC, OP_PSH,
  OP_OR, OP_XOR, OP_AND, OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
  OP_SHL, OP_SHR, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_SYS
};

typedef struct {
  const char *name; /* points into src[] or into a word list */
  int32_t val;      /* number, address, frame offset or code address */
  int32_t hval;     /* global meaning saved while a local shadows it */
  uint8_t len;
  uint8_t tk;       /* T_ID or the keyword token */
  uint8_t cls;      /* 0, T_NUM, T_FUN, T_FWD, T_SYS, T_GLO or T_LOC */
  uint8_t type;
  uint8_t array;
  uint8_t hcls, htype, harray;
} sym_t;

static sym_t syms[CL_SYM_MAX];
static int nsyms;

/* Lexer state. */
static int lp;          /* read position in src[] */
static int lline;       /* current source line (1-based) */
static int tk;          /* current token */
static int tk_op;       /* operator of a compound assignment, 0 for '=' */
static int32_t ival;    /* value of a number/character/string token */
static int id;          /* symbol index of an identifier token */

/* Compiler state. */
static int ty;          /* type of the expression just compiled */
static int last_op = -1;/* index of the last emitted instruction */
static int depth;
static int data_top;    /* next free byte for globals and strings */
static int str_open;    /* inside a sequence of adjacent string literals */
static int str_pos;
static int local_size;  /* bytes of locals in the current function */
static int loop_depth;
static int brk_chain[CL_LOOP_MAX];
static int cont_chain[CL_LOOP_MAX];
static int cont_target[CL_LOOP_MAX];

/* Errors (compile and run time). */
static int cerr;
static int err_line;
static char err_msg[48];

/* Code address -> source line, for run-time error messages. */
static uint16_t lm_pc[CL_LINES_MAX];
static uint16_t lm_line[CL_LINES_MAX];
static int lm_count;

static void cl_error(const char *msg) {
  if (cerr) {
    return;
  }
  cerr = 1;
  err_line = lline;
  err_msg[0] = '\0';
  str_append(err_msg, sizeof(err_msg), msg);
  tk = 0;          /* stop the parser: every loop ends at end-of-input */
  lp = src_len;
}

static void cl_error_name(const char *msg, int s) {
  char text[48];
  text[0] = '\0';
  str_append(text, sizeof(text), msg);
  str_append(text, sizeof(text), ": ");
  int n = cl_strlen(text);
  for (int i = 0; i < syms[s].len && n + 1 < (int)sizeof(text); ++i) {
    text[n++] = syms[s].name[i];
  }
  text[n] = '\0';
  cl_error(text);
}

/* ---- instruction emission ---- */

static void emit(int32_t word) {
  if (code_len >= CL_CODE_MAX) {
    cl_error("program too large");
    return;
  }
  code[code_len++] = word;
}

static void emit_op(int op) {
  last_op = code_len;
  emit(op);
}

static void emit_op1(int op, int32_t arg) {
  emit_op(op);
  emit(arg);
}

/* Was the last instruction a load? Then the expression is an lvalue: the
 * load can be replaced by a push of its address (assignment, ++, &). */
static int last_is_load(void) {
  return last_op >= 0 && last_op == code_len - 1 &&
         (code[last_op] == OP_LI || code[last_op] == OP_LC);
}

static void emit_load(void) {
  emit_op(ty == TY_CHAR ? OP_LC : OP_LI);
}

static void emit_store(int type) {
  emit_op(type == TY_CHAR ? OP_SC : OP_SI);
}

/* Patch a chain of jump operands (linked through the operands themselves). */
static void patch_chain(int index, int target) {
  while (index >= 0) {
    int next_index = code[index];
    code[index] = target;
    index = next_index;
  }
}

static void line_map_add(void) {
  if (lm_count > 0 && lm_pc[lm_count - 1] == code_len) {
    lm_line[lm_count - 1] = (uint16_t)lline;
  } else if (lm_count < CL_LINES_MAX) {
    lm_pc[lm_count] = (uint16_t)code_len;
    lm_line[lm_count] = (uint16_t)lline;
    lm_count++;
  }
}

static int line_for_pc(int pc) {
  int line = 0;
  for (int i = 0; i < lm_count && lm_pc[i] <= pc; ++i) {
    line = lm_line[i];
  }
  return line;
}

/* ---- symbols ---- */

static int sym_lookup(const char *name, int len) {
  for (int i = 0; i < nsyms; ++i) {
    if (syms[i].len == len && cl_memeq(syms[i].name, name, len)) {
      return i;
    }
  }
  if (nsyms >= CL_SYM_MAX) {
    cl_error("too many names");
    return 0;
  }
  if (len > 255) {
    cl_error("name too long");
    return 0;
  }
  sym_t *s = &syms[nsyms];
  cl_fill(s, 0, sizeof(*s));
  s->name = name;
  s->len = (uint8_t)len;
  s->tk = T_ID;
  return nsyms++;
}

/* Register every word of a list; returns how many were added. */
static int register_words(const char *list, int tk_value, int tk_step,
                          int cls) {
  int count = 0;
  while (*list) {
    int n = 0;
    while (list[n] && list[n] != ' ') {
      n++;
    }
    int s = sym_lookup(list, n);
    syms[s].tk = (uint8_t)(tk_value + tk_step * count);
    syms[s].cls = (uint8_t)cls;
    syms[s].type = TY_INT;
    syms[s].val = count;
    list += n;
    while (*list == ' ') {
      list++;
    }
    count++;
  }
  return count;
}

/* ---- VM memory used at compile time for globals and strings ---- */

static int data_alloc(int size) {
  int addr = data_top;
  data_top = (data_top + size + 3) & ~3;
  if (data_top > CL_MEM_SIZE - CL_STACK_MIN) {
    cl_error("out of memory for globals");
    return CL_MEM_NULL_GUARD;
  }
  return addr;
}

static void data_end_string(void) {
  if (str_open) {
    str_open = 0;
    data_top = (str_pos + 1 + 3) & ~3; /* keep the NUL, align */
  }
}

static void mem_put32(int addr, int32_t v) {
  vm_mem[addr] = (uint8_t)v;
  vm_mem[addr + 1] = (uint8_t)(v >> 8);
  vm_mem[addr + 2] = (uint8_t)(v >> 16);
  vm_mem[addr + 3] = (uint8_t)(v >> 24);
}

/* ======================================================================== */
/* Lexer                                                                    */
/* ======================================================================== */

static int peek(int c) {
  return lp < src_len && src[lp] == c;
}

/* One character of a string or character literal, with escapes. */
static int lex_char(void) {
  int c = lp < src_len ? (unsigned char)src[lp++] : 0;
  if (c != '\\' || lp >= src_len) {
    return c;
  }
  c = (unsigned char)src[lp++];
  switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'b': return '\b';
    case 'a': return 7;
    case 'e': return 27;
    case '0': return 0;
    case 'x': {
      int v = 0;
      while (lp < src_len) {
        int h = src[lp];
        if (is_digit(h)) {
          v = v * 16 + h - '0';
        } else if (h >= 'a' && h <= 'f') {
          v = v * 16 + h - 'a' + 10;
        } else if (h >= 'A' && h <= 'F') {
          v = v * 16 + h - 'A' + 10;
        } else {
          break;
        }
        lp++;
      }
      return v & 0xff;
    }
    default: return c; /* \\ \' \" and anything else */
  }
}

static void lex_number(int c) {
  uint32_t v = (uint32_t)(c - '0');
  if (c == '0' && (peek('x') || peek('X'))) {
    lp++;
    v = 0;
    while (lp < src_len) {
      int h = src[lp];
      if (is_digit(h)) {
        v = v * 16u + (uint32_t)(h - '0');
      } else if (h >= 'a' && h <= 'f') {
        v = v * 16u + (uint32_t)(h - 'a' + 10);
      } else if (h >= 'A' && h <= 'F') {
        v = v * 16u + (uint32_t)(h - 'A' + 10);
      } else {
        break;
      }
      lp++;
    }
  } else if (c == '0') {
    while (lp < src_len && src[lp] >= '0' && src[lp] <= '7') {
      v = v * 8u + (uint32_t)(src[lp++] - '0');
    }
  } else {
    while (lp < src_len && is_digit(src[lp])) {
      v = v * 10u + (uint32_t)(src[lp++] - '0');
    }
  }
  if (lp < src_len && is_alpha(src[lp])) {
    cl_error("bad number");
  }
  ival = (int32_t)v;
}

static void skip_spaces_in_line(void) {
  while (lp < src_len && (src[lp] == ' ' || src[lp] == '\t')) {
    lp++;
  }
}

/* '#' lines: "#define NAME value" defines a constant, other directives such
 * as #include are ignored so that ordinary C files still compile. */
static void lex_directive(void) {
  skip_spaces_in_line();
  int start = lp;
  while (lp < src_len && is_alpha(src[lp])) {
    lp++;
  }
  if (lp - start == 6 && cl_memeq(src + start, "define", 6)) {
    skip_spaces_in_line();
    int name = lp;
    while (lp < src_len && (is_alpha(src[lp]) || is_digit(src[lp]))) {
      lp++;
    }
    if (lp == name) {
      cl_error("bad #define");
      return;
    }
    int s = sym_lookup(src + name, lp - name);
    skip_spaces_in_line();
    int negative = 0;
    if (peek('-')) {
      negative = 1;
      lp++;
    }
    if (lp < src_len && is_digit(src[lp])) {
      int c = src[lp++];
      lex_number(c);
    } else if (peek('\'')) {
      lp++;
      ival = lex_char();
      if (!peek('\'')) {
        cl_error("bad character constant");
        return;
      }
      lp++;
    } else {
      cl_error("#define supports numbers only");
      return;
    }
    syms[s].cls = T_NUM;
    syms[s].type = TY_INT;
    syms[s].val = negative ? -ival : ival;
  }
  while (lp < src_len && src[lp] != '\n') {
    lp++;
  }
}

static void next(void) {
  while (!cerr && lp < src_len) {
    int c = (unsigned char)src[lp++];
    if (c == '\n') {
      lline++;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      continue;
    }
    if (c == '#') {
      lex_directive();
      continue;
    }
    if (is_alpha(c)) {
      int start = lp - 1;
      while (lp < src_len && (is_alpha(src[lp]) || is_digit(src[lp]))) {
        lp++;
      }
      id = sym_lookup(src + start, lp - start);
      tk = syms[id].tk;
      if (tk == T_SKIP) {
        continue; /* const, static, unsigned, ... */
      }
      return;
    }
    if (is_digit(c)) {
      lex_number(c);
      tk = T_NUM;
      return;
    }
    if (c == '/') {
      if (peek('/')) {
        while (lp < src_len && src[lp] != '\n') {
          lp++;
        }
        continue;
      }
      if (peek('*')) {
        lp++;
        while (lp < src_len && !(src[lp] == '*' && lp + 1 < src_len &&
                                 src[lp + 1] == '/')) {
          if (src[lp] == '\n') {
            lline++;
          }
          lp++;
        }
        if (lp >= src_len) {
          cl_error("unterminated comment");
          return;
        }
        lp += 2;
        continue;
      }
      if (peek('=')) {
        lp++;
        tk = T_ASSIGN;
        tk_op = OP_DIV;
      } else {
        tk = T_DIV;
      }
      return;
    }
    if (c == '\'') {
      ival = lex_char();
      if (!peek('\'')) {
        cl_error("bad character constant");
        return;
      }
      lp++;
      tk = T_NUM;
      return;
    }
    if (c == '"') {
      /* Adjacent literals ("ab" "cd") are stored as one string. */
      if (!str_open) {
        str_open = 1;
        str_pos = data_top;
      }
      ival = str_pos;
      while (lp < src_len && src[lp] != '"' && src[lp] != '\n') {
        int ch = lex_char();
        if (str_pos >= CL_MEM_SIZE - CL_STACK_MIN - 1) {
          cl_error("out of memory for strings");
          return;
        }
        vm_mem[str_pos++] = (uint8_t)ch;
      }
      if (!peek('"')) {
        cl_error("unterminated string");
        return;
      }
      lp++;
      tk = '"';
      return;
    }
    if (c == '=') {
      if (peek('=')) {
        lp++;
        tk = T_EQ;
      } else {
        tk = T_ASSIGN;
        tk_op = 0;
      }
      return;
    }
    if (c == '+' || c == '-') {
      int op = c == '+' ? OP_ADD : OP_SUB;
      if (peek(c)) {
        lp++;
        tk = c == '+' ? T_INC : T_DEC;
      } else if (peek('=')) {
        lp++;
        tk = T_ASSIGN;
        tk_op = op;
      } else if (c == '-' && peek('>')) {
        cl_error("structures are not supported");
      } else {
        tk = c == '+' ? T_ADD : T_SUB;
      }
      return;
    }
    if (c == '!') {
      if (peek('=')) {
        lp++;
        tk = T_NE;
      } else {
        tk = '!';
      }
      return;
    }
    if (c == '<' || c == '>') {
      int less = c == '<';
      if (peek('=')) {
        lp++;
        tk = less ? T_LE : T_GE;
      } else if (peek(c)) {
        lp++;
        if (peek('=')) {
          lp++;
          tk = T_ASSIGN;
          tk_op = less ? OP_SHL : OP_SHR;
        } else {
          tk = less ? T_SHL : T_SHR;
        }
      } else {
        tk = less ? T_LT : T_GT;
      }
      return;
    }
    if (c == '|' || c == '&') {
      int is_or = c == '|';
      if (peek(c)) {
        lp++;
        tk = is_or ? T_LOR : T_LAN;
      } else if (peek('=')) {
        lp++;
        tk = T_ASSIGN;
        tk_op = is_or ? OP_OR : OP_AND;
      } else {
        tk = is_or ? T_OR : T_AND;
      }
      return;
    }
    if (c == '^' || c == '%' || c == '*') {
      int op = c == '^' ? OP_XOR : (c == '%' ? OP_MOD : OP_MUL);
      if (peek('=')) {
        lp++;
        tk = T_ASSIGN;
        tk_op = op;
      } else {
        tk = c == '^' ? T_XOR : (c == '%' ? T_MOD : T_MUL);
      }
      return;
    }
    if (c == '[') {
      tk = T_BRAK;
      return;
    }
    if (c == '?') {
      tk = T_COND;
      return;
    }
    if (c == '~' || c == ';' || c == '{' || c == '}' || c == '(' ||
        c == ')' || c == ']' || c == ',' || c == ':') {
      tk = c;
      return;
    }
    cl_error("unexpected character");
    return;
  }
  tk = 0; /* end of input */
}

static void expect(int token) {
  if (tk == token) {
    next();
    return;
  }
  char text[24] = "expected ' '";
  text[10] = (char)(token == T_BRAK ? '[' : token);
  cl_error(text);
}

/* A constant: number, character, enum/#define name, optionally negative. */
static int32_t const_int(void) {
  int negative = 0;
  if (tk == T_SUB) {
    negative = 1;
    next();
  }
  int32_t v = 0;
  if (tk == T_NUM) {
    v = ival;
  } else if (tk == T_ID && syms[id].cls == T_NUM) {
    v = syms[id].val;
  } else {
    cl_error("constant expected");
    return 0;
  }
  next();
  return negative ? -v : v;
}

/* ======================================================================== */
/* Expressions (precedence climbing)                                        */
/* ======================================================================== */

/* Operand precedence level of the right side of a binary operator. */
static int right_level(int op_tk) {
  if (op_tk == T_OR) return T_XOR;
  if (op_tk == T_XOR) return T_AND;
  if (op_tk == T_AND) return T_EQ;
  if (op_tk == T_EQ || op_tk == T_NE) return T_LT;
  if (op_tk <= T_GE) return T_SHL;
  if (op_tk <= T_SHR) return T_ADD;
  if (op_tk <= T_SUB) return T_MUL;
  return T_INC;
}

/* Turn the value in the accumulator into 0 or 1. */
static void emit_bool(void) {
  emit_op(OP_PSH);
  emit_op1(OP_IMM, 0);
  emit_op(OP_NE);
}

static void emit_scale(int type) {
  if (type > TY_PTR) { /* int pointers step by 4 bytes */
    emit_op(OP_PSH);
    emit_op1(OP_IMM, 4);
    emit_op(OP_MUL);
  }
}

/* The load just emitted becomes "push address, load again": used by ++, --
 * and compound assignment, which need both the address and the value. */
static int reuse_lvalue(void) {
  if (!last_is_load()) {
    cl_error("not assignable");
    return 0;
  }
  int load = code[last_op];
  code[last_op] = OP_PSH;
  emit_op(load);
  return 1;
}

static void expr(int lev);

static void expr_call(int s) {
  int argc = 0;
  next(); /* '(' */
  while (tk != ')' && tk) {
    expr(T_ASSIGN);
    emit_op(OP_PSH);
    argc++;
    if (tk != ',') {
      break;
    }
    next();
  }
  expect(')');
  if (syms[s].cls == T_SYS) {
    int want = k_builtin_argc[syms[s].val];
    if ((want == 255 && argc < 1) || (want != 255 && argc != want)) {
      cl_error_name("wrong number of arguments", s);
      return;
    }
    emit_op(OP_SYS);
    emit(syms[s].val);
    emit(argc);
  } else if (syms[s].cls == T_FUN) {
    emit_op1(OP_JSR, syms[s].val);
  } else if (syms[s].cls == 0 || syms[s].cls == T_FWD) {
    /* Not defined yet: remember the call in a chain patched later. */
    if (syms[s].cls == 0) {
      syms[s].cls = T_FWD;
      syms[s].type = TY_INT;
      syms[s].val = -1;
    }
    emit_op1(OP_JSR, syms[s].val);
    syms[s].val = code_len - 1;
  } else {
    cl_error_name("not a function", s);
    return;
  }
  if (argc) {
    emit_op1(OP_ADJ, argc);
  }
  ty = syms[s].type;
}

/* Primary expressions and prefix operators. */
static void expr_unary(void) {
  int t;
  if (tk == T_NUM) {
    emit_op1(OP_IMM, ival);
    next();
    ty = TY_INT;
  } else if (tk == '"') {
    emit_op1(OP_IMM, ival);
    next();
    while (tk == '"') {
      next();
    }
    data_end_string();
    ty = TY_CHAR + TY_PTR;
  } else if (tk == T_SIZEOF) {
    next();
    expect('(');
    t = TY_INT;
    if (tk == T_INT || tk == T_VOID) {
      next();
    } else if (tk == T_CHAR) {
      next();
      t = TY_CHAR;
    } else {
      cl_error("sizeof needs a type");
      return;
    }
    while (tk == T_MUL) {
      next();
      t += TY_PTR;
    }
    expect(')');
    emit_op1(OP_IMM, t == TY_CHAR ? 1 : 4);
    ty = TY_INT;
  } else if (tk == T_ID) {
    int s = id;
    next();
    if (tk == '(') {
      expr_call(s);
    } else if (syms[s].cls == T_NUM) {
      emit_op1(OP_IMM, syms[s].val);
      ty = TY_INT;
    } else {
      if (syms[s].cls == T_LOC) {
        emit_op1(OP_LEA, syms[s].val);
      } else if (syms[s].cls == T_GLO) {
        emit_op1(OP_IMM, syms[s].val);
      } else {
        cl_error_name("undefined", s);
        return;
      }
      if (syms[s].array) {
        ty = syms[s].type + TY_PTR; /* an array name is its address */
      } else {
        ty = syms[s].type;
        emit_load();
      }
    }
  } else if (tk == '(') {
    next();
    if (tk == T_INT || tk == T_CHAR || tk == T_VOID) { /* cast */
      t = tk == T_INT ? TY_INT : TY_CHAR;
      next();
      while (tk == T_MUL) {
        next();
        t += TY_PTR;
      }
      expect(')');
      expr(T_INC);
      if (t == TY_CHAR) { /* (char) keeps the low 8 bits */
        emit_op(OP_PSH);
        emit_op1(OP_IMM, 0xff);
        emit_op(OP_AND);
      }
      ty = t;
    } else {
      expr(T_ASSIGN);
      expect(')');
    }
  } else if (tk == T_MUL) {
    next();
    expr(T_INC);
    if (ty < TY_PTR) {
      cl_error("cannot dereference a non-pointer");
      return;
    }
    ty -= TY_PTR;
    emit_load();
  } else if (tk == T_AND) {
    next();
    expr(T_INC);
    if (!last_is_load()) {
      cl_error("cannot take this address");
      return;
    }
    code_len--; /* drop the load: the address stays in the accumulator */
    last_op = -1;
    ty += TY_PTR;
  } else if (tk == '!' || tk == '~') {
    int is_not = tk == '!';
    next();
    expr(T_INC);
    emit_op(OP_PSH);
    emit_op1(OP_IMM, is_not ? 0 : -1);
    emit_op(is_not ? OP_EQ : OP_XOR);
    ty = TY_INT;
  } else if (tk == T_ADD) {
    next();
    expr(T_INC);
    ty = TY_INT;
  } else if (tk == T_SUB) {
    next();
    if (tk == T_NUM) {
      emit_op1(OP_IMM, (int32_t)(0u - (uint32_t)ival));
      next();
    } else {
      emit_op1(OP_IMM, -1);
      emit_op(OP_PSH);
      expr(T_INC);
      emit_op(OP_MUL);
    }
    ty = TY_INT;
  } else if (tk == T_INC || tk == T_DEC) {
    t = tk;
    next();
    expr(T_INC);
    if (!reuse_lvalue()) {
      return;
    }
    emit_op(OP_PSH);
    emit_op1(OP_IMM, ty > TY_PTR ? 4 : 1);
    emit_op(t == T_INC ? OP_ADD : OP_SUB);
    emit_store(ty);
  } else {
    cl_error(tk ? "bad expression" : "unexpected end of program");
  }
}

static void expr(int lev) {
  if (++depth > CL_DEPTH_MAX) {
    cl_error("expression too complex");
    depth--;
    return;
  }
  expr_unary();

  while (tk >= lev && !cerr) {
    int t = ty;
    int hole;
    if (tk == T_ASSIGN) {
      int op = tk_op;
      next();
      if (!last_is_load()) {
        cl_error("not assignable");
        break;
      }
      if (op) { /* a op= b  ->  a = a op b */
        reuse_lvalue();
        emit_op(OP_PSH);
        expr(T_ASSIGN);
        if (op == OP_ADD || op == OP_SUB) {
          emit_scale(t);
        }
        emit_op(op);
      } else {
        code[last_op] = OP_PSH; /* push the address instead of loading */
        expr(T_ASSIGN);
      }
      emit_store(t);
      ty = t;
    } else if (tk == T_COND) {
      next();
      emit_op(OP_BZ);
      hole = code_len;
      emit(0);
      expr(T_ASSIGN);
      expect(':');
      code[hole] = code_len + 2;
      emit_op(OP_JMP);
      hole = code_len;
      emit(0);
      expr(T_COND);
      code[hole] = code_len;
    } else if (tk == T_LOR || tk == T_LAN) {
      int is_or = tk == T_LOR;
      next();
      emit_op(is_or ? OP_BNZ : OP_BZ); /* short-circuit */
      hole = code_len;
      emit(0);
      expr(is_or ? T_LAN : T_OR);
      code[hole] = code_len;
      emit_bool();
      ty = TY_INT;
    } else if (tk >= T_OR && tk <= T_MOD) {
      int op = tk - T_OR + OP_OR;
      next();
      emit_op(OP_PSH);
      expr(right_level(op - OP_OR + T_OR));
      if (op == OP_SUB && t >= TY_PTR && t == ty) {
        emit_op(OP_SUB); /* pointer difference counts elements */
        if (t > TY_PTR) {
          emit_op(OP_PSH);
          emit_op1(OP_IMM, 4);
          emit_op(OP_DIV);
        }
        ty = TY_INT;
      } else if (op == OP_ADD || op == OP_SUB) {
        emit_scale(t);
        emit_op(op);
        ty = t >= TY_PTR ? t : TY_INT;
      } else {
        emit_op(op);
        ty = TY_INT;
      }
    } else if (tk == T_INC || tk == T_DEC) { /* postfix */
      int step = ty > TY_PTR ? 4 : 1;
      int is_inc = tk == T_INC;
      if (!reuse_lvalue()) {
        break;
      }
      emit_op(OP_PSH);
      emit_op1(OP_IMM, step);
      emit_op(is_inc ? OP_ADD : OP_SUB);
      emit_store(ty);
      emit_op(OP_PSH); /* give back the old value */
      emit_op1(OP_IMM, step);
      emit_op(is_inc ? OP_SUB : OP_ADD);
      next();
    } else if (tk == T_BRAK) { /* a[i] is *(a + i) */
      next();
      emit_op(OP_PSH);
      expr(T_ASSIGN);
      expect(']');
      if (t < TY_PTR) {
        cl_error("subscript of a non-array");
        break;
      }
      emit_scale(t);
      emit_op(OP_ADD);
      ty = t - TY_PTR;
      emit_load();
    } else {
      cl_error("bad expression");
      break;
    }
  }
  depth--;
}

/* ======================================================================== */
/* Statements and declarations                                              */
/* ======================================================================== */

static void loop_push(int continue_target) {
  if (loop_depth >= CL_LOOP_MAX) {
    cl_error("loops nested too deeply");
    return;
  }
  brk_chain[loop_depth] = -1;
  cont_chain[loop_depth] = -1;
  cont_target[loop_depth] = continue_target;
  loop_depth++;
}

static void loop_pop(int break_target, int continue_target) {
  if (loop_depth <= 0 || cerr) {
    return;
  }
  loop_depth--;
  patch_chain(brk_chain[loop_depth], break_target);
  patch_chain(cont_chain[loop_depth], continue_target);
}

static int base_type(void) {
  int t = tk == T_CHAR ? TY_CHAR : TY_INT;
  if (tk == T_VOID) {
    t = TY_CHAR; /* only "void *" makes sense for data */
  }
  next();
  return t;
}

/* Hide a global name behind a local one until the function ends. */
static void make_local(int s, int type, int array, int32_t val) {
  syms[s].hcls = syms[s].cls;
  syms[s].htype = syms[s].type;
  syms[s].harray = syms[s].array;
  syms[s].hval = syms[s].val;
  syms[s].cls = T_LOC;
  syms[s].type = (uint8_t)type;
  syms[s].array = (uint8_t)array;
  syms[s].val = val;
}

static void local_decl(int in_for) {
  int base = base_type();
  while (tk && !cerr) {
    int t = base;
    int size = 4;
    int array = 0;
    while (tk == T_MUL) {
      next();
      t += TY_PTR;
    }
    if (tk != T_ID) {
      cl_error("variable name expected");
      return;
    }
    int s = id;
    next();
    if (tk == T_BRAK) {
      next();
      int32_t n = const_int();
      expect(']');
      if (n <= 0 || n > CL_MEM_SIZE) {
        cl_error("bad array size");
        return;
      }
      size = ((t == TY_CHAR ? n : n * 4) + 3) & ~3;
      array = 1;
    }
    if (syms[s].cls == T_LOC) {
      /* The same simple variable declared again (e.g. two "for (int i..."
       * loops) reuses its slot; anything else is a real duplicate. */
      if (array || syms[s].array || syms[s].type != t) {
        cl_error_name("already declared", s);
        return;
      }
    } else {
      local_size += size;
      if (local_size > CL_MEM_SIZE / 2) {
        cl_error("local variables too large");
        return;
      }
      make_local(s, t, array, -local_size);
    }
    if (tk == T_ASSIGN && !tk_op) {
      if (array) {
        cl_error("local array initializers are not supported");
        return;
      }
      next();
      emit_op1(OP_LEA, syms[s].val);
      emit_op(OP_PSH);
      expr(T_ASSIGN);
      emit_store(t);
    }
    if (tk != ',') {
      break;
    }
    next();
  }
  if (!in_for) {
    expect(';');
  }
}

static void stmt(void) {
  int a, b, c;
  if (cerr) {
    return;
  }
  if (++depth > CL_DEPTH_MAX) {
    cl_error("statements nested too deeply");
    depth--;
    return;
  }
  line_map_add();
  if (tk == T_IF) {
    next();
    expect('(');
    expr(T_ASSIGN);
    expect(')');
    emit_op(OP_BZ);
    b = code_len;
    emit(0);
    stmt();
    if (tk == T_ELSE) {
      code[b] = code_len + 2;
      emit_op(OP_JMP);
      b = code_len;
      emit(0);
      next();
      stmt();
    }
    code[b] = code_len;
  } else if (tk == T_WHILE) {
    next();
    a = code_len;
    expect('(');
    expr(T_ASSIGN);
    expect(')');
    emit_op(OP_BZ);
    b = code_len;
    emit(0);
    loop_push(a);
    stmt();
    emit_op1(OP_JMP, a);
    code[b] = code_len;
    loop_pop(code_len, a);
  } else if (tk == T_DO) {
    next();
    a = code_len;
    loop_push(-1); /* continue target is known only after the body */
    stmt();
    if (tk != T_WHILE) {
      cl_error("expected while after do");
    }
    next();
    c = code_len;
    expect('(');
    expr(T_ASSIGN);
    expect(')');
    expect(';');
    emit_op1(OP_BNZ, a);
    loop_pop(code_len, c);
  } else if (tk == T_FOR) {
    /* for (init; cond; step) body  is laid out as:
     *   init; C: cond; BZ exit; JMP B; S: step; JMP C; B: body; JMP S; exit: */
    next();
    expect('(');
    if (tk == T_INT || tk == T_CHAR) {
      local_decl(1);
    } else if (tk != ';') {
      expr(T_ASSIGN);
    }
    expect(';');
    a = code_len;
    if (tk != ';') {
      expr(T_ASSIGN);
    } else {
      emit_op1(OP_IMM, 1);
    }
    expect(';');
    emit_op(OP_BZ);
    b = code_len;
    emit(0);
    emit_op(OP_JMP);
    c = code_len;
    emit(0);
    int step = code_len;
    if (tk != ')') {
      expr(T_ASSIGN);
    }
    expect(')');
    emit_op1(OP_JMP, a);
    code[c] = code_len;
    loop_push(step);
    stmt();
    emit_op1(OP_JMP, step);
    code[b] = code_len;
    loop_pop(code_len, step);
  } else if (tk == T_BREAK || tk == T_CONTINUE) {
    int is_break = tk == T_BREAK;
    next();
    expect(';');
    if (loop_depth == 0) {
      cl_error(is_break ? "break outside a loop" : "continue outside a loop");
    } else {
      int top = loop_depth - 1;
      emit_op(OP_JMP);
      if (is_break) {
        emit(brk_chain[top]);
        brk_chain[top] = code_len - 1;
      } else if (cont_target[top] >= 0) {
        emit(cont_target[top]);
      } else {
        emit(cont_chain[top]);
        cont_chain[top] = code_len - 1;
      }
    }
  } else if (tk == T_RETURN) {
    next();
    if (tk != ';') {
      expr(T_ASSIGN);
    } else {
      emit_op1(OP_IMM, 0);
    }
    expect(';');
    emit_op(OP_LEV);
  } else if (tk == '{') {
    next();
    while (tk != '}' && tk) {
      stmt();
    }
    expect('}');
  } else if (tk == ';') {
    next();
  } else if (tk == T_INT || tk == T_CHAR || tk == T_VOID) {
    local_decl(0);
  } else {
    expr(T_ASSIGN);
    expect(';');
  }
  depth--;
}

static void function_def(int s) {
  int nparams = 0;
  next(); /* '(' */
  while (tk != ')' && tk && !cerr) {
    int t = TY_INT;
    if (tk == T_VOID) {
      next();
      if (tk == ')') {
        break; /* f(void) */
      }
      t = TY_CHAR;
    } else if (tk == T_INT || tk == T_CHAR) {
      t = base_type();
    }
    while (tk == T_MUL) {
      next();
      t += TY_PTR;
    }
    if (tk != T_ID) {
      cl_error("parameter name expected");
      return;
    }
    if (syms[id].cls == T_LOC) {
      cl_error_name("duplicate parameter", id);
      return;
    }
    int param = id;
    next();
    if (tk == T_BRAK) { /* int a[] is a pointer parameter */
      next();
      expect(']');
      t += TY_PTR;
    }
    make_local(param, t, 0, nparams++);
    if (tk == ',') {
      next();
    }
  }
  expect(')');

  /* Parameters sit above the saved frame pointer and return address:
   * parameter i of n is at bp + 8 + 4 * (n - 1 - i). */
  for (int i = 0; i < nsyms; ++i) {
    if (syms[i].cls == T_LOC) {
      syms[i].val = 8 + 4 * (nparams - 1 - syms[i].val);
    }
  }

  if (tk == ';') { /* prototype */
    next();
    if (syms[s].cls != T_FUN && syms[s].cls != T_FWD) {
      syms[s].cls = T_FWD;
      syms[s].val = -1;
    }
  } else if (syms[s].cls == T_FUN) {
    cl_error_name("already defined", s);
  } else {
    patch_chain(syms[s].cls == T_FWD ? syms[s].val : -1, code_len);
    syms[s].cls = T_FUN;
    syms[s].val = code_len;
    line_map_add(); /* errors in the prologue report the function line */
    if (tk != '{') {
      cl_error("expected '{'");
    }
    next();
    emit_op(OP_ENT);
    int ent = code_len;
    emit(0);
    local_size = 0;
    while (tk != '}' && tk && !cerr) {
      stmt();
    }
    code[ent] = local_size;
    emit_op1(OP_IMM, 0); /* falling off the end returns 0 */
    emit_op(OP_LEV);
    expect('}');
  }

  for (int i = 0; i < nsyms; ++i) { /* locals go out of scope */
    if (syms[i].cls == T_LOC) {
      syms[i].cls = syms[i].hcls;
      syms[i].type = syms[i].htype;
      syms[i].array = syms[i].harray;
      syms[i].val = syms[i].hval;
    }
  }
}

static void enum_decl(void) {
  next();
  if (tk == T_ID) {
    next(); /* optional tag */
  }
  expect('{');
  int32_t value = 0;
  while (tk != '}' && tk && !cerr) {
    if (tk != T_ID) {
      cl_error("enum name expected");
      return;
    }
    int s = id;
    next();
    if (tk == T_ASSIGN && !tk_op) {
      next();
      value = const_int();
    }
    syms[s].cls = T_NUM;
    syms[s].type = TY_INT;
    syms[s].val = value++;
    if (tk == ',') {
      next();
    }
  }
  expect('}');
  expect(';');
}

/* Global array: size, optional "string" or {list} initializer. */
static void global_array(int s, int t) {
  next(); /* '[' */
  int32_t n = -1; /* -1: size taken from the initializer */
  if (tk != ']') {
    n = const_int();
    if (n <= 0 || n > CL_MEM_SIZE) {
      cl_error("bad array size");
      return;
    }
  }
  expect(']');
  int esize = t == TY_CHAR ? 1 : 4;
  int addr = 0;
  if (n > 0) {
    addr = data_alloc(n * esize);
  }
  if (tk == T_ASSIGN && !tk_op) {
    next();
    if (tk == '"' && t == TY_CHAR) {
      int start = ival;
      next();
      while (tk == '"') {
        next();
      }
      int len = str_pos - start;
      data_end_string();
      if (n < 0) {
        addr = start; /* the literal itself becomes the array */
      } else if (len >= n) {
        cl_error("string longer than the array");
      } else {
        cl_move(&vm_mem[addr], &vm_mem[start], len);
      }
    } else {
      expect('{');
      int count = 0;
      if (n < 0) {
        addr = data_top;
      }
      while (tk != '}' && tk && !cerr) {
        int32_t v = const_int();
        int at = addr + count * esize;
        if ((n > 0 && count >= n) || at + esize > CL_MEM_SIZE - CL_STACK_MIN) {
          cl_error("too many initializers");
          return;
        }
        if (esize == 1) {
          vm_mem[at] = (uint8_t)v;
        } else {
          mem_put32(at, v);
        }
        count++;
        if (tk != ',') {
          break;
        }
        next();
      }
      expect('}');
      if (n < 0) {
        data_top = (addr + count * esize + 3) & ~3;
      }
    }
  } else if (n < 0) {
    cl_error("array size missing");
  }
  syms[s].cls = T_GLO;
  syms[s].type = (uint8_t)t;
  syms[s].array = 1;
  syms[s].val = addr;
}

static void global_decl(void) {
  if (tk == T_ENUM) {
    enum_decl();
    return;
  }
  if (tk == ';') {
    next();
    return;
  }
  if (tk != T_INT && tk != T_CHAR && tk != T_VOID) {
    cl_error("declaration expected (int, char, void)");
    return;
  }
  int is_void = tk == T_VOID;
  int base = base_type();
  while (tk && !cerr) {
    int t = base;
    while (tk == T_MUL) {
      next();
      t += TY_PTR;
    }
    if (tk != T_ID) {
      cl_error("name expected");
      return;
    }
    int s = id;
    int cls = syms[s].cls;
    next();
    if (tk == '(') {
      if (cls == T_GLO || cls == T_NUM) {
        cl_error_name("already defined", s);
        return;
      }
      syms[s].type = (uint8_t)(is_void && t == TY_CHAR ? TY_INT : t);
      function_def(s);
      return; /* one function per declaration */
    }
    if (cls == T_FUN || cls == T_FWD || cls == T_GLO || cls == T_NUM) {
      cl_error_name("already defined", s);
      return;
    }
    syms[s].array = 0;
    if (tk == T_BRAK) {
      global_array(s, t);
    } else {
      int addr = data_alloc(4);
      syms[s].cls = T_GLO;
      syms[s].type = (uint8_t)t;
      syms[s].val = addr;
      if (tk == T_ASSIGN && !tk_op) {
        next();
        if (tk == '"') {
          int start = ival;
          next();
          while (tk == '"') {
            next();
          }
          data_end_string();
          mem_put32(addr, start);
        } else {
          int32_t v = const_int();
          if (t == TY_CHAR) {
            vm_mem[addr] = (uint8_t)v;
          } else {
            mem_put32(addr, v);
          }
        }
      }
    }
    if (tk != ',') {
      break;
    }
    next();
  }
  expect(';');
}

/* Compile src[] into code[]. Returns 0 on success. */
static int cl_compile(void) {
  cerr = 0;
  err_line = 0;
  err_msg[0] = '\0';
  nsyms = 0;
  code_len = 0;
  last_op = -1;
  lm_count = 0;
  depth = 0;
  loop_depth = 0;
  str_open = 0;
  cl_fill(vm_mem, 0, CL_MEM_SIZE);
  data_top = CL_MEM_NULL_GUARD;
  lline = 1;

  register_words(k_keywords, T_CHAR, 1, 0);
  register_words(k_int_words, T_INT, 0, 0);
  register_words(k_skip_words, T_SKIP, 0, 0);
  register_words(k_builtins, T_ID, 0, T_SYS);
  int nconst = register_words(k_constants, T_ID, 0, T_NUM);
  for (int i = 0; i < nconst; ++i) {
    syms[nsyms - nconst + i].val = k_constant_values[i];
  }

  /* Start-up code: call main() and pass its result to exit(). */
  emit_op(OP_JSR);
  emit(0);
  emit_op(OP_PSH);
  emit_op(OP_SYS);
  emit(SYS_EXIT);
  emit(1);

  lp = 0;
  next();
  while (tk && !cerr) {
    global_decl();
  }
  if (cerr) {
    return -1;
  }

  int s = sym_lookup("main", 4);
  if (syms[s].cls != T_FUN) {
    cl_error("main() is missing");
    return -1;
  }
  code[1] = syms[s].val;
  for (int i = 0; i < nsyms; ++i) {
    if (syms[i].cls == T_FWD) {
      cl_error_name("function not defined", i);
      return -1;
    }
  }
  return 0;
}

/* ======================================================================== */
/* Virtual machine                                                          */
/* ======================================================================== */

enum { VM_IDLE, VM_RUN, VM_DONE, VM_ERROR };
enum { SYS_OK, SYS_BLOCK, SYS_YIELD };

static int vm_state;
static int32_t vm_a, vm_pc, vm_sp, vm_bp, vm_op_pc;
static int vm_argc;
static int32_t vm_exit_code;
static int32_t heap_top, heap_last;
static uint32_t rng_state;
static int canvas;          /* the program drew graphics */
static int rl_active;       /* readline() in progress */
static int rl_len;
static int sleep_active;
static uint32_t sleep_until;
static int waiting_input;   /* blocked in getchar/readline */

static void vm_error(const char *msg) {
  if (vm_state != VM_RUN) {
    return;
  }
  vm_state = VM_ERROR;
  err_line = line_for_pc(vm_op_pc);
  err_msg[0] = '\0';
  str_append(err_msg, sizeof(err_msg), msg);
}

static int mem_ok(int32_t addr, int32_t size) {
  return addr >= CL_MEM_NULL_GUARD && addr <= CL_MEM_SIZE - size;
}

static int32_t ld32(int32_t addr) {
  if (!mem_ok(addr, 4)) {
    vm_error(addr < CL_MEM_NULL_GUARD ? "NULL pointer access"
                                      : "invalid memory access");
    return 0;
  }
  return (int32_t)((uint32_t)vm_mem[addr] | ((uint32_t)vm_mem[addr + 1] << 8) |
                   ((uint32_t)vm_mem[addr + 2] << 16) |
                   ((uint32_t)vm_mem[addr + 3] << 24));
}

static int ld8(int32_t addr) {
  if (!mem_ok(addr, 1)) {
    vm_error(addr < CL_MEM_NULL_GUARD ? "NULL pointer access"
                                      : "invalid memory access");
    return 0;
  }
  return vm_mem[addr];
}

static void st32(int32_t addr, int32_t v) {
  if (!mem_ok(addr, 4)) {
    vm_error(addr < CL_MEM_NULL_GUARD ? "NULL pointer access"
                                      : "invalid memory access");
    return;
  }
  mem_put32(addr, v);
}

static void st8(int32_t addr, int v) {
  if (!mem_ok(addr, 1)) {
    vm_error(addr < CL_MEM_NULL_GUARD ? "NULL pointer access"
                                      : "invalid memory access");
    return;
  }
  vm_mem[addr] = (uint8_t)v;
}

static void push(int32_t v) {
  vm_sp -= 4;
  if (vm_sp < heap_top + 64) {
    vm_error("stack overflow (endless recursion?)");
    return;
  }
  mem_put32(vm_sp, v);
}

static int32_t pop(void) {
  int32_t v = ld32(vm_sp);
  vm_sp += 4;
  return v;
}

/* Argument i of the built-in being executed (arguments are pushed left to
 * right, so the first one is the deepest on the stack). */
static int32_t arg(int i) {
  return ld32(vm_sp + 4 * (vm_argc - 1 - i));
}

static int32_t vm_strlen(int32_t s) {
  int32_t n = 0;
  while (vm_state == VM_RUN && ld8(s + n)) {
    n++;
  }
  return n;
}

static void vm_puts(int32_t s) {
  int c;
  while (vm_state == VM_RUN && (c = ld8(s++)) != 0) {
    con_putc(c);
  }
}

/* Integer to text: returns the length written into out[12]. */
static int fmt_int(char *out, int32_t v, int base, int is_signed, int upper) {
  char tmp[12];
  int n = 0;
  int negative = is_signed && v < 0;
  uint32_t u = negative ? 0u - (uint32_t)v : (uint32_t)v;
  do {
    int digit = (int)(u % (uint32_t)base);
    tmp[n++] = (char)(digit < 10 ? '0' + digit
                                 : (upper ? 'A' : 'a') + digit - 10);
    u /= (uint32_t)base;
  } while (u);
  int len = 0;
  if (negative) {
    out[len++] = '-';
  }
  while (n) {
    out[len++] = tmp[--n];
  }
  return len;
}

/* printf with %d %i %u %x %X %c %s %%, width, '-' and '0' flags. */
static int32_t vm_printf(void) {
  int32_t fmt = arg(0);
  int next_arg = 1;
  int32_t written = 0;
  while (vm_state == VM_RUN) {
    int c = ld8(fmt++);
    if (!c) {
      break;
    }
    if (c != '%') {
      con_putc(c);
      written++;
      continue;
    }
    c = ld8(fmt++);
    int left = 0;
    int zero = 0;
    int width = 0;
    if (c == '-') {
      left = 1;
      c = ld8(fmt++);
    }
    if (c == '0') {
      zero = 1;
      c = ld8(fmt++);
    }
    while (c >= '0' && c <= '9') {
      width = width * 10 + c - '0';
      c = ld8(fmt++);
    }
    if (c == 'l') {
      c = ld8(fmt++); /* %ld is the same as %d here */
    }
    if (c == '%') {
      con_putc('%');
      written++;
      continue;
    }
    if (next_arg >= vm_argc) {
      vm_error("printf: missing argument");
      break;
    }
    int32_t v = arg(next_arg++);
    char buf[12];
    int len = 0;
    int32_t text = 0;
    if (c == 'd' || c == 'i') {
      len = fmt_int(buf, v, 10, 1, 0);
    } else if (c == 'u') {
      len = fmt_int(buf, v, 10, 0, 0);
    } else if (c == 'x' || c == 'X') {
      len = fmt_int(buf, v, 16, 0, c == 'X');
    } else if (c == 'c') {
      buf[0] = (char)v;
      len = 1;
    } else if (c == 's') {
      text = v;
      len = vm_strlen(v);
    } else {
      vm_error("printf: unknown % format");
      break;
    }
    int pad = width > len ? width - len : 0;
    int first = 0;
    if (!left && zero && !text && len > 0 && buf[0] == '-') {
      con_putc('-'); /* -0042, not 00-42 */
      first = 1;
    }
    while (!left && pad-- > 0) {
      con_putc(zero && !text ? '0' : ' ');
    }
    for (int i = first; i < len; ++i) {
      con_putc(text ? ld8(text + i) : (unsigned char)buf[i]);
    }
    while (left && pad-- > 0) {
      con_putc(' ');
    }
    written += len > width ? len : width;
  }
  return written;
}

/* readline(buf, max): line editing with echo; blocks until ENTER. */
static int vm_readline(void) {
  int32_t buf = arg(0);
  int32_t max = arg(1);
  if (max < 1 || !mem_ok(buf, max)) {
    vm_error("readline: bad buffer");
    return SYS_OK;
  }
  if (!rl_active) {
    rl_active = 1;
    rl_len = 0;
  }
  int key;
  while ((key = keyq_pop()) != 0) {
    if (key == PRG32_KEY_ENTER) {
      st8(buf + rl_len, 0);
      con_putc('\n');
      rl_active = 0;
      waiting_input = 0;
      vm_a = rl_len;
      return SYS_OK;
    }
    if (key == PRG32_KEY_BACKSPACE) {
      if (rl_len > 0) {
        rl_len--;
        con_putc('\b');
      }
    } else if (key >= 32 && key < 127 && rl_len < max - 1) {
      st8(buf + rl_len++, key);
      con_putc(key);
    }
  }
  waiting_input = 1;
  return SYS_BLOCK;
}

/* Key code (character or KEY_*) -> USB HID usage for keydown(). */
static uint32_t key_to_usage(int32_t k) {
  if (k >= 'a' && k <= 'z') return PRG32_HID_KEY_LETTER(k - 'a' + 'A');
  if (k >= 'A' && k <= 'Z') return PRG32_HID_KEY_LETTER(k);
  if (k >= '1' && k <= '9') return PRG32_HID_KEY_1 + (uint32_t)(k - '1');
  if (k == '0') return PRG32_HID_KEY_1 + 9u;
  if (k == ' ') return PRG32_HID_KEY_SPACE;
  if (k == PRG32_KEY_ENTER) return PRG32_HID_KEY_ENTER;
  if (k == PRG32_KEY_ESCAPE) return PRG32_HID_KEY_ESCAPE;
  if (k == PRG32_KEY_BACKSPACE) return PRG32_HID_KEY_BACKSPACE;
  if (k == PRG32_KEY_TAB) return PRG32_HID_KEY_TAB;
  if (k == PRG32_KEY_UP) return PRG32_HID_KEY_UP;
  if (k == PRG32_KEY_DOWN) return PRG32_HID_KEY_DOWN;
  if (k == PRG32_KEY_LEFT) return PRG32_HID_KEY_LEFT;
  if (k == PRG32_KEY_RIGHT) return PRG32_HID_KEY_RIGHT;
  return 0;
}

static void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0;
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int e = dx + dy;
  for (int steps = 0; steps < 2048; ++steps) { /* Bresenham */
    prg32_gfx_pixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    int e2 = 2 * e;
    if (e2 >= dy) {
      e += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      e += dx;
      y0 += sy;
    }
  }
}

static int vm_syscall(int sys) {
  int32_t a0 = vm_argc > 0 ? arg(0) : 0;
  int32_t a1 = vm_argc > 1 ? arg(1) : 0;
  vm_a = 0;
  switch (sys) {
    case SYS_PUTCHAR:
      con_putc(a0 & 0xff);
      vm_a = a0;
      break;
    case SYS_PUTS:
      vm_puts(a0);
      con_putc('\n');
      break;
    case SYS_PRINTF:
      vm_a = vm_printf();
      break;
    case SYS_GETCHAR: {
      int key = keyq_pop();
      if (!key) {
        waiting_input = 1;
        return SYS_BLOCK;
      }
      waiting_input = 0;
      if ((key >= 32 && key < 127) || key == PRG32_KEY_ENTER) {
        con_putc(key); /* echo like a terminal */
      }
      vm_a = key;
      break;
    }
    case SYS_GETKEY:
      vm_a = keyq_pop();
      break;
    case SYS_READLINE:
      return vm_readline();
    case SYS_STRLEN:
      vm_a = vm_strlen(a0);
      break;
    case SYS_STRCMP: {
      int32_t i = 0;
      int x, y;
      do {
        x = ld8(a0 + i);
        y = ld8(a1 + i);
        i++;
      } while (vm_state == VM_RUN && x && x == y);
      vm_a = x - y;
      break;
    }
    case SYS_STRCPY:
    case SYS_STRCAT: {
      int32_t d = a0;
      if (sys == SYS_STRCAT) {
        d += vm_strlen(a0);
      }
      int32_t i = 0;
      int c;
      do {
        c = ld8(a1 + i);
        st8(d + i, c);
        i++;
      } while (vm_state == VM_RUN && c);
      vm_a = a0;
      break;
    }
    case SYS_ATOI: {
      int32_t s = a0;
      int negative = 0;
      uint32_t v = 0;
      while (ld8(s) == ' ' && vm_state == VM_RUN) {
        s++;
      }
      if (ld8(s) == '-' || ld8(s) == '+') {
        negative = ld8(s++) == '-';
      }
      while (vm_state == VM_RUN && is_digit(ld8(s))) {
        v = v * 10u + (uint32_t)(ld8(s++) - '0');
      }
      vm_a = (int32_t)(negative ? 0u - v : v);
      break;
    }
    case SYS_ABS:
      vm_a = a0 < 0 ? (int32_t)(0u - (uint32_t)a0) : a0;
      break;
    case SYS_RAND:
      rng_state = rng_state * 1103515245u + 12345u;
      vm_a = (int32_t)((rng_state >> 16) & 0x7fffu);
      break;
    case SYS_SRAND:
      rng_state = (uint32_t)a0;
      break;
    case SYS_MALLOC: {
      /* A simple "bump" allocator between globals and the stack. */
      int32_t size = (a0 + 3) & ~3;
      if (a0 <= 0 || heap_top + size > vm_sp - CL_STACK_MIN) {
        vm_a = 0;
        break;
      }
      heap_last = heap_top;
      vm_a = heap_top;
      cl_fill(&vm_mem[heap_top], 0, size);
      heap_top += size;
      break;
    }
    case SYS_FREE:
      if (a0 != 0 && a0 == heap_last) {
        heap_top = heap_last; /* only the latest block is really freed */
        heap_last = -1;
      }
      break;
    case SYS_MEMSET: {
      int32_t n = arg(2);
      if (n < 0 || !mem_ok(a0, n)) {
        vm_error("memset: bad range");
        break;
      }
      cl_fill(&vm_mem[a0], a1, n);
      vm_a = a0;
      break;
    }
    case SYS_EXIT:
      vm_exit_code = a0;
      vm_state = VM_DONE;
      return SYS_YIELD;
    case SYS_CLS:
      canvas = 0;
      con_clear();
      break;
    case SYS_GOTOXY:
      con_x = a0 < 0 ? 0 : (a0 >= CON_COLS ? CON_COLS - 1 : a0);
      con_y = a1 < 0 ? 0 : (a1 >= CON_ROWS ? CON_ROWS - 1 : a1);
      break;
    case SYS_TEXTCOLOR:
      con_color = (uint8_t)(a0 & 15);
      break;
    case SYS_CLEAR:
      canvas = 1;
      prg32_gfx_clear((uint16_t)a0);
      break;
    case SYS_PIXEL:
      canvas = 1;
      prg32_gfx_pixel(a0, a1, (uint16_t)arg(2));
      break;
    case SYS_RECT:
      canvas = 1;
      prg32_gfx_rect(a0, a1, arg(2), arg(3), (uint16_t)arg(4));
      break;
    case SYS_LINE:
      canvas = 1;
      draw_line(a0, a1, arg(2), arg(3), (uint16_t)arg(4));
      break;
    case SYS_TEXT: {
      char buf[CON_COLS + 1];
      int32_t s = arg(2);
      int n = 0;
      int c;
      while (n < CON_COLS && vm_state == VM_RUN && (c = ld8(s + n)) != 0) {
        buf[n++] = (char)c;
      }
      buf[n] = '\0';
      canvas = 1;
      prg32_gfx_text8(a0, a1, buf, (uint16_t)arg(3), PRG32_COLOR_BLACK);
      break;
    }
    case SYS_RGB:
      vm_a = (int32_t)(((uint32_t)(a0 & 0xf8) << 8) |
                       ((uint32_t)(a1 & 0xfc) << 3) |
                       ((uint32_t)(arg(2) & 0xff) >> 3));
      break;
    case SYS_FRAME:
      return SYS_YIELD; /* continue in the next frame */
    case SYS_BTN:
      vm_a = (int32_t)(prg32_input_read() & 0x7fu);
      break;
    case SYS_KEYDOWN: {
      uint32_t usage = key_to_usage(a0);
      vm_a = usage ? prg32_btkbd_key_down(usage) != 0 : 0;
      break;
    }
    case SYS_MS:
      vm_a = (int32_t)prg32_ticks_ms();
      break;
    case SYS_SLEEP:
      if (!sleep_active) {
        sleep_active = 1;
        sleep_until = prg32_ticks_ms() + (uint32_t)(a0 > 0 ? a0 : 0);
      }
      if ((int32_t)(prg32_ticks_ms() - sleep_until) < 0) {
        return SYS_BLOCK;
      }
      sleep_active = 0;
      break;
    case SYS_NOTE:
      prg32_audio_note(0, PRG32_DEFAULT_INSTRUMENT_ID, (uint8_t)a0, 200,
                       (uint32_t)(a1 > 0 ? a1 : 0));
      break;
    default:
      vm_error("unknown built-in");
      break;
  }
  return SYS_OK;
}

/* Execute up to `budget` instructions. Returns 1 when the program is waiting
 * (for a key, a timer or the next frame) and the slice should end. */
static int vm_run_batch(int budget) {
  while (budget-- > 0 && vm_state == VM_RUN) {
    if (vm_pc < 0 || vm_pc >= code_len) {
      vm_error("jump outside the program");
      return 1;
    }
    vm_op_pc = vm_pc;
    int op = code[vm_pc++];
    int32_t b;
    switch (op) {
      case OP_LEA: vm_a = vm_bp + code[vm_pc++]; break;
      case OP_IMM: vm_a = code[vm_pc++]; break;
      case OP_JMP: vm_pc = code[vm_pc]; break;
      case OP_JSR:
        push(vm_pc + 1);
        vm_pc = code[vm_pc];
        break;
      case OP_BZ: vm_pc = vm_a ? vm_pc + 1 : code[vm_pc]; break;
      case OP_BNZ: vm_pc = vm_a ? code[vm_pc] : vm_pc + 1; break;
      case OP_ENT: {
        int32_t size = code[vm_pc++];
        push(vm_bp);
        vm_bp = vm_sp;
        vm_sp -= size;
        if (vm_sp < heap_top + 64) {
          vm_error("stack overflow (endless recursion?)");
          return 1;
        }
        cl_fill(&vm_mem[vm_sp], 0, size); /* locals start at zero */
        break;
      }
      case OP_ADJ: vm_sp += code[vm_pc++] * 4; break;
      case OP_LEV:
        vm_sp = vm_bp;
        vm_bp = pop();
        vm_pc = pop();
        break;
      case OP_LI: vm_a = ld32(vm_a); break;
      case OP_LC: vm_a = ld8(vm_a); break;
      case OP_SI: st32(pop(), vm_a); break;
      case OP_SC:
        vm_a &= 0xff;
        st8(pop(), vm_a);
        break;
      case OP_PSH: push(vm_a); break;
      case OP_SYS: {
        int sys = code[vm_pc++];
        vm_argc = code[vm_pc++];
        int r = vm_syscall(sys);
        if (r == SYS_BLOCK) {
          vm_pc = vm_op_pc; /* try the same call again next frame */
          return 1;
        }
        if (r == SYS_YIELD) {
          return 1;
        }
        break;
      }
      default:
        /* Binary operators: left operand on the stack, right in vm_a.
         * Unsigned arithmetic keeps overflow well defined (wraps). */
        b = pop();
        switch (op) {
          case OP_OR: vm_a = b | vm_a; break;
          case OP_XOR: vm_a = b ^ vm_a; break;
          case OP_AND: vm_a = b & vm_a; break;
          case OP_EQ: vm_a = b == vm_a; break;
          case OP_NE: vm_a = b != vm_a; break;
          case OP_LT: vm_a = b < vm_a; break;
          case OP_GT: vm_a = b > vm_a; break;
          case OP_LE: vm_a = b <= vm_a; break;
          case OP_GE: vm_a = b >= vm_a; break;
          case OP_SHL: vm_a = (int32_t)((uint32_t)b << (vm_a & 31)); break;
          case OP_SHR: vm_a = b >> (vm_a & 31); break;
          case OP_ADD: vm_a = (int32_t)((uint32_t)b + (uint32_t)vm_a); break;
          case OP_SUB: vm_a = (int32_t)((uint32_t)b - (uint32_t)vm_a); break;
          case OP_MUL: vm_a = (int32_t)((uint32_t)b * (uint32_t)vm_a); break;
          case OP_DIV:
          case OP_MOD:
            if (vm_a == 0) {
              vm_error("division by zero");
              return 1;
            }
            if (vm_a == -1) { /* avoid INT_MIN / -1 overflow */
              vm_a = op == OP_DIV ? (int32_t)(0u - (uint32_t)b) : 0;
            } else {
              vm_a = op == OP_DIV ? b / vm_a : b % vm_a;
            }
            break;
          default:
            vm_error("bad instruction");
            return 1;
        }
        break;
    }
  }
  return vm_state != VM_RUN;
}

static void vm_start(void) {
  vm_pc = 0;
  vm_sp = CL_MEM_SIZE;
  vm_bp = vm_sp;
  vm_a = 0;
  vm_exit_code = 0;
  heap_top = data_top;
  heap_last = -1;
  rl_active = 0;
  sleep_active = 0;
  waiting_input = 0;
  canvas = 0;
  rng_state = prg32_random_number(1, 0x7fffffffu);
  con_color = 15;
  con_clear();
  keyq_clear();
  vm_state = VM_RUN;
}

/* Run the program for up to CL_SLICE_MS milliseconds. */
static void vm_run_slice(void) {
  uint32_t start = prg32_ticks_ms();
  while (vm_state == VM_RUN) {
    if (vm_run_batch(1024)) {
      break;
    }
    if (prg32_ticks_ms() - start >= CL_SLICE_MS) {
      break;
    }
  }
}

/* ======================================================================== */
/* Editor                                                                   */
/* ======================================================================== */

static int ed_cursor;     /* byte index into src[] */
static int ed_goal_col;   /* column kept while moving up and down */
static int ed_top;        /* first visible line */
static int ed_left;       /* first visible column */
static int ed_line, ed_col;
static int ed_lines;
static int ed_modified;
static int ed_example;
static int ed_dirty = 1;
static int ed_confirm;    /* action waiting for a second key press */
static uint32_t ed_confirm_until;
static char ed_msg[CON_COLS + 1];
static int ed_msg_error;
static uint32_t ed_msg_until;
static int ed_blink;

enum { CONFIRM_NONE, CONFIRM_NEW, CONFIRM_EXAMPLE };

static void ed_message(const char *text, int is_error, uint32_t ms) {
  ed_msg[0] = '\0';
  str_append(ed_msg, sizeof(ed_msg), text);
  ed_msg_error = is_error;
  ed_msg_until = prg32_ticks_ms() + ms;
  ed_dirty = 1;
}

static int line_start(int pos) {
  while (pos > 0 && src[pos - 1] != '\n') {
    pos--;
  }
  return pos;
}

static int line_end(int pos) {
  while (pos < src_len && src[pos] != '\n') {
    pos++;
  }
  return pos;
}

static int pos_of_line(int line) {
  int pos = 0;
  while (line > 0 && pos < src_len) {
    if (src[pos++] == '\n') {
      line--;
    }
  }
  return pos;
}

/* Recompute cursor line/column and scroll so the cursor stays visible. */
static void ed_update_view(void) {
  ed_line = 0;
  ed_lines = 1;
  for (int i = 0; i < src_len; ++i) {
    if (src[i] == '\n') {
      if (i < ed_cursor) {
        ed_line++;
      }
      ed_lines++;
    }
  }
  ed_col = ed_cursor - line_start(ed_cursor);
  if (ed_line < ed_top) {
    ed_top = ed_line;
  }
  if (ed_line >= ed_top + ED_ROWS) {
    ed_top = ed_line - ED_ROWS + 1;
  }
  if (ed_col < ed_left) {
    ed_left = ed_col;
  }
  if (ed_col >= ed_left + CON_COLS - 1) {
    ed_left = ed_col - CON_COLS + 2;
  }
  ed_dirty = 1;
}

static void ed_set_text(const char *text) {
  src_len = 0;
  while (text[src_len] && src_len < CL_SRC_MAX - 1) {
    src[src_len] = text[src_len];
    src_len++;
  }
  ed_cursor = 0;
  ed_top = 0;
  ed_left = 0;
  ed_goal_col = 0;
  ed_modified = 0;
  ed_update_view();
}

static void ed_load_example(int index) {
  const char *p = k_examples;
  for (int i = 0; i < index; ++i) {
    p += cl_strlen(p) + 1;
  }
  ed_set_text(p);
}

static int ed_insert(const char *text, int n) {
  if (src_len + n >= CL_SRC_MAX) {
    ed_message("PROGRAM FULL", 1, 2000);
    return 0;
  }
  cl_move(&src[ed_cursor + n], &src[ed_cursor], src_len - ed_cursor);
  cl_move(&src[ed_cursor], text, n);
  src_len += n;
  ed_cursor += n;
  ed_modified = 1;
  return 1;
}

static void ed_delete(int pos, int n) {
  if (pos < 0 || n <= 0 || pos + n > src_len) {
    return;
  }
  cl_move(&src[pos], &src[pos + n], src_len - pos - n);
  src_len -= n;
  ed_modified = 1;
}

static void ed_move_lines(int delta) {
  int target = ed_line + delta;
  if (target < 0) {
    target = 0;
  }
  if (target >= ed_lines) {
    target = ed_lines - 1;
  }
  int start = pos_of_line(target);
  int len = line_end(start) - start;
  ed_cursor = start + (ed_goal_col < len ? ed_goal_col : len);
}

/* ENTER keeps the indentation and indents after '{'. */
static void ed_newline(void) {
  int start = line_start(ed_cursor);
  char text[40];
  int n = 0;
  text[n++] = '\n';
  for (int i = start; i < ed_cursor && src[i] == ' ' && n < 36; ++i) {
    text[n++] = ' ';
  }
  int before = ed_cursor - 1;
  while (before >= start && src[before] == ' ') {
    before--;
  }
  if (before >= start && src[before] == '{') {
    text[n++] = ' ';
    text[n++] = ' ';
  }
  ed_insert(text, n);
}

static void run_program(void);

static void ed_confirmed(int action) {
  uint32_t now = prg32_ticks_ms();
  if (ed_modified &&
      !(ed_confirm == action && (int32_t)(ed_confirm_until - now) > 0)) {
    ed_confirm = action;
    ed_confirm_until = now + 3000u;
    ed_message("PRESS AGAIN TO DISCARD CHANGES", 1, 3000);
    return;
  }
  ed_confirm = CONFIRM_NONE;
  if (action == CONFIRM_NEW) {
    ed_set_text("int main() {\n  \n  return 0;\n}\n");
    ed_cursor = 15;
    ed_message("NEW PROGRAM", 0, 1500);
  } else {
    ed_example = (ed_example + 1) % CL_EXAMPLE_COUNT;
    ed_load_example(ed_example);
    ed_message("EXAMPLE LOADED: F5 RUNS IT", 0, 2000);
  }
}

static void ed_key(int key) {
  int keep_goal = 0;
  if (key == PRG32_KEY_F1 + 4 || key == 18) { /* F5, CTRL+R */
    run_program();
    return;
  }
  if (key == PRG32_KEY_F1) {
    g_mode = MODE_HELP;
    ed_dirty = 1;
    return;
  }
  if (key == PRG32_KEY_F1 + 1 || key == 14) { /* F2, CTRL+N */
    ed_confirmed(CONFIRM_NEW);
  } else if (key == PRG32_KEY_F1 + 2 || key == 5) { /* F3, CTRL+E */
    ed_confirmed(CONFIRM_EXAMPLE);
  } else if (key == PRG32_KEY_LEFT) {
    if (ed_cursor > 0) ed_cursor--;
  } else if (key == PRG32_KEY_RIGHT) {
    if (ed_cursor < src_len) ed_cursor++;
  } else if (key == PRG32_KEY_UP) {
    ed_move_lines(-1);
    keep_goal = 1;
  } else if (key == PRG32_KEY_DOWN) {
    ed_move_lines(1);
    keep_goal = 1;
  } else if (key == PRG32_KEY_PAGE_UP) {
    ed_move_lines(-(ED_ROWS - 1));
    keep_goal = 1;
  } else if (key == PRG32_KEY_PAGE_DOWN) {
    ed_move_lines(ED_ROWS - 1);
    keep_goal = 1;
  } else if (key == PRG32_KEY_HOME) {
    ed_cursor = line_start(ed_cursor);
  } else if (key == PRG32_KEY_END) {
    ed_cursor = line_end(ed_cursor);
  } else if (key == PRG32_KEY_BACKSPACE) {
    if (ed_cursor > 0) {
      ed_cursor--;
      ed_delete(ed_cursor, 1);
    }
  } else if (key == PRG32_KEY_DELETE) {
    ed_delete(ed_cursor, 1);
  } else if (key == PRG32_KEY_ENTER) {
    ed_newline();
  } else if (key == PRG32_KEY_TAB) {
    ed_insert("  ", 2);
  } else if (key >= 32 && key < 127) {
    if (key == '}') { /* un-indent a closing brace */
      int start = line_start(ed_cursor);
      int blank = 1;
      for (int i = start; i < ed_cursor; ++i) {
        blank &= src[i] == ' ';
      }
      if (blank && ed_cursor - start >= 2) {
        ed_cursor -= 2;
        ed_delete(ed_cursor, 2);
      }
    }
    char c = (char)key;
    ed_insert(&c, 1);
  } else {
    return;
  }
  ed_update_view();
  if (!keep_goal) {
    ed_goal_col = ed_col;
  }
}

/* ---- syntax colouring ---- */

static uint8_t hl_class[HL_MAX];
static char draw_buf[CON_COLS + 1];

/* Classify the characters of one line; returns the block-comment state
 * at the end of the line. */
static int hl_line(int start, int end, int in_comment) {
  int n = end - start;
  if (n > HL_MAX) {
    n = HL_MAX;
  }
  int i = 0;
  while (i < n) {
    const char *p = &src[start + i];
    int cls = HL_TEXT;
    int len = 1;
    if (in_comment) {
      cls = HL_COMMENT;
      if (p[0] == '*' && i + 1 < n && p[1] == '/') {
        len = 2;
        in_comment = 0;
      }
    } else if (p[0] == '/' && i + 1 < n && p[1] == '*') {
      cls = HL_COMMENT;
      len = 2;
      in_comment = 1;
    } else if (p[0] == '/' && i + 1 < n && p[1] == '/') {
      cls = HL_COMMENT;
      len = n - i;
    } else if (p[0] == '#') {
      cls = HL_PREPROC;
      len = n - i;
    } else if (p[0] == '"' || p[0] == '\'') {
      cls = HL_STRING;
      while (i + len < n && p[len] != p[0]) {
        len += p[len] == '\\' && i + len + 1 < n ? 2 : 1;
      }
      if (i + len < n) {
        len++;
      }
    } else if (is_digit(p[0])) {
      cls = HL_NUMBER;
      while (i + len < n && (is_alpha(p[len]) || is_digit(p[len]))) {
        len++;
      }
    } else if (is_alpha(p[0])) {
      while (i + len < n && (is_alpha(p[len]) || is_digit(p[len]))) {
        len++;
      }
      if (word_index(k_keywords, p, len) >= 0 ||
          word_index(k_int_words, p, len) >= 0 ||
          word_index(k_skip_words, p, len) >= 0) {
        cls = HL_KEYWORD;
      } else if (word_index(k_builtins, p, len) >= 0) {
        cls = HL_BUILTIN;
      } else if (word_index(k_constants, p, len) >= 0) {
        cls = HL_CONST;
      }
    }
    for (int k = 0; k < len && i < n; ++k) {
      hl_class[i++] = (uint8_t)cls;
    }
  }
  /* A comment opened after the highlighted part still counts. */
  for (int k = start + n; k + 1 < end; ++k) {
    if (!in_comment && src[k] == '/' && src[k + 1] == '*') {
      in_comment = 1;
    } else if (in_comment && src[k] == '*' && src[k + 1] == '/') {
      in_comment = 0;
    }
  }
  return in_comment;
}

/* Draw text[0..n) at a row using runs of equal colour. */
static void draw_runs(int x_col, int row_y, const char *text,
                      const uint8_t *classes, int n) {
  int i = 0;
  while (i < n) {
    int cls = classes[i];
    int k = 0;
    while (i < n && classes[i] == cls) {
      draw_buf[k++] = text[i++];
    }
    draw_buf[k] = '\0';
    prg32_gfx_text8((x_col + i - k) * 8, row_y, draw_buf, k_hl_colors[cls],
                    PRG32_COLOR_BLACK);
  }
}

static void draw_bar(int row_y, const char *text, uint16_t bg) {
  prg32_gfx_rect(0, row_y, PRG32_GAME_W, 8, bg);
  prg32_gfx_text8(0, row_y, text, COLOR_BAR_FG, bg);
}

static void ed_draw_bar(void) {
  char bar[CON_COLS + 1];
  uint32_t now = prg32_ticks_ms();
  bar[0] = '\0';
  int error_bar = 0;
  if ((int32_t)(ed_msg_until - now) > 0) {
    str_append(bar, sizeof(bar), " ");
    str_append(bar, sizeof(bar), ed_msg);
    error_bar = ed_msg_error;
  } else {
    uint32_t state = prg32_btkbd_state();
    str_append(bar, sizeof(bar),
               state == PRG32_BTKBD_STATE_CONNECTED ||
                       state == PRG32_BTKBD_STATE_CONSOLE
                   ? " PRG32 C  F1 HELP F5 RUN"
                   : " NO KEYBOARD: PAIR ONE IN SETUP");
    str_append(bar, sizeof(bar), "  ");
    str_append_uint(bar, sizeof(bar), (uint32_t)ed_line + 1);
    str_append(bar, sizeof(bar), ":");
    str_append_uint(bar, sizeof(bar), (uint32_t)ed_col + 1);
  }
  draw_bar(0, bar, error_bar ? COLOR_ERROR_BG : COLOR_BAR_BG);
}

/* Draw the visible text rows, or only `only_line` when it is >= 0. */
static void ed_draw_rows(int only_line) {
  int pos = 0;
  int line = 0;
  int in_comment = 0;
  while (pos <= src_len && line < ed_top + ED_ROWS) {
    int end = line_end(pos);
    int after = hl_line(pos, end, in_comment); /* fills hl_class[] */
    int y = (line - ed_top + 1) * 8;
    if (line >= ed_top && (only_line < 0 || line == only_line)) {
      if (only_line >= 0) {
        prg32_gfx_rect(0, y, PRG32_GAME_W, 8, PRG32_COLOR_BLACK);
      }
      int from = ed_left;
      int count = end - pos - from;
      if (count > CON_COLS) {
        count = CON_COLS;
      }
      int colored = count;
      if (from + colored > HL_MAX) {
        colored = from < HL_MAX ? HL_MAX - from : 0;
      }
      if (colored > 0) {
        draw_runs(0, y, &src[pos + from], &hl_class[from], colored);
      }
      for (int k = colored > 0 ? colored : 0; k < count; ++k) {
        draw_buf[0] = src[pos + from + k]; /* tail of a very long line */
        draw_buf[1] = '\0';
        prg32_gfx_text8(k * 8, y, draw_buf, k_hl_colors[HL_TEXT],
                        PRG32_COLOR_BLACK);
      }
    }
    in_comment = after;
    line++;
    pos = end + 1;
  }
}

static void ed_draw_cursor(void) {
  if (!ed_blink) {
    return;
  }
  char under = ed_cursor < src_len && src[ed_cursor] != '\n' ? src[ed_cursor]
                                                            : ' ';
  draw_buf[0] = under;
  draw_buf[1] = '\0';
  prg32_gfx_text8((ed_col - ed_left) * 8, (ed_line - ed_top + 1) * 8,
                  draw_buf, PRG32_COLOR_BLACK, PRG32_COLOR_WHITE);
}

static void ed_draw(void) {
  prg32_gfx_clear(PRG32_COLOR_BLACK);
  ed_draw_bar();
  ed_draw_rows(-1);
  ed_draw_cursor();
}

static void help_draw(void) {
  prg32_gfx_clear(COLOR_BAR_BG);
  const char *p = k_help;
  int row = 0;
  while (*p && row < 25) {
    int n = 0;
    while (p[n] && p[n] != '\n' && n < CON_COLS) {
      draw_buf[n] = p[n];
      n++;
    }
    draw_buf[n] = '\0';
    prg32_gfx_text8(0, row * 8, draw_buf,
                    row == 0 || row == 8 ? 0xFFEA : 0xFFFF, COLOR_BAR_BG);
    p += n;
    while (*p && *p != '\n') {
      p++;
    }
    if (*p == '\n') {
      p++;
    }
    row++;
  }
}

/* ======================================================================== */
/* Running programs                                                         */
/* ======================================================================== */

static void ed_goto_line(int line) {
  if (line > 0) {
    ed_cursor = pos_of_line(line - 1);
    ed_update_view();
    ed_goal_col = 0;
  }
}

static void error_text(char *out, int cap, const char *prefix) {
  out[0] = '\0';
  str_append(out, cap, prefix);
  if (err_line > 0) {
    str_append(out, cap, "LINE ");
    str_append_uint(out, cap, (uint32_t)err_line);
    str_append(out, cap, ": ");
  }
  str_append(out, cap, err_msg);
}

static void run_program(void) {
  if (cl_compile() != 0) {
    char text[CON_COLS + 1];
    error_text(text, sizeof(text), "");
    ed_goto_line(err_line);
    ed_message(text, 1, 8000);
    return;
  }
  vm_start();
  prg32_btkbd_flush();
  g_mode = MODE_RUN;
}

static void run_stop(const char *why) {
  if (vm_state == VM_RUN) {
    vm_state = VM_DONE;
    vm_exit_code = -1;
    err_msg[0] = '\0';
    str_append(err_msg, sizeof(err_msg), why);
  }
}

static void run_update(uint32_t joy_new) {
  int key;
  while ((key = prg32_btkbd_read_key()) != 0) {
    if (key == PRG32_KEY_ESCAPE || key == 3) { /* ESC or CTRL+C */
      run_stop("STOPPED");
      break;
    }
    keyq_push(key);
  }
  if (joy_new & PRG32_BTN_B) {
    run_stop("STOPPED");
  }
  if (vm_state == VM_RUN) {
    vm_run_slice();
  }
  if (vm_state != VM_RUN) {
    g_mode = MODE_ENDED;
    con_dirty = 1;
    prg32_btkbd_flush();
  }
}

static void ended_update(uint32_t joy_new) {
  int key = prg32_btkbd_read_key();
  if (key || (joy_new & (PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_SELECT))) {
    g_mode = MODE_EDIT;
    if (vm_state == VM_ERROR) {
      char text[CON_COLS + 1];
      error_text(text, sizeof(text), "");
      ed_goto_line(err_line);
      ed_message(text, 1, 8000);
    }
    ed_dirty = 1;
  }
}

static void con_draw(void) {
  char bar[CON_COLS + 1];
  if (!canvas) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    for (int y = 0; y < CON_ROWS; ++y) {
      int x = 0;
      while (x < CON_COLS) {
        uint8_t color = con_fg[y][x];
        int k = 0;
        int start = x;
        while (x < CON_COLS && con_fg[y][x] == color) {
          draw_buf[k++] = con_ch[y][x++];
        }
        draw_buf[k] = '\0';
        prg32_gfx_text8(start * 8, y * 8, draw_buf, k_con_palette[color],
                        PRG32_COLOR_BLACK);
      }
    }
    if (g_mode == MODE_RUN && waiting_input && ed_blink) {
      prg32_gfx_rect(con_x * 8, con_y * 8 + 7, 8, 1, PRG32_COLOR_WHITE);
    }
  }
  if (g_mode == MODE_RUN) {
    if (!canvas) {
      draw_bar(CON_ROWS * 8,
               waiting_input ? " WAITING FOR INPUT - ESC STOPS"
                             : " RUNNING - ESC STOPS",
               COLOR_BAR_BG);
    }
  } else if (vm_state == VM_ERROR) {
    error_text(bar, sizeof(bar), " ERROR ");
    draw_bar(CON_ROWS * 8, bar, COLOR_ERROR_BG);
  } else {
    bar[0] = '\0';
    if (vm_exit_code == -1 && err_msg[0]) {
      str_append(bar, sizeof(bar), " STOPPED");
    } else {
      str_append(bar, sizeof(bar), " ENDED, EXIT CODE ");
      if (vm_exit_code < 0) {
        str_append(bar, sizeof(bar), "-");
        str_append_uint(bar, sizeof(bar), 0u - (uint32_t)vm_exit_code);
      } else {
        str_append_uint(bar, sizeof(bar), (uint32_t)vm_exit_code);
      }
    }
    str_append(bar, sizeof(bar), " - PRESS A KEY");
    draw_bar(CON_ROWS * 8, bar, COLOR_BAR_BG);
  }
}

/* ======================================================================== */
/* Cartridge entry points                                                   */
/* ======================================================================== */

static int g_last_mode = -1;
static int g_last_blink = -1;

void c_language_init(void) {
  /* Typed keys are program text here, not joystick buttons. */
  prg32_btkbd_set_mapping(0);
  prg32_btkbd_flush();
  g_mode = MODE_EDIT;
  g_joy_last = prg32_input_read();
  vm_state = VM_IDLE;
  ed_example = 0;
  ed_load_example(0);
  ed_message("WELCOME TO PRG32 C - F1 FOR HELP", 0, 3000);
}

void c_language_update(void) {
  uint32_t joy = prg32_input_read();
  uint32_t joy_new = joy & ~g_joy_last;
  g_joy_last = joy;
  ed_blink = (int)((prg32_ticks_ms() / 400u) & 1u);

  if (g_mode == MODE_EDIT) {
    int key;
    int budget = 16; /* at most this many keys per frame */
    while (g_mode == MODE_EDIT && budget-- > 0 &&
           (key = prg32_btkbd_read_key()) != 0) {
      ed_key(key);
    }
    /* Joystick fallback: D-pad moves, A runs, SELECT loads an example. */
    if (g_mode == MODE_EDIT) {
      if (joy_new & PRG32_BTN_LEFT) ed_key(PRG32_KEY_LEFT);
      if (joy_new & PRG32_BTN_RIGHT) ed_key(PRG32_KEY_RIGHT);
      if (joy_new & PRG32_BTN_UP) ed_key(PRG32_KEY_UP);
      if (joy_new & PRG32_BTN_DOWN) ed_key(PRG32_KEY_DOWN);
      if (joy_new & PRG32_BTN_SELECT) ed_confirmed(CONFIRM_EXAMPLE);
      if (joy_new & PRG32_BTN_A) run_program();
    }
  } else if (g_mode == MODE_HELP) {
    if (prg32_btkbd_read_key() || (joy_new & (PRG32_BTN_A | PRG32_BTN_B))) {
      g_mode = MODE_EDIT;
      ed_dirty = 1;
    }
  } else if (g_mode == MODE_RUN) {
    run_update(joy_new);
  } else {
    ended_update(joy_new);
  }
}

void c_language_draw(void) {
  /* The framebuffer keeps its content between frames, so only redraw what
   * changed. Programs that draw graphics own the screen. */
  int mode_changed = g_mode != g_last_mode;
  int blink_changed = ed_blink != g_last_blink;
  g_last_mode = g_mode;
  g_last_blink = ed_blink;
  if (g_mode == MODE_EDIT) {
    int msg_expired =
        ed_msg_until && (int32_t)(ed_msg_until - prg32_ticks_ms()) <= 0;
    if (msg_expired) {
      ed_msg_until = 0;
    }
    if (mode_changed || ed_dirty || msg_expired) {
      ed_draw();
      ed_dirty = 0;
    } else if (blink_changed) {
      ed_draw_rows(ed_line); /* only the cursor line */
      ed_draw_cursor();
    }
  } else if (g_mode == MODE_HELP) {
    if (mode_changed) {
      help_draw();
    }
  } else if (mode_changed || con_dirty ||
             (blink_changed && waiting_input && !canvas)) {
    con_draw();
    con_dirty = 0;
  }
}
