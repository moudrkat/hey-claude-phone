// The local model for LocalModel.kt: a sentence in, a program out, and the program run by an interpreter that
// decides what it may do. Here that is drawing the home screen: rows of text, with live values left as markers
// that the page fills in (LocalScreen.kt), so nothing is fetched from C.
//
//   \x01t\x02 the time    \x01d\x02 the date    \x01w|Prague\x02 weather there    \x01p|bitcoin\x02 its price
//   \x01f|Prague\x02 tomorrow there    \x01r|..\x02 sunrise    \x01s|..\x02 sunset    \x01a|..\x02 the air
//   \x01u|christmas\x02 days until    \x01n\x02 a headline    \x01j\x02 the name day
//
// tiny/ is the tiny-house runtime (github.com/moudrkat): llm.h and bpe_tokenizer.h in it are Viacheslav Sierbov's
// slvDev/esp32-ai (MIT), the rest is the home language and its interpreter.
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include "tiny/tiny.h"
#include "tiny/home.h"

static tiny T;
static uint8_t *BLOB;
static int LOADED;

// what price() may be asked for; the page knows where each one lives
static const char *PRICED[] = {"bitcoin", "btc", "ethereum", "eth", "solana", "dogecoin", "euro", "dollar", "pound", 0};

static void now(void *self, int date, char *out, int n) { (void)self; snprintf(out, n, "\x01%c\x02", date ? 'd' : 't'); }
static int look(void *self, int which, const char *what, char *out, int n) {
  (void)self;
  static const char MARK[HL_SOURCES] = {'w', 'p', 'f', 'r', 's', 'a', 'u', 'n', 'j'};
  if (which == HL_NEWS || which == HL_NAMEDAY) { snprintf(out, n, "\x01%c\x02", MARK[which]); return 0; }
  char low[HL_SAY + 2]; int L = 0;
  for (const char *p = what; *p && L < HL_SAY; p++) low[L++] = *p >= 'A' && *p <= 'Z' ? *p + 32 : *p;
  low[L] = 0;
  if (which == HL_PRICE) {
    for (int i = 0; PRICED[i]; i++) if (strstr(low, PRICED[i])) { snprintf(out, n, "\x01p|%s\x02", PRICED[i]); return 0; }
    return -1;
  }
  for (const char *p = what; *p; p++) if (*p == '|' || *p == 1 || *p == 2) return -1;
  snprintf(out, n, "\x01%c|%.40s\x02", MARK[which], which == HL_UNTIL ? low : what);   // a place or a day: the page looks it up
  return 0;
}

static char SAID[HL_SAY + 1];
static void said(void *self, const char *text) { (void)self; snprintf(SAID, sizeof SAID, "%s", text); }

// s as a JSON string body
static int esc(char *out, int n, const char *s) {
  int k = 0;
  for (; *s && k < n - 8; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { out[k++] = '\\'; out[k++] = c; }
    else if (c < 32) k += snprintf(out + k, 8, "\\u%04x", c);
    else out[k++] = c;
  }
  out[k] = 0;
  return k;
}

JNIEXPORT jboolean JNICALL Java_cz_magiak_asistent_local_LocalModel_nativeLoad(JNIEnv *env, jclass cls, jbyteArray model) {
  (void)cls;
  if (LOADED) return JNI_TRUE;
  jsize n = (*env)->GetArrayLength(env, model);
  BLOB = (uint8_t *)malloc(n);
  if (!BLOB) return JNI_FALSE;
  (*env)->GetByteArrayRegion(env, model, 0, n, (jbyte *)BLOB);
  LOADED = tiny_load(&T, BLOB) == 0;
  return LOADED ? JNI_TRUE : JNI_FALSE;
}

// The program the model writes for a sentence, or "" if the sentence does not fit (ASCII, a few dozen words).
JNIEXPORT jstring JNICALL Java_cz_magiak_asistent_local_LocalModel_nativeWrite(JNIEnv *env, jclass cls, jstring sentence) {
  (void)cls;
  static char prog[TINY_MAX_PROG];
  prog[0] = 0;
  if (LOADED) {
    const char *s = (*env)->GetStringUTFChars(env, sentence, 0);
    if (tiny_write(&T, s, prog, sizeof prog) < 0) prog[0] = 0;
    (*env)->ReleaseStringUTFChars(env, sentence, s);
  }
  return (*env)->NewStringUTF(env, prog);
}

// Runs a program. {"ok":true,"screen":true,"rows":[{"size":0,"text":"..."}],"said":"..."} or {"ok":false,"error":"..."}.
JNIEXPORT jstring JNICALL Java_cz_magiak_asistent_local_LocalModel_nativeRun(JNIEnv *env, jclass cls, jstring program) {
  (void)cls;
  static hm_home H; static ol_machine M; static char out[4096], buf[512];
  memset(&H, 0, sizeof H);
  H.now = now; H.look = look; H.said = said; SAID[0] = 0;
  const char *src = (*env)->GetStringUTFChars(env, program, 0);
  hl_io io = hm_io(&H);
  int rc = ol_parse(&M, src);
  if (rc == 0) rc = ol_run(&M, &io);
  int k = 0;
  if (rc != 0) {
    esc(buf, sizeof buf, M.err);
    k = snprintf(out, sizeof out, "{\"ok\":false,\"error\":\"%s\"}", buf);
  } else {
    k = snprintf(out, sizeof out, "{\"ok\":true,\"screen\":%s,\"rows\":[", H.has_screen ? "true" : "false");
    for (int i = 0; i < H.nrow; i++) {
      esc(buf, sizeof buf, H.row[i]);
      k += snprintf(out + k, sizeof out - k, "%s{\"size\":%d,\"text\":\"%s\"}", i ? "," : "", H.row_size[i], buf);
    }
    esc(buf, sizeof buf, SAID);
    k += snprintf(out + k, sizeof out - k, "],\"said\":\"%s\"}", buf);
  }
  (*env)->ReleaseStringUTFChars(env, program, src);
  return (*env)->NewStringUTF(env, out);
}
