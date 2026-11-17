// house_lang.h -- the on-device interpreter for the home language (house.py is the reference).
//
// A small subset of Python for a home and a screen: on, off, set, is_on, temp, wait, say; clear, big, line, small;
// time, date, weather, price; whole numbers and strings, variables, + - * // %, comparisons, and/or/not, str abs
// min max, for i in range(...), while, if/elif/else, break, continue, pass. Anything else is refused at parse time.
//
// One header, C99, no malloc: the program is parsed into a fixed node pool and run by walking it. Everything the
// program does goes through callbacks (hl_io), so the same code runs on the host (a home held in memory, for
// testing against house.py) and on a chip or a phone. The callbacks are where the words that say where become
// devices, and where a lock says no. The run stops after HL_MAX_STEPS steps or HL_MAX_WAIT_MS of waiting: whatever
// the model writes, the home stays in charge.
//
// This file began as runtime/oled_lang.h of esp32-ai (the lexer, the parser and the evaluator are the same).
//
// Usage:
//   ol_machine m; hl_io io = {...};
//   if (ol_parse(&m, src) != 0) -> m.err holds the reason
//   if (ol_run(&m, &io) != 0)   -> m.err holds the reason (what was done before stays done)

#ifndef HOUSE_LANG_H
#define HOUSE_LANG_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define OL_MAX_SRC     4096
#define OL_MAX_TOKENS  1200
#define OL_MAX_NODES   1200
#define OL_MAX_VARS    24
#define OL_NAME_LEN    16
#define OL_STR_LEN     128
#define OL_TEMP_BYTES  2048
#define OL_MAX_STEPS   5000
#define OL_MAX_WAIT_MS 43200000L   // half a day
#define HL_SAY 60               // characters in a where, a row, an answer
#define OL_MAX_DEPTH   24

// What a program can do. A callback that returns -1 has refused, with the reason in err (HL_ERR bytes).
#define HL_ERR 96
typedef struct {
  void *ctx;
  int (*sw)(void *ctx, const char *where, int on, char *err);            // on(where), off(where)
  int (*set)(void *ctx, const char *where, int n, char *err);            // set(where, n)
  int (*is_on)(void *ctx, const char *where, int *out, char *err);       // is_on(where)
  int (*temp)(void *ctx, const char *where, int *out, char *err);        // temp(where), degrees
  void (*wait)(void *ctx, long ms);
  void (*say)(void *ctx, const char *text);
  void (*clear)(void *ctx);                                              // an empty screen
  int (*row)(void *ctx, int size, const char *text, char *err);          // 0 big, 1 line, 2 small; -1: the screen is full
  void (*now)(void *ctx, int date, char *out, int n);                    // time() or date() as text
  int (*look)(void *ctx, int which, const char *what, char *out, int n, char *err);   // a source, HL_WEATHER..HL_NAMEDAY
  void (*done)(void *ctx);
} ol_io;
typedef ol_io hl_io;

// ---------------------------------------------------------------- tokens
enum { T_EOF, T_NEWLINE, T_INDENT, T_DEDENT, T_NAME, T_INT, T_STR, T_OP, T_KW };
enum { K_FOR, K_IN, K_WHILE, K_IF, K_ELIF, K_ELSE, K_AND, K_OR, K_NOT, K_BREAK, K_CONTINUE, K_PASS, K__N };
static const char *OL_KW[K__N] = {"for", "in", "while", "if", "elif", "else", "and", "or", "not", "break", "continue", "pass"};

typedef struct { uint8_t type, kw; char op[3]; int32_t ival; uint16_t start, len; uint16_t line; } ol_tok;

// ---------------------------------------------------------------- nodes
enum {
  N_INT, N_STR, N_NAME, N_BIN, N_NEG, N_NOT, N_AND, N_OR, N_CMP, N_CALL,                // expressions
  N_EXPR, N_ASSIGN, N_AUG, N_IF, N_FOR, N_WHILE, N_BREAK, N_CONTINUE, N_PASS, N_BLOCK  // statements
};
enum { F_ON, F_OFF, F_SET, F_IS_ON, F_TEMP, F_WAIT, F_SAY, F_CLEAR, F_BIG, F_LINE, F_SMALL, F_TIME, F_DATE,
       F_WEATHER, F_PRICE, F_TOMORROW, F_SUNRISE, F_SUNSET, F_AIR, F_UNTIL, F_NEWS, F_NAMEDAY,      // the sources, in HL_ order
       F_STR, F_ABS, F_MIN, F_MAX, F_RANGE, F__N };
enum { HL_WEATHER, HL_PRICE, HL_TOMORROW, HL_SUNRISE, HL_SUNSET, HL_AIR, HL_UNTIL, HL_NEWS, HL_NAMEDAY, HL_SOURCES };
static const char *OL_FN[F__N] = {"on", "off", "set", "is_on", "temp", "wait", "say", "clear", "big", "line", "small", "time", "date",
                                  "weather", "price", "tomorrow", "sunrise", "sunset", "air", "until", "news", "nameday",
                                  "str", "abs", "min", "max", "range"};
static const int8_t OL_ARGS[F__N] = {1, 1, 2, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0,
                                     1, 1, 1, 1, 1, 1, 1, 0, 0,
                                     1, 1, -1, -1, -2};   // -1: 1+ args, -2: 1..3

