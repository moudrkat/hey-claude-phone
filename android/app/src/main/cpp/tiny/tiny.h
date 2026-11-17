// tiny.h -- the whole way from a sentence to a program, in C: the tokenizer, the model, greedy decoding.
// The model file is model/model.bin (4-bit weights, 2.1 MB); the caller reads it into memory and keeps it there.
// Shared by the host test (host/tiny.c) and a device (Android: android/tinyhouse.c).
//
//   tiny t; if (tiny_load(&t, blob)) -> not a model file
//   char prog[TINY_MAX_PROG]; int n = tiny_write(&t, "show the clock", prog, sizeof prog);
//
// llm.h and bpe_tokenizer.h are the esp32-ai runtime (Viacheslav Sierbov's slvDev/esp32-ai, MIT; see THIRD_PARTY.md).
#ifndef TINY_H
#define TINY_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef PROGMEM
#define PROGMEM
#endif
#include "llm.h"
#include "bpe_tokenizer.h"
#include "generated/vocab.h"
#include "generated/tokenizer_encoder.h"

#define TINY_MAX_PROG 1024

typedef struct { Model m; Scratch s; BpeTokenizer tok; int eot; } tiny;

static int tiny_load(tiny *t, const uint8_t *blob) {
  memset(t, 0, sizeof *t);
  if (llm_load(blob, &t->m)) return -1;
  int D = t->m.c.dim, L = t->m.c.n_layers, P = t->m.c.ple_dim, F = t->m.c.ffn, V = t->m.out_vocab, S = t->m.c.seq_len;
  Scratch *s = &t->s;
  s->x = (float *)malloc(D * 4); s->h = (float *)malloc((F > D ? F : D) * 4); s->qkv = (float *)malloc(3 * D * 4); s->att = (float *)malloc(D * 4);
  s->g1 = (float *)malloc(F * 4); s->g2 = (float *)malloc((P > F ? P : F) * 4);
  s->ple = (float *)malloc(L * P * 4); s->tmpP = (float *)malloc(L * P * 4); s->trow = (float *)malloc(L * P * 4);
  s->logits = (float *)malloc(V * 4); s->scores = (float *)malloc(S * 4);
  s->kcache = (float *)malloc((size_t)L * S * D * 4); s->vcache = (float *)malloc((size_t)L * S * D * 4);
  if (!s->x || !s->h || !s->qkv || !s->att || !s->g1 || !s->g2 || !s->ple || !s->tmpP || !s->trow || !s->logits || !s->scores || !s->kcache || !s->vcache) return -1;
  if (bpe_tokenizer_load(TOKENIZER_ENCODER_ASSET, TOKENIZER_ENCODER_ASSET_SIZE, &t->tok)) return -1;
  t->eot = -1;
  for (int v = 0; v < VOCAB_N; v++)
    if (VOCAB_OFF[v + 1] - VOCAB_OFF[v] == 13 && !memcmp(VOCAB_BLOB + VOCAB_OFF[v], "<|endoftext|>", 13)) t->eot = v;
  return VOCAB_N == t->m.out_vocab ? 0 : -1;
}

// The program for a sentence, greedy, as "# <sentence>\n" is how every training document begins.
// Returns its length, or -1 if the sentence does not fit (ASCII only, a few dozen words).
static int tiny_write(tiny *t, const char *sentence, char *out, int n) {
  char wrapped[400];
  if (snprintf(wrapped, sizeof wrapped, "# %s\n", sentence) >= (int)sizeof wrapped) return -1;
  uint16_t enc[160];
  int np = bpe_encode_ascii(&t->tok, wrapped, enc, 160), S = t->m.c.seq_len, V = t->m.out_vocab, pos = 0, len = 0;
  if (np <= 0 || np >= S - 8) return -1;
  for (int i = 0; i < np; i++) llm_forward(&t->m, enc[i], pos++, &t->s);
  while (pos < S) {
    int best = 0;
    for (int v = 1; v < V; v++) if (t->s.logits[v] > t->s.logits[best]) best = v;
    if (best == t->eot) break;
    int L = VOCAB_OFF[best + 1] - VOCAB_OFF[best];
    if (len + L >= n - 1) break;
    memcpy(out + len, VOCAB_BLOB + VOCAB_OFF[best], L); len += L;
    llm_forward(&t->m, best, pos++, &t->s);
  }
  out[len] = 0;
  return len;
}

#endif  // TINY_H
