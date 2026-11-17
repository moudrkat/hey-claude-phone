// home.h -- a home held in memory, for the interpreter's callbacks: its devices, which of them a phrase means
// (the same word matcher as house.py's match_words), and the rules a program cannot bend. Plain C: the host
// test and a device share it. A device adds what a switch really does (hm_apply) and where its sources come from.
#ifndef HOME_H
#define HOME_H

#include <stdio.h>
#include <string.h>
#include "house_lang.h"

#define HM_MAX_DEV 24
#define HM_MAX_ROOMS 12
#define HM_MAX_ROWS 8
enum { HM_LIGHT, HM_HEATER, HM_BLIND, HM_PLUG, HM_LOCK, HM_ALARM, HM_KINDS };
static const char *HM_KIND[HM_KINDS] = {"light", "heater", "blind", "plug", "lock", "alarm"};
static const int HM_LO[HM_KINDS] = {0, 5, 0, 0, 0, 0}, HM_HI[HM_KINDS] = {100, 30, 100, 0, 0, 0};
#define HM_HAS_LEVEL(k) ((k) <= HM_BLIND)
#define HM_HANDS_OFF(k) ((k) == HM_LOCK || (k) == HM_ALARM)

typedef struct { char id[24], name[40], room[24]; int kind, on, level; } hm_dev;
typedef struct { char room[24]; int deg; } hm_room;
typedef struct {
  hm_dev dev[HM_MAX_DEV]; int ndev;
  hm_room room[HM_MAX_ROOMS]; int nroom;
  long t;                                   // the program's own clock, ms
  int has_screen, nrow; int row_size[HM_MAX_ROWS]; char row[HM_MAX_ROWS][HL_SAY + 1];
  void (*apply)(void *self, int dev, int what, int value);            // what: 0 on, 1 level. NULL: memory only
  void (*said)(void *self, const char *text);
  void (*now)(void *self, int date, char *out, int n);
  int (*look)(void *self, int price, const char *what, char *out, int n);   // 0 found, -1 not in the catalogue
  void (*pause)(void *self, long ms);
} hm_home;

// ---------------------------------------------------------------- words -> devices (house.py: match_words)
static const char *HM_KIND_WORDS[HM_KINDS][5] = {{"light", "lights", "lamp", "lamps", 0}, {"heating", "heater", "radiator", "heat", 0},
                                                  {"blind", "blinds", "shade", "shades", 0}, {"plug", "socket", 0}, {"lock", 0}, {"alarm", 0}};
static const char *HM_ALL[] = {"all", "every", "everything", "everywhere", 0};
static const char *HM_SKIP[] = {"the", "a", "an", "in", "on", "at", "of", "by", "to", "my", "our", "room", 0};

static int hm_in(const char *w, const char **list) { for (int i = 0; list[i]; i++) if (!strcmp(w, list[i])) return 1; return 0; }
// lower-case words of s, commas as spaces; returns how many (at most max)
static int hm_words(const char *s, char out[][24], int max) {
  int n = 0, k = 0;
  for (;; s++) {
    char c = *s;
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c == 0 || c == ' ' || c == ',' || c == '\t' || c == '\n') {
      if (k) { out[n][k] = 0; n++; k = 0; if (n >= max) return n; }
      if (!*s) return n;
    } else if (k < 23) out[n][k++] = c;
  }
}
// ids of the devices the phrase means, into hits; want: a bit per kind, 0 = any. Returns the count.
static int hm_match(hm_home *h, const char *phrase, unsigned want, int *hits) {
  char w[16][24]; int nw = hm_words(phrase, w, 16);
  unsigned kinds = 0; int everything = 0; char key[16][24]; int nkey = 0;
  for (int i = 0; i < nw; i++) {
    int is_kind = 0;
    for (int k = 0; k < HM_KINDS; k++) if (hm_in(w[i], HM_KIND_WORDS[k])) { kinds |= 1u << k; is_kind = 1; }
    if (hm_in(w[i], HM_ALL)) { everything = 1; continue; }
    if (!is_kind && !hm_in(w[i], HM_SKIP)) strcpy(key[nkey++], w[i]);
  }
  int best = 0, n = 0;
  for (int d = 0; d < h->ndev; d++) {
    if (want && !(want >> h->dev[d].kind & 1)) continue;
    if (kinds && !(kinds >> h->dev[d].kind & 1)) continue;
    char both[80]; snprintf(both, sizeof both, "%s %s", h->dev[d].name, h->dev[d].room);
    char own[16][24]; int no = hm_words(both, own, 16), score = 0;
    for (int i = 0; i < nkey; i++) {                       // each word of the phrase counts once per time it is said
      int found = 0;
      for (int j = 0; j < no; j++) if (!hm_in(own[j], HM_SKIP) && !strcmp(key[i], own[j])) found = 1;
      score += found;
    }
    if (nkey && score == 0) continue;
    if (score > best) { best = score; n = 0; hits[n++] = d; }
    else if (score == best) hits[n++] = d;
  }
  if (!nkey && !everything && !kinds) return 0;
  if (!kinds && !everything && n > 1) {                    // "the kitchen": its lights, unless told otherwise
    int m = 0, lights[HM_MAX_DEV];
    for (int i = 0; i < n; i++) if (h->dev[hits[i]].kind == HM_LIGHT) lights[m++] = hits[i];
    if (m) { memcpy(hits, lights, m * sizeof *hits); n = m; }
  }
  return n;
}

