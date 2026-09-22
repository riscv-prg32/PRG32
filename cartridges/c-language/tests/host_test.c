/*
 * Host test for the PRG32 C interpreter cartridge.
 *
 * The cartridge source is compiled for the development computer with small
 * stand-ins for the PRG32 functions it calls. Each test compiles a C program
 * with the cartridge's own compiler, runs it in the cartridge's VM, and
 * compares the console output (or the error) with the expected text.
 *
 * Build and run: cartridges/c-language/tests/run_host_tests.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_out[8192];
static size_t g_out_len;
static void host_trace(int c) {
  if (g_out_len + 1 < sizeof(g_out)) {
    g_out[g_out_len++] = (char)c;
    g_out[g_out_len] = '\0';
  }
}

#define CL_HOST_TEST 1
#define CL_TRACE_PUTC(c) host_trace(c)
#include "../src/c_language.c"

/* ---- PRG32 stand-ins ---- */
static uint32_t g_ms;
static const char *g_keys = "";
uint32_t prg32_ticks_ms(void) { return g_ms++; }
uint32_t prg32_input_read(void) { return 0; }
uint32_t prg32_random_number(uint32_t min, uint32_t max) { (void)max; return min; }
void prg32_gfx_clear(uint16_t c) { (void)c; }
void prg32_gfx_pixel(int x, int y, uint16_t c) { (void)x; (void)y; (void)c; }
void prg32_gfx_rect(int x, int y, int w, int h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void prg32_gfx_text8(int x, int y, const char *s, uint16_t fg, uint16_t bg) { (void)x; (void)y; (void)s; (void)fg; (void)bg; }
void prg32_audio_note(uint8_t ch, uint8_t in, uint8_t n, uint8_t v, uint32_t ms) { (void)ch; (void)in; (void)n; (void)v; (void)ms; }
uint32_t prg32_btkbd_state(void) { return PRG32_BTKBD_STATE_CONSOLE; }
int prg32_btkbd_read_key(void) {
  if (!*g_keys) return 0;
  return (unsigned char)*g_keys++;
}
int prg32_btkbd_key_down(uint32_t u) { (void)u; return 0; }
void prg32_btkbd_flush(void) {}
int prg32_btkbd_set_mapping(int e) { (void)e; return 1; }

static int g_failures;

/* Compile and run; returns "OUT:<output>" or "ERR <line>: <message>". */
static const char *run(const char *program, const char *keys) {
  static char result[8300];
  g_out_len = 0;
  g_out[0] = '\0';
  src_len = (int)strlen(program);
  memcpy(src, program, (size_t)src_len);
  if (cl_compile() != 0) {
    snprintf(result, sizeof(result), "ERR %d: %s", err_line, err_msg);
    return result;
  }
  vm_start();
  g_keys = keys;
  for (int frame = 0; frame < 20000 && vm_state == VM_RUN; ++frame) {
    int key;
    while ((key = prg32_btkbd_read_key()) != 0) keyq_push(key);
    vm_run_slice();
  }
  if (vm_state == VM_ERROR) {
    snprintf(result, sizeof(result), "RTE %d: %s", err_line, err_msg);
  } else if (vm_state == VM_RUN) {
    snprintf(result, sizeof(result), "TIMEOUT");
  } else {
    snprintf(result, sizeof(result), "OUT:%s", g_out);
  }
  return result;
}

static void check(const char *name, const char *program, const char *keys,
                  const char *expected) {
  const char *got = run(program, keys);
  if (strcmp(got, expected) != 0) {
    printf("FAIL %s\n  expected: [%s]\n  got:      [%s]\n", name, expected, got);
    g_failures++;
  } else {
    printf("ok   %s\n", name);
  }
}

int main(void) {
  check("hello", "int main(){ printf(\"hi %d\\n\", 42); return 0; }", "", "OUT:hi 42\n");
  check("arith", "int main(){ printf(\"%d %d %d %d %d\", 7/2, -7/2, 7%3, 1<<4, -16>>2); }", "", "OUT:3 -3 1 16 -4");
  check("precedence", "int main(){ printf(\"%d\", 2+3*4-(8-2)/3); }", "", "OUT:12");
  check("logic", "int main(){ printf(\"%d %d %d %d\", 5||0, 0&&1, !3, 3>2 ? 10 : 20); }", "", "OUT:1 0 0 10");
  check("compound", "int main(){ int x = 5; x += 3; x *= 2; x -= 1; x /= 3; x %= 4; x <<= 2; x |= 1; printf(\"%d\", x); }", "", "OUT:5");
  check("incdec", "int main(){ int i = 5; int a = i++; int b = ++i; int c = i--; printf(\"%d %d %d %d\", a, b, c, i); }", "", "OUT:5 7 7 6");
  check("loops", "int main(){ int i, s = 0; for (i = 0; i < 10; i++) { if (i == 3) continue; if (i == 8) break; s += i; } while (s > 20) s -= 5; do { s++; } while (s < 22); printf(\"%d\", s); }", "", "OUT:22");
  check("for-decl", "int main(){ int s = 0; for (int i = 0; i < 3; i++) s += i; for (int i = 0; i < 3; i++) s += i; printf(\"%d\", s); }", "", "OUT:6");
  check("do-continue", "int main(){ int i = 0, n = 0; do { i++; if (i % 2) continue; n++; } while (i < 10); printf(\"%d\", n); }", "", "OUT:5");
  check("recursion", "int fib(int n){ if (n < 2) return n; return fib(n-1)+fib(n-2); } int main(){ printf(\"%d\", fib(15)); }", "", "OUT:610");
  check("forward", "int twice(int x); int main(){ printf(\"%d\", twice(21)); } int twice(int x){ return x * 2; }", "", "OUT:42");
  check("forward-implicit", "int main(){ printf(\"%d\", sq(9)); } int sq(int x){ return x * x; }", "", "OUT:81");
  check("pointers", "int main(){ int a = 1; int *p = &a; *p = 9; int **q = &p; **q += 1; printf(\"%d\", a); }", "", "OUT:10");
  check("arrays", "int g[5] = {5, 4, 3}; int main(){ int a[4]; int i; for (i = 0; i < 4; i++) a[i] = i * i; printf(\"%d %d %d %d\", a[3], g[0], g[2], g[4]); }", "", "OUT:9 5 3 0");
  check("ptr-arith", "int main(){ int a[4]; int *p = a; p[2] = 7; p = p + 2; printf(\"%d %d\", *p, p - a); }", "", "OUT:7 2");
  check("strings", "char buf[32]; int main(){ strcpy(buf, \"abc\"); strcat(buf, \"def\"); printf(\"%s %d %d\", buf, strlen(buf), strcmp(\"a\", \"b\") < 0); }", "", "OUT:abcdef 6 1");
  check("chars", "int main(){ char c = 'A'; c++; char s[4]; s[0] = c; s[1] = 0; printf(\"%s %d\", s, c); }", "", "OUT:B 66");
  check("char-array-init", "char s[] = \"hey\"; char t[8] = \"yo\"; int main(){ printf(\"%s-%s-%c\", s, t, s[1]); }", "", "OUT:hey-yo-e");
  check("printf-format", "int main(){ printf(\"[%5d][%-4d][%04d][%x][%X][%c][%3s][%%]\", 42, 7, -5, 255, 255, 'z', \"ab\"); }", "", "OUT:[   42][7   ][-005][ff][FF][z][ ab][%]");
  check("enum-define", "#include <stdio.h>\n#define TEN 10\nenum { A, B = 5, C };\nint main(){ printf(\"%d %d %d %d\", A, B, C, TEN); }", "", "OUT:0 5 6 10");
  check("sizeof-cast", "int main(){ int x = 321; printf(\"%d %d %d %d\", sizeof(int), sizeof(char), sizeof(int*), (char)x); }", "", "OUT:4 1 4 65");
  check("globals-shadow", "int x = 3; int f(int x){ return x + 1; } int main(){ printf(\"%d %d\", f(10), x); }", "", "OUT:11 3");
  check("malloc", "int main(){ int *p = malloc(16); p[3] = 5; printf(\"%d\", p[3]); free(p); }", "", "OUT:5");
  check("exit-code", "int main(){ exit(3); printf(\"no\"); }", "", "OUT:");
  check("getchar", "int main(){ int c = getchar(); printf(\"=%c\", c); }", "q", "OUT:q=q");
  check("readline", "int main(){ char b[10]; readline(b, 10); printf(\"<%d:%d>\", atoi(b), strlen(b)); }", "12x\b3\n", "OUT:12x\b3\n<123:3>");
  check("err-syntax", "int main(){\n  int x = ;\n}", "", "ERR 2: bad expression");
  check("err-undefined", "int main(){\n  y = 1;\n}", "", "ERR 2: undefined: y");
  check("err-missing-main", "int f(){ return 1; }", "", "ERR 1: main() is missing");
  check("err-args", "int main(){ strlen(); }", "", "ERR 1: wrong number of arguments: strlen");
  check("err-undefined-fn", "int g(int x); int main(){ return g(1); }", "", "ERR 1: function not defined: g");
  check("rte-div0", "int main(){\n  int z = 0;\n  return 5 / z;\n}", "", "RTE 3: division by zero");
  check("rte-null", "int main(){\n  int *p = 0;\n  *p = 1;\n}", "", "RTE 3: NULL pointer access");
  check("rte-recursion", "int f(int n){ return f(n + 1); }\nint main(){ return f(0); }", "", "RTE 1: stack overflow (endless recursion?)");
  check("deep-nesting", "int main(){ return ((((((((((((((((((((((1)))))))))))))))))))))); }", "", "ERR 1: expression too complex");
  check("nested-comment", "/* a\n b */ int main(){ // x\n printf(\"ok\"); }", "", "OUT:ok");

  /* Every built-in example must compile. */
  for (int i = 0; i < CL_EXAMPLE_COUNT; ++i) {
    ed_load_example(i);
    if (cl_compile() != 0) {
      printf("FAIL example %d: line %d: %s\n", i, err_line, err_msg);
      g_failures++;
    } else {
      printf("ok   example %d compiles (%d words)\n", i, code_len);
    }
  }
  ed_load_example(0);
  static char copy[CL_SRC_MAX];
  memcpy(copy, src, (size_t)src_len);
  copy[src_len] = '\0';
  const char *hello = run(copy, "");
  if (strncmp(hello, "OUT:Hello", 9) != 0) printf("  got %s\n", hello);
  printf("%s   example 0 runs\n", strncmp(hello, "OUT:Hello, PRG32!", 17) == 0 ? "ok  " : "FAIL");
  if (strncmp(hello, "OUT:Hello, PRG32!", 17) != 0) g_failures++;
  ed_load_example(1);
  memcpy(copy, src, (size_t)src_len);
  copy[src_len] = '\0';
  hello = run(copy, "");
  if (!strstr(hello, "fib(19) = 4181")) { printf("FAIL example 1 output\n"); g_failures++; } else printf("ok   example 1 runs\n");

  printf(g_failures ? "%d FAILURE(S)\n" : "all host tests passed\n", g_failures);
  return g_failures ? 1 : 0;
}