// a node: kind, small op/fn field, int payload, and children as a linked list (first child, next sibling)
typedef struct { uint8_t kind, op; int16_t first, next; int32_t ival; uint16_t sstart, slen; } ol_node;

typedef struct { uint8_t is_str; int32_t i; const char *s; uint16_t n; } ol_val;
typedef struct { char name[OL_NAME_LEN]; uint8_t used, is_str; int32_t i; char s[OL_STR_LEN]; uint16_t n; } ol_var;

typedef struct {
  const char *src; int src_len;
  ol_tok tok[OL_MAX_TOKENS]; int ntok, pos;
  ol_node node[OL_MAX_NODES]; int nnode; int root;
  ol_var var[OL_MAX_VARS];
  char temp[OL_TEMP_BYTES]; int temp_used;
  long steps; long waited; int depth;
  int brk, cont;          // loop control flags while unwinding a block
  volatile int stop;      // set from a callback (a new prompt arrived): the run ends at the next step
  char err[96];
} ol_machine;

static int ol_fail(ol_machine *m, const char *msg) { if (!m->err[0]) snprintf(m->err, sizeof m->err, "%s", msg); return -1; }

// ---------------------------------------------------------------- lexer
static int ol_lex(ol_machine *m) {
  const char *s = m->src; int n = m->src_len, i = 0, line = 1;
  int indent[32] = {0}, depth = 0, at_line_start = 1, paren = 0;
  m->ntok = 0;
#define PUSH(t) do { if (m->ntok >= OL_MAX_TOKENS) return ol_fail(m, "program too long"); m->tok[m->ntok] = (t); m->tok[m->ntok].line = line; m->ntok++; } while (0)
  while (i <= n) {
    if (at_line_start && paren == 0) {
      int col = 0, j = i;
      while (j < n && (s[j] == ' ' || s[j] == '\t')) { col += s[j] == '\t' ? 4 : 1; j++; }
      if (j >= n) { i = n + 1; break; }
      if (s[j] == '\n' || s[j] == '#') {               // blank or comment line: no indentation change
        while (j < n && s[j] != '\n') j++;
        i = j + 1; line++; continue;
      }
      if (col > indent[depth]) {
        if (depth >= 31) return ol_fail(m, "nested too deep");
        indent[++depth] = col; ol_tok t = {T_INDENT}; PUSH(t);
      } else {
        while (col < indent[depth]) { depth--; ol_tok t = {T_DEDENT}; PUSH(t); }
        if (col != indent[depth]) return ol_fail(m, "syntax: unindent does not match");
      }
      i = j; at_line_start = 0;
    }
    if (i >= n) break;
    char c = s[i];
    if (c == '\n') {
      if (paren == 0) { ol_tok t = {T_NEWLINE}; PUSH(t); at_line_start = 1; }
      i++; line++; continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
    if (c == '#') { while (i < n && s[i] != '\n') i++; continue; }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
      int j = i; while (j < n && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z') || (s[j] >= '0' && s[j] <= '9') || s[j] == '_')) j++;
      ol_tok t = {T_NAME}; t.start = i; t.len = j - i;
      for (int k = 0; k < K__N; k++)
        if ((int)strlen(OL_KW[k]) == j - i && !memcmp(OL_KW[k], s + i, j - i)) { t.type = T_KW; t.kw = k; }
      if ((j - i == 4 && !memcmp(s + i, "True", 4)) || (j - i == 5 && !memcmp(s + i, "False", 5)) || (j - i == 4 && !memcmp(s + i, "None", 4)))
        return ol_fail(m, "only whole numbers and strings");
      PUSH(t); i = j; continue;
    }
    if (c >= '0' && c <= '9') {
      int64_t v = 0; int j = i;
      while (j < n && s[j] >= '0' && s[j] <= '9') { v = v * 10 + (s[j] - '0'); if (v > 2147483647) return ol_fail(m, "number too big"); j++; }
      if (j < n && (s[j] == '.' || s[j] == 'e' || s[j] == 'E' || s[j] == 'x' || s[j] == '_')) return ol_fail(m, "only whole numbers and strings");
      if (s[i] == '0' && j - i > 1 && v != 0) return ol_fail(m, "syntax: a number does not start with 0");
      ol_tok t = {T_INT}; t.ival = (int32_t)v; PUSH(t); i = j; continue;
    }
    if (c == '"' || c == '\'') {
      int j = i + 1;
      while (j < n && s[j] != c && s[j] != '\n') { if (s[j] == '\\') return ol_fail(m, "no escapes in strings"); j++; }
      if (j >= n || s[j] != c) return ol_fail(m, "syntax: unterminated string");
      if (j - i - 1 >= OL_STR_LEN) return ol_fail(m, "string too long");
      ol_tok t = {T_STR}; t.start = i + 1; t.len = j - i - 1; PUSH(t); i = j + 1; continue;
    }
    static const char *ops[] = {"//=", "==", "!=", "<=", ">=", "+=", "-=", "*=", "%=", "//", "+", "-", "*", "%", "<", ">", "=", "(", ")", ",", ":"};
    int matched = 0;
    for (unsigned k = 0; k < sizeof ops / sizeof *ops; k++) {
      int L = (int)strlen(ops[k]);
      if (i + L <= n && !memcmp(s + i, ops[k], L)) {
        ol_tok t = {T_OP}; memcpy(t.op, ops[k], L); t.op[L] = 0; PUSH(t);
        if (c == '(') paren++; else if (c == ')') paren--;
        i += L; matched = 1; break;
      }
    }
    if (!matched) return ol_fail(m, "syntax: unexpected character");
  }
  if (m->ntok && m->tok[m->ntok - 1].type != T_NEWLINE) { ol_tok t = {T_NEWLINE}; PUSH(t); }
  while (depth > 0) { depth--; ol_tok t = {T_DEDENT}; PUSH(t); }
  { ol_tok t = {T_EOF}; PUSH(t); }
#undef PUSH
  return 0;
}

// ---------------------------------------------------------------- parser
static ol_tok *ol_peek(ol_machine *m) { return &m->tok[m->pos]; }
static int ol_is_op(ol_machine *m, const char *op) { ol_tok *t = ol_peek(m); return t->type == T_OP && !strcmp(t->op, op); }
static int ol_is_kw(ol_machine *m, int kw) { ol_tok *t = ol_peek(m); return t->type == T_KW && t->kw == kw; }
static int ol_new(ol_machine *m, int kind) {
  if (m->nnode >= OL_MAX_NODES) { ol_fail(m, "program too long"); return -1; }
  ol_node *n = &m->node[m->nnode]; memset(n, 0, sizeof *n); n->kind = kind; n->first = n->next = -1;
  return m->nnode++;
}
static void ol_add(ol_machine *m, int parent, int child) {
  if (m->node[parent].first < 0) { m->node[parent].first = child; return; }
  int c = m->node[parent].first; while (m->node[c].next >= 0) c = m->node[c].next; m->node[c].next = child;
}
static int ol_expect_op(ol_machine *m, const char *op) {
  if (!ol_is_op(m, op)) { char b[48]; snprintf(b, sizeof b, "syntax: expected '%s'", op); return ol_fail(m, b); }
  m->pos++; return 0;
}

static int ol_expr(ol_machine *m);
static int ol_atom(ol_machine *m) {
  ol_tok *t = ol_peek(m);
  if (t->type == T_INT) { int n = ol_new(m, N_INT); if (n < 0) return -1; m->node[n].ival = t->ival; m->pos++; return n; }
  if (t->type == T_STR) { int n = ol_new(m, N_STR); if (n < 0) return -1; m->node[n].sstart = t->start; m->node[n].slen = t->len; m->pos++; return n; }
  if (t->type == T_OP && !strcmp(t->op, "(")) { m->pos++; int e = ol_expr(m); if (e < 0) return -1; if (ol_expect_op(m, ")")) return -1; return e; }
  if (t->type == T_NAME) {
    m->pos++;
    if (ol_is_op(m, "(")) {
      int f = -1;
      for (int k = 0; k < F__N; k++) if ((int)strlen(OL_FN[k]) == t->len && !memcmp(OL_FN[k], m->src + t->start, t->len)) f = k;
      if (f < 0) { char b[64]; snprintf(b, sizeof b, "unknown function %.*s", t->len > 20 ? 20 : t->len, m->src + t->start); return ol_fail(m, b); }
      int n = ol_new(m, N_CALL); if (n < 0) return -1; m->node[n].op = f; m->pos++;
      int argc = 0;
      if (!ol_is_op(m, ")")) for (;;) {
        if (ol_peek(m)->type == T_NAME && m->tok[m->pos + 1].type == T_OP && !strcmp(m->tok[m->pos + 1].op, "="))
          return ol_fail(m, "calls are name(args)");
        int a = ol_expr(m); if (a < 0) return -1; ol_add(m, n, a); argc++;
        if (ol_is_op(m, ",")) { m->pos++; if (ol_is_op(m, ")")) break; continue; }
        break;
      }
      if (ol_expect_op(m, ")")) return -1;
      int want = OL_ARGS[f];
      if (f == F_RANGE) return ol_fail(m, "range only in a for loop");
      if ((want >= 0 && argc != want) || (want == -1 && argc < 1)) {
        char b[64]; snprintf(b, sizeof b, "%s takes %d arguments", OL_FN[f], want); return ol_fail(m, b);
      }
      m->node[n].ival = argc;
      return n;
    }
    int n = ol_new(m, N_NAME); if (n < 0) return -1; m->node[n].sstart = t->start; m->node[n].slen = t->len;
    if (t->len >= OL_NAME_LEN) return ol_fail(m, "name too long");
    return n;
  }
  if (t->type == T_OP && !strcmp(t->op, "-")) { m->pos++; int e = ol_atom(m); if (e < 0) return -1; int n = ol_new(m, N_NEG); if (n < 0) return -1; ol_add(m, n, e); return n; }
  return ol_fail(m, "syntax: expected a value");
}
static int ol_term(ol_machine *m) {
  int a = ol_atom(m); if (a < 0) return -1;
  while (ol_is_op(m, "*") || ol_is_op(m, "//") || ol_is_op(m, "%")) {
    char op = ol_peek(m)->op[0] == '/' ? '/' : ol_peek(m)->op[0]; m->pos++;
    int b = ol_atom(m); if (b < 0) return -1;
    int n = ol_new(m, N_BIN); if (n < 0) return -1; m->node[n].op = op; ol_add(m, n, a); ol_add(m, n, b); a = n;
  }
  return a;
}
static int ol_arith(ol_machine *m) {
  int a = ol_term(m); if (a < 0) return -1;
  while (ol_is_op(m, "+") || ol_is_op(m, "-")) {
    char op = ol_peek(m)->op[0]; m->pos++;
    int b = ol_term(m); if (b < 0) return -1;
    int n = ol_new(m, N_BIN); if (n < 0) return -1; m->node[n].op = op; ol_add(m, n, a); ol_add(m, n, b); a = n;
  }
  return a;
}
static int ol_cmpop(ol_machine *m) {
  static const char *c[] = {"==", "!=", "<", "<=", ">", ">="};
  for (int k = 0; k < 6; k++) if (ol_is_op(m, c[k])) return k + 1;
  return 0;
}
static int ol_compare(ol_machine *m) {
  int a = ol_arith(m); if (a < 0) return -1;
  if (!ol_cmpop(m)) return a;
  int n = ol_new(m, N_CMP); if (n < 0) return -1; ol_add(m, n, a);
  int ops = 0;
  while (ol_cmpop(m)) {   // chained: the ops are packed 3 bits each into ival
    int k = ol_cmpop(m); m->pos++;
    if (ops >= 8) return ol_fail(m, "comparison chain too long");
    m->node[n].ival |= k << (3 * ops++);
    int b = ol_arith(m); if (b < 0) return -1; ol_add(m, n, b);
  }
  return n;
}
static int ol_not(ol_machine *m) {
  if (ol_is_kw(m, K_NOT)) { m->pos++; int e = ol_not(m); if (e < 0) return -1; int n = ol_new(m, N_NOT); if (n < 0) return -1; ol_add(m, n, e); return n; }
  return ol_compare(m);
}
static int ol_and(ol_machine *m) {
  int a = ol_not(m); if (a < 0) return -1;
  if (!ol_is_kw(m, K_AND)) return a;
  int n = ol_new(m, N_AND); if (n < 0) return -1; ol_add(m, n, a);
  while (ol_is_kw(m, K_AND)) { m->pos++; int b = ol_not(m); if (b < 0) return -1; ol_add(m, n, b); }
  return n;
}
static int ol_expr(ol_machine *m) {
  if (++m->depth > OL_MAX_DEPTH * 4) return ol_fail(m, "nested too deep");
  int a = ol_and(m); if (a < 0) return -1;
  if (ol_is_kw(m, K_OR)) {
    int n = ol_new(m, N_OR); if (n < 0) return -1; ol_add(m, n, a);
    while (ol_is_kw(m, K_OR)) { m->pos++; int b = ol_and(m); if (b < 0) return -1; ol_add(m, n, b); }
    a = n;
  }
  m->depth--;
  return a;
}

static int ol_block(ol_machine *m);
static int ol_end_line(ol_machine *m) {
  if (ol_peek(m)->type != T_NEWLINE) return ol_fail(m, "syntax: expected end of line");
  m->pos++; return 0;
}
static int ol_stmt(ol_machine *m) {
  ol_tok *t = ol_peek(m);
  if (t->type == T_KW && (t->kw == K_IF || t->kw == K_WHILE)) {
    int kind = t->kw == K_IF ? N_IF : N_WHILE; m->pos++;
    int n = ol_new(m, kind); if (n < 0) return -1;
    int c = ol_expr(m); if (c < 0) return -1; ol_add(m, n, c);
    if (ol_expect_op(m, ":") || ol_end_line(m)) return -1;
    int b = ol_block(m); if (b < 0) return -1; ol_add(m, n, b);
    if (kind == N_IF) {
      if (ol_is_kw(m, K_ELIF)) {        // elif = else: if ...
        m->tok[m->pos].kw = K_IF;
        int blk = ol_new(m, N_BLOCK); if (blk < 0) return -1;
        int inner = ol_stmt(m); if (inner < 0) return -1; ol_add(m, blk, inner); ol_add(m, n, blk);
      } else if (ol_is_kw(m, K_ELSE)) {
        m->pos++; if (ol_expect_op(m, ":") || ol_end_line(m)) return -1;
        int e = ol_block(m); if (e < 0) return -1; ol_add(m, n, e);
      }
    } else if (ol_is_kw(m, K_ELSE)) return ol_fail(m, "not in the language: while-else");
    return n;
  }
  if (t->type == T_KW && t->kw == K_FOR) {
    m->pos++;
    ol_tok *v = ol_peek(m);
    if (v->type != T_NAME) return ol_fail(m, "for only as: for i in range(...)");
    m->pos++;
    if (!ol_is_kw(m, K_IN)) return ol_fail(m, "for only as: for i in range(...)");
    m->pos++;
    ol_tok *r = ol_peek(m);
    if (r->type != T_NAME || r->len != 5 || memcmp(m->src + r->start, "range", 5)) return ol_fail(m, "for only as: for i in range(...)");
    m->pos++;
    if (ol_expect_op(m, "(")) return -1;
    int n = ol_new(m, N_FOR); if (n < 0) return -1;
    m->node[n].sstart = v->start; m->node[n].slen = v->len;
    if (v->len >= OL_NAME_LEN) return ol_fail(m, "name too long");
    int argc = 0; int args = ol_new(m, N_BLOCK); if (args < 0) return -1;
    for (;;) { int a = ol_expr(m); if (a < 0) return -1; ol_add(m, args, a); argc++; if (ol_is_op(m, ",")) { m->pos++; continue; } break; }
    if (ol_expect_op(m, ")")) return -1;
    if (argc < 1 || argc > 3) return ol_fail(m, "range takes 1 to 3 numbers");
    m->node[n].ival = argc; ol_add(m, n, args);
    if (ol_expect_op(m, ":") || ol_end_line(m)) return -1;
    int b = ol_block(m); if (b < 0) return -1; ol_add(m, n, b);
    if (ol_is_kw(m, K_ELSE)) return ol_fail(m, "for only as: for i in range(...)");
    return n;
  }
  if (t->type == T_KW && (t->kw == K_BREAK || t->kw == K_CONTINUE || t->kw == K_PASS)) {
    int n = ol_new(m, t->kw == K_BREAK ? N_BREAK : t->kw == K_CONTINUE ? N_CONTINUE : N_PASS); if (n < 0) return -1;
    m->pos++; if (ol_end_line(m)) return -1; return n;
  }
  if (t->type == T_KW) return ol_fail(m, "syntax: unexpected keyword");
  // assignment or expression
  if (t->type == T_NAME && m->tok[m->pos + 1].type == T_OP) {
    const char *op = m->tok[m->pos + 1].op;
    if (!strcmp(op, "=") || !strcmp(op, "+=") || !strcmp(op, "-=") || !strcmp(op, "*=") || !strcmp(op, "//=") || !strcmp(op, "%=")) {
      int n = ol_new(m, op[0] == '=' ? N_ASSIGN : N_AUG); if (n < 0) return -1;
      m->node[n].sstart = t->start; m->node[n].slen = t->len; m->node[n].op = op[0];
      if (t->len >= OL_NAME_LEN) return ol_fail(m, "name too long");
      m->pos += 2;
      int e = ol_expr(m); if (e < 0) return -1; ol_add(m, n, e);
      if (ol_is_op(m, "=")) return ol_fail(m, "assign to names only");
      if (ol_end_line(m)) return -1;
      return n;
    }
  }
  int n = ol_new(m, N_EXPR); if (n < 0) return -1;
  int e = ol_expr(m); if (e < 0) return -1; ol_add(m, n, e);
  if (ol_is_op(m, "=")) return ol_fail(m, "assign to names only");
  if (ol_end_line(m)) return -1;
  return n;
}
static int ol_block(ol_machine *m) {
  if (ol_peek(m)->type != T_INDENT) return ol_fail(m, "syntax: expected an indented block");
  m->pos++;
  int b = ol_new(m, N_BLOCK); if (b < 0) return -1;
  while (ol_peek(m)->type != T_DEDENT && ol_peek(m)->type != T_EOF) { int s = ol_stmt(m); if (s < 0) return -1; ol_add(m, b, s); }
  if (ol_peek(m)->type == T_DEDENT) m->pos++;
  return b;
}

static int ol_parse(ol_machine *m, const char *src) {
  memset(m->err, 0, sizeof m->err);
  m->src = src; m->src_len = (int)strlen(src); m->nnode = 0; m->pos = 0; m->depth = 0;
  if (m->src_len > OL_MAX_SRC) return ol_fail(m, "program too long");
  if (ol_lex(m)) return -1;
  m->root = ol_new(m, N_BLOCK); if (m->root < 0) return -1;
  while (ol_peek(m)->type != T_EOF) {
    if (ol_peek(m)->type == T_INDENT) return ol_fail(m, "syntax: unexpected indent");
    int s = ol_stmt(m); if (s < 0) return -1; ol_add(m, m->root, s);
  }
  return 0;
}

// ---------------------------------------------------------------- runtime
static int ol_tick(ol_machine *m) {
  if (m->stop) return ol_fail(m, "interrupted");
  if (++m->steps > OL_MAX_STEPS) return ol_fail(m, "too many steps");
  return 0;
}
static ol_var *ol_lookup(ol_machine *m, uint16_t start, uint16_t len, int create) {
  for (int k = 0; k < OL_MAX_VARS; k++)
    if (m->var[k].used && (int)strlen(m->var[k].name) == len && !memcmp(m->var[k].name, m->src + start, len)) return &m->var[k];
  if (!create) return NULL;
  for (int k = 0; k < OL_MAX_VARS; k++)
    if (!m->var[k].used) { m->var[k].used = 1; memcpy(m->var[k].name, m->src + start, len); m->var[k].name[len] = 0; return &m->var[k]; }
  ol_fail(m, "too many variables"); return NULL;
}
static char *ol_talloc(ol_machine *m, int n) {
  if (m->temp_used + n + 1 > OL_TEMP_BYTES) { ol_fail(m, "string too long"); return NULL; }
  char *p = m->temp + m->temp_used; m->temp_used += n + 1; return p;
}
static int32_t ol_floordiv(int32_t a, int32_t b) { int32_t q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
static int32_t ol_mod(int32_t a, int32_t b) { int32_t r = a % b; if (r != 0 && ((r < 0) != (b < 0))) r += b; return r; }
static int ol_truthy(ol_val v) { return v.is_str ? v.n > 0 : v.i != 0; }

static int ol_eval(ol_machine *m, int id, ol_val *out, ol_io *io);
static int ol_cmp_vals(ol_machine *m, int op, ol_val a, ol_val b, int *res) {
  if (op <= 2) {   // == and != compare anything: different types are unequal
    int eq = a.is_str == b.is_str && (a.is_str ? (a.n == b.n && !memcmp(a.s, b.s, a.n)) : a.i == b.i);
    *res = op == 1 ? eq : !eq; return 0;
  }
  if (a.is_str != b.is_str) return ol_fail(m, "cannot compare a number and a string");
  int c;
  if (a.is_str) { int L = a.n < b.n ? a.n : b.n; c = memcmp(a.s, b.s, L); if (!c) c = (a.n > b.n) - (a.n < b.n); }
  else c = (a.i > b.i) - (a.i < b.i);
  *res = op == 3 ? c < 0 : op == 4 ? c <= 0 : op == 5 ? c > 0 : c >= 0;
  return 0;
}
static int ol_binop(ol_machine *m, char op, ol_val a, ol_val b, ol_val *out) {
  memset(out, 0, sizeof *out);
  if (op == '+') {
    if (a.is_str || b.is_str) {                       // text + anything is text, cut at HL_SAY characters
      char na[12], nb[12];
      if (!a.is_str) { a.n = snprintf(na, sizeof na, "%ld", (long)a.i); a.s = na; }
      if (!b.is_str) { b.n = snprintf(nb, sizeof nb, "%ld", (long)b.i); b.s = nb; }
      int L = a.n + b.n > HL_SAY ? HL_SAY : a.n + b.n;
      char *p = ol_talloc(m, L); if (!p) return -1;
      int la = a.n > L ? L : a.n;
      memcpy(p, a.s, la); memcpy(p + la, b.s, L - la); p[L] = 0;
      out->is_str = 1; out->s = p; out->n = L; return 0;
    }
    out->i = a.i + b.i; return 0;
  }
  if (a.is_str || b.is_str) return ol_fail(m, "numbers only, or text + text");
  if (0) {
    ol_val sv = a.is_str ? a : b; int32_t k = a.is_str ? b.i : a.i; if (k < 0) k = 0;
    if ((long)sv.n * k > 200) return ol_fail(m, "string too long");
    if ((long)sv.n * k >= OL_STR_LEN) return ol_fail(m, "string too long");
    char *p = ol_talloc(m, sv.n * k); if (!p) return -1;
    for (int r = 0; r < k; r++) memcpy(p + r * sv.n, sv.s, sv.n);
    p[sv.n * k] = 0; out->is_str = 1; out->s = p; out->n = sv.n * k; return 0;
  }
  if (a.is_str || b.is_str) return ol_fail(m, "unsupported operand types");
  if (op == '-') out->i = a.i - b.i;
  else if (op == '*') out->i = a.i * b.i;
  else if (op == '/') { if (!b.i) return ol_fail(m, "division by zero"); out->i = ol_floordiv(a.i, b.i); }
  else if (op == '%') { if (!b.i) return ol_fail(m, "division by zero"); out->i = ol_mod(a.i, b.i); }
  return 0;
}
static int ol_eval(ol_machine *m, int id, ol_val *out, ol_io *io) {
  if (ol_tick(m)) return -1;
  ol_node *n = &m->node[id];
  memset(out, 0, sizeof *out);
  switch (n->kind) {
  case N_INT: out->i = n->ival; return 0;
  case N_STR: out->is_str = 1; out->s = m->src + n->sstart; out->n = n->slen; return 0;
  case N_NAME: {
    ol_var *v = ol_lookup(m, n->sstart, n->slen, 0);
    if (!v) { char b[48]; snprintf(b, sizeof b, "unknown name %.*s", n->slen, m->src + n->sstart); return ol_fail(m, b); }
    out->is_str = v->is_str; out->i = v->i; out->s = v->s; out->n = v->n; return 0;
  }
  case N_NEG: { ol_val a; if (ol_eval(m, n->first, &a, io)) return -1; if (a.is_str) return ol_fail(m, "bad operand for unary -"); out->i = -a.i; return 0; }
  case N_NOT: { ol_val a; if (ol_eval(m, n->first, &a, io)) return -1; out->i = !ol_truthy(a); return 0; }
  case N_AND: case N_OR: {
    for (int c = n->first; c >= 0; c = m->node[c].next) {
      if (ol_eval(m, c, out, io)) return -1;
      if (n->kind == N_AND ? !ol_truthy(*out) : ol_truthy(*out)) return 0;
    }
    return 0;
  }
  case N_BIN: {
    ol_val a, b; if (ol_eval(m, n->first, &a, io) || ol_eval(m, m->node[n->first].next, &b, io)) return -1;
    return ol_binop(m, n->op, a, b, out);
  }
  case N_CMP: {
    int c = n->first; ol_val left; if (ol_eval(m, c, &left, io)) return -1;
    int k = 0;
    for (c = m->node[c].next; c >= 0; c = m->node[c].next, k++) {
      ol_val right; if (ol_eval(m, c, &right, io)) return -1;
      int res; if (ol_cmp_vals(m, (n->ival >> (3 * k)) & 7, left, right, &res)) return -1;
      if (!res) { out->i = 0; return 0; }
      left = right;
    }
    out->i = 1; return 0;
  }
  case N_CALL: {
    ol_val a[8]; int argc = 0;
    for (int c = n->first; c >= 0; c = m->node[c].next) { if (argc >= 8) return ol_fail(m, "too many arguments"); if (ol_eval(m, c, &a[argc++], io)) return -1; }
    int f = n->op;
    if (f == F_STR) {
      if (a[0].is_str) { *out = a[0]; return 0; }
      char *p = ol_talloc(m, 12); if (!p) return -1;
      int L = snprintf(p, 12, "%ld", (long)a[0].i); out->is_str = 1; out->s = p; out->n = L; return 0;
    }
    if (f == F_ABS) { if (a[0].is_str) return ol_fail(m, "bad operand type for abs()"); out->i = a[0].i < 0 ? -a[0].i : a[0].i; return 0; }
    if (f == F_MIN || f == F_MAX) {
      if (argc < 2) return ol_fail(m, "min and max take two or more numbers");   // one int arg is not iterable in Python either
      *out = a[0];
      for (int k = 1; k < argc; k++) {
        int res; if (ol_cmp_vals(m, f == F_MIN ? 3 : 5, a[k], *out, &res)) return -1;
        if (res) *out = a[k];
      }
      return 0;
    }
    char err[HL_ERR] = {0}, w[HL_SAY + 2];
    #define HL_TEXT(k) do { if (!a[k].is_str) snprintf(w, sizeof w, "%ld", (long)a[k].i); else { int L = a[k].n > HL_SAY ? HL_SAY : a[k].n; memcpy(w, a[k].s, L); w[L] = 0; } } while (0)
    #define HL_WHERE(k) do { int sp = 1; for (int q = 0; a[k].is_str && q < a[k].n; q++) if (a[k].s[q] != ' ') sp = 0; \
                             if (!a[k].is_str || sp || a[k].n > HL_SAY) return ol_fail(m, "where must be a few words in quotes"); \
                             memcpy(w, a[k].s, a[k].n); w[a[k].n] = 0; } while (0)
    switch (f) {
    case F_ON: case F_OFF: HL_WHERE(0); if (io->sw(io->ctx, w, f == F_ON, err)) return ol_fail(m, err); break;
    case F_SET: if (a[1].is_str) return ol_fail(m, "set() takes a number"); HL_WHERE(0); if (io->set(io->ctx, w, a[1].i, err)) return ol_fail(m, err); break;
    case F_IS_ON: { int v = 0; HL_WHERE(0); if (io->is_on(io->ctx, w, &v, err)) return ol_fail(m, err); out->i = v; break; }
    case F_TEMP: { int v = 0; if (!a[0].is_str) return ol_fail(m, "where must be a few words in quotes"); HL_TEXT(0); if (io->temp(io->ctx, w, &v, err)) return ol_fail(m, err); out->i = v; break; }
    case F_WAIT: {
      if (a[0].is_str || a[0].i < 0) return ol_fail(m, "wait() takes milliseconds");
      m->waited += a[0].i;
      if (m->waited > OL_MAX_WAIT_MS) return ol_fail(m, "waited too long");
      io->wait(io->ctx, a[0].i);
      break;
    }
    case F_SAY: HL_TEXT(0); io->say(io->ctx, w); break;
    case F_CLEAR: io->clear(io->ctx); break;
    case F_BIG: case F_LINE: case F_SMALL: HL_TEXT(0); if (io->row(io->ctx, f - F_BIG, w, err)) return ol_fail(m, err); break;
    case F_TIME: case F_DATE: {
      char *p = ol_talloc(m, HL_SAY); if (!p) return -1;
      io->now(io->ctx, f == F_DATE, p, HL_SAY + 1); out->is_str = 1; out->s = p; out->n = (uint16_t)strlen(p); break;
    }
    case F_WEATHER: case F_PRICE: case F_TOMORROW: case F_SUNRISE: case F_SUNSET: case F_AIR: case F_UNTIL: case F_NEWS: case F_NAMEDAY: {
      w[0] = 0;
      if (argc) {
        int sp = 1; for (int q = 0; a[0].is_str && q < a[0].n; q++) if (a[0].s[q] != ' ') sp = 0;
        if (!a[0].is_str || sp) { char b2[48]; snprintf(b2, sizeof b2, "%s() of what?", OL_FN[f]); return ol_fail(m, b2); }
        HL_TEXT(0);
      }
      char *p = ol_talloc(m, HL_SAY); if (!p) return -1;
      if (io->look(io->ctx, f - F_WEATHER, w, p, HL_SAY + 1, err)) return ol_fail(m, err);
      out->is_str = 1; out->s = p; out->n = (uint16_t)strlen(p); break;
    }
    }
    #undef HL_TEXT
    #undef HL_WHERE
    return 0;
  }
  }
  return ol_fail(m, "cannot evaluate");
}

static int ol_exec(ol_machine *m, int id, ol_io *io);
static int ol_exec_block(ol_machine *m, int id, ol_io *io) {
  for (int c = m->node[id].first; c >= 0; c = m->node[c].next) {
    if (ol_exec(m, c, io)) return -1;
    if (m->brk || m->cont) return 0;
  }
  return 0;
}
static int ol_set(ol_machine *m, uint16_t start, uint16_t len, ol_val v) {
  ol_var *var = ol_lookup(m, start, len, 1); if (!var) return -1;
  var->is_str = v.is_str; var->i = v.i;
  if (v.is_str) { memmove(var->s, v.s, v.n); var->s[v.n] = 0; var->n = v.n; } else var->n = 0;
  return 0;
}
static int ol_exec(ol_machine *m, int id, ol_io *io) {
  if (ol_tick(m)) return -1;
  ol_node *n = &m->node[id];
  m->temp_used = 0;   // temporaries live for one statement; variables own copies
  switch (n->kind) {
  case N_EXPR: { ol_val v; return ol_eval(m, n->first, &v, io); }
  case N_ASSIGN: { ol_val v; if (ol_eval(m, n->first, &v, io)) return -1; return ol_set(m, n->sstart, n->slen, v); }
  case N_AUG: {
    ol_var *var = ol_lookup(m, n->sstart, n->slen, 0);
    if (!var) { char b[48]; snprintf(b, sizeof b, "unknown name %.*s", n->slen, m->src + n->sstart); return ol_fail(m, b); }
    ol_val cur = {var->is_str, var->i, var->s, var->n}, rhs, res;
    if (ol_tick(m) || ol_tick(m) || ol_eval(m, n->first, &rhs, io)) return -1;   // as lang.py: the op and the name
    char op = n->op; if (op == '/') op = '/';
    if (ol_binop(m, op, cur, rhs, &res)) return -1;
    return ol_set(m, n->sstart, n->slen, res);
  }
  case N_IF: {
    int cond = n->first, body = m->node[cond].next, other = m->node[body].next;
    ol_val c; if (ol_eval(m, cond, &c, io)) return -1;
    if (ol_truthy(c)) return ol_exec_block(m, body, io);
    if (other >= 0) return ol_exec_block(m, other, io);
    return 0;
  }
  case N_WHILE: {
    int cond = n->first, body = m->node[cond].next;
    for (;;) {
      ol_val c; m->temp_used = 0; if (ol_eval(m, cond, &c, io)) return -1;
      if (!ol_truthy(c)) return 0;
      if (ol_exec_block(m, body, io)) return -1;
      if (m->brk) { m->brk = 0; return 0; }
      m->cont = 0;
    }
  }
  case N_FOR: {
    int args = n->first, body = m->node[args].next;
    int32_t r[3] = {0, 0, 1}; int k = 0;
    for (int c = m->node[args].first; c >= 0; c = m->node[c].next, k++) {
      ol_val v; if (ol_eval(m, c, &v, io)) return -1;
      if (v.is_str) return ol_fail(m, "range takes whole numbers");
      r[k] = v.i;
    }
    int32_t start = 0, stop, step = 1;
    if (n->ival == 1) stop = r[0]; else { start = r[0]; stop = r[1]; if (n->ival == 3) step = r[2]; }
    if (step == 0) return ol_fail(m, "range() arg 3 must not be zero");
    for (int32_t i = start; step > 0 ? i < stop : i > stop; i += step) {
      ol_val iv = {0, i, 0, 0};
      if (ol_set(m, n->sstart, n->slen, iv)) return -1;
      if (ol_exec_block(m, body, io)) return -1;
      if (m->brk) { m->brk = 0; return 0; }
      m->cont = 0;
    }
    return 0;
  }
  case N_BREAK: m->brk = 1; return 0;
  case N_CONTINUE: m->cont = 1; return 0;
  case N_PASS: return 0;
  case N_BLOCK: return ol_exec_block(m, id, io);
  }
  return ol_fail(m, "cannot run");
}

// break/continue must sit inside a loop: checked once before running, as Python checks at compile time
static int ol_check_loops(ol_machine *m, int id, int in_loop) {
  ol_node *n = &m->node[id];
  if ((n->kind == N_BREAK || n->kind == N_CONTINUE) && !in_loop) return ol_fail(m, "break outside a loop");
  int loop = in_loop || n->kind == N_FOR || n->kind == N_WHILE;
  for (int c = n->first; c >= 0; c = m->node[c].next) if (ol_check_loops(m, c, loop)) return -1;
  return 0;
}

static int ol_run(ol_machine *m, ol_io *io) {
  memset(m->var, 0, sizeof m->var);
  m->steps = 0; m->waited = 0; m->brk = m->cont = 0; m->stop = 0;
  if (ol_check_loops(m, m->root, 0)) return -1;
  int rc = ol_exec_block(m, m->root, io);
  if (io->done) io->done(io->ctx);
  return rc;
}

#endif