// ---------------------------------------------------------------- the callbacks the interpreter calls
static void hm_put(hm_home *h, int d, int what, int value) {
  int *slot = what ? &h->dev[d].level : &h->dev[d].on;
  if (*slot == value) return;
  *slot = value;
  if (h->apply) h->apply(h, d, what, value);
}
// the devices a program may touch among those the phrase means; -1 with the reason if there is none
static int hm_find(hm_home *h, const char *where, unsigned want, int *mine, char *err) {
  int hits[HM_MAX_DEV], n = hm_match(h, where, want, hits), m = 0;
  if (!n) { snprintf(err, HL_ERR, "I don't know \"%s\"", where); return -1; }
  for (int i = 0; i < n; i++) if (!HM_HANDS_OFF(h->dev[hits[i]].kind)) mine[m++] = hits[i];
  if (!m) { snprintf(err, HL_ERR, "%s: not mine to touch", h->dev[hits[0]].name); return -1; }
  return m;
}
static int hm_sw(void *ctx, const char *where, int on, char *err) {
  hm_home *h = (hm_home *)ctx; int mine[HM_MAX_DEV], n = hm_find(h, where, 0, mine, err);
  if (n < 0) return -1;
  for (int i = 0; i < n; i++) hm_put(h, mine[i], 0, on);
  return 0;
}
static int hm_set(void *ctx, const char *where, int v, char *err) {
  hm_home *h = (hm_home *)ctx; int mine[HM_MAX_DEV];
  int n = hm_find(h, where, 1u << HM_LIGHT | 1u << HM_HEATER | 1u << HM_BLIND, mine, err);
  if (n < 0) return -1;
  for (int i = 0; i < n; i++) {
    int k = h->dev[mine[i]].kind, x = v < HM_LO[k] ? HM_LO[k] : v > HM_HI[k] ? HM_HI[k] : v;      // out of range is clipped
    hm_put(h, mine[i], 1, x);
    hm_put(h, mine[i], 0, x > HM_LO[k] || k == HM_HEATER);
  }
  return 0;
}
static int hm_is_on(void *ctx, const char *where, int *out, char *err) {
  hm_home *h = (hm_home *)ctx; int mine[HM_MAX_DEV], n = hm_find(h, where, 0, mine, err);
  if (n < 0) return -1;
  *out = 0;
  for (int i = 0; i < n; i++) if (h->dev[mine[i]].on) *out = 1;
  return 0;
}
static int hm_temp(void *ctx, const char *where, int *out, char *err) {
  hm_home *h = (hm_home *)ctx; int hits[HM_MAX_DEV], n = hm_match(h, where, 0, hits);
  const char *room = 0;
  for (int i = 0; i < n; i++) if (!room || strcmp(h->dev[hits[i]].room, room) < 0) room = h->dev[hits[i]].room;   // the first by name
  if (!room) {                                               // no device there: a room named in the phrase
    char low[HL_SAY + 2]; int L = 0;
    for (const char *p = where; *p && L < HL_SAY; p++) low[L++] = *p >= 'A' && *p <= 'Z' ? *p + 32 : *p;
    low[L] = 0;
    for (int r = 0; r < h->nroom; r++) if (strstr(low, h->room[r].room) && (!room || strcmp(h->room[r].room, room) < 0)) room = h->room[r].room;
  }
  if (!room) { snprintf(err, HL_ERR, "I don't know \"%s\"", where); return -1; }
  for (int r = 0; r < h->nroom; r++) if (!strcmp(h->room[r].room, room)) { *out = h->room[r].deg; return 0; }
  snprintf(err, HL_ERR, "I don't know \"%s\"", where); return -1;
}
static void hm_wait(void *ctx, long ms) { hm_home *h = (hm_home *)ctx; h->t += ms; if (h->pause) h->pause(h, ms); }
static void hm_say(void *ctx, const char *text) { hm_home *h = (hm_home *)ctx; if (h->said) h->said(h, text); }
static void hm_clear(void *ctx) { hm_home *h = (hm_home *)ctx; h->has_screen = 1; h->nrow = 0; }
static int hm_row(void *ctx, int size, const char *text, char *err) {
  hm_home *h = (hm_home *)ctx;
  if (!h->has_screen) { h->has_screen = 1; h->nrow = 0; }
  if (h->nrow >= HM_MAX_ROWS) { snprintf(err, HL_ERR, "the screen is full"); return -1; }
  h->row_size[h->nrow] = size; snprintf(h->row[h->nrow], HL_SAY + 1, "%s", text); h->nrow++;
  return 0;
}
static void hm_now(void *ctx, int date, char *out, int n) { hm_home *h = (hm_home *)ctx; if (h->now) h->now(h, date, out, n); else snprintf(out, n, "%s", date ? "Tue 17 Nov" : "14:05"); }
static int hm_look(void *ctx, int price, const char *what, char *out, int n, char *err) {
  hm_home *h = (hm_home *)ctx;
  if (h->look && h->look(h, price, what, out, n) == 0) return 0;
  snprintf(err, HL_ERR, "I don't know the %s of \"%s\"", price ? "price" : "weather", what); return -1;
}
static hl_io hm_io(hm_home *h) {
  hl_io io = {h, hm_sw, hm_set, hm_is_on, hm_temp, hm_wait, hm_say, hm_clear, hm_row, hm_now, hm_look, 0};
  return io;
}

#endif  // HOME_H
