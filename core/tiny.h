/* tiny.h - TINY 2 transformer core: portable C99, no heap, no stdio.

   The network reads and writes one letter at a time. Weights are used in
   place from the TINY.BIN image (which may live in flash / ROM); the caller
   provides the RAM for the letter memory (key/value cache).

   Typical use:

       tiny_model m;  tiny_ctx c;
       tiny_model_init(&m, blob, blob_len);
       mem = <tiny_ctx_mem_size(&m) bytes, any alignment>;
       tiny_ctx_init(&c, &m, mem, size);
       tiny_reset(&c);
       for (each letter ch of the question) tiny_feed(&c, ch);
       tiny_end_question(&c);
       while ((ch = tiny_next(&c)) > 0) emit(ch);

   All functions are reentrant: all state lives in tiny_model / tiny_ctx.
   One tiny_model can be shared (read only) by several tiny_ctx. */

#ifndef TINY_H
#define TINY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TINY_VERSION_MAJOR 2
#define TINY_VERSION_MINOR 1

#define TINY_VOCAB  97    /* 0 pad, 1 newline, 2.. the letters ' '..'~' */
#define TINY_CTX    128   /* longest question + answer, in letters      */
#define TINY_MAXQ   72    /* longest question, in letters               */

/* Compile-time limits; override with -D to shrink RAM on small targets. */
#ifndef TINY_MAXD
#define TINY_MAXD   128
#endif
#ifndef TINY_MAXHID
#define TINY_MAXHID 512
#endif
#ifndef TINY_MAXL
#define TINY_MAXL   8
#endif

/* Error codes (all negative). */
#define TINY_OK          0
#define TINY_ERR_ARG    -1   /* bad argument                          */
#define TINY_ERR_FORMAT -2   /* not a "TNY2" image                    */
#define TINY_ERR_SIZE   -3   /* network size beyond compile limits    */
#define TINY_ERR_LENGTH -4   /* image too short or too long           */
#define TINY_ERR_MEM    -5   /* caller memory too small               */
#define TINY_ERR_FULL   -6   /* question too long / context full      */
#define TINY_ERR_STATE  -7   /* call out of order                     */

/* A table of 8-bit weights: each row is a float32 scale then `cols` bytes.
   Points into the model image; nothing is copied. */
typedef struct {
    const uint8_t *data;
    int rows, cols;
} tiny_table;

typedef struct {
    int d, layers, heads, head_dim, hidden;
    float att_div;                                   /* sqrt(head_dim) */
    tiny_table emb, pos;
    tiny_table wq[TINY_MAXL], wk[TINY_MAXL], wv[TINY_MAXL], wo[TINY_MAXL];
    tiny_table f1[TINY_MAXL], f2[TINY_MAXL];
    const uint8_t *g1[TINY_MAXL], *g2[TINY_MAXL], *gf; /* float32 gains */
} tiny_model;

typedef struct {
    const tiny_model *m;
    /* letter memory, inside caller memory */
    int8_t *kc[TINY_MAXL], *vc[TINY_MAXL];
    float *ks[TINY_MAXL], *vs[TINY_MAXL];
    /* work buffers */
    float bx[TINY_MAXD], bh[TINY_MAXD], bq[TINY_MAXD], bt[TINY_MAXD], ba[TINY_MAXD];
    float bm[TINY_MAXHID], bs[TINY_CTX];
    float score[TINY_VOCAB];
    /* state */
    int pos;             /* next position to fill                      */
    int phase;           /* 0 reading question, 1 answering, 2 done    */
    int answer_len;
    int creativity;      /* 0 = always the surest letter, 9 = wild     */
    uint32_t rng;
    /* monitor */
    float conf;                      /* probability of the last letter */
    float layer_act[TINY_MAXL];      /* mean ReLU activation per layer */
} tiny_ctx;

/* ---- model -------------------------------------------------------- */

/* Parse a TINY.BIN image. The image must stay valid while the model is used. */
int  tiny_model_init(tiny_model *m, const void *image, size_t len);
int  tiny_model_params(const tiny_model *m);     /* number of weights   */
/* shape: 0 width, 1 layers, 2 heads, 3 hidden (feed-forward) width */
int  tiny_model_shape(const tiny_model *m, int which);

/* ---- context ------------------------------------------------------ */

size_t tiny_ctx_mem_size(const tiny_model *m);   /* RAM needed for cache */
int  tiny_ctx_init(tiny_ctx *c, const tiny_model *m, void *mem, size_t len);

void tiny_set_creativity(tiny_ctx *c, int level); /* clamped to 0..9    */
void tiny_seed(tiny_ctx *c, uint32_t seed);

/* ---- asking ------------------------------------------------------- */

void tiny_reset(tiny_ctx *c);                 /* start a new question   */
int  tiny_feed(tiny_ctx *c, int ch);          /* read one letter 32..126 */
int  tiny_end_question(tiny_ctx *c);          /* question done           */
int  tiny_next(tiny_ctx *c);  /* next answer letter, 0 = end, <0 error   */

/* Convenience: clean up q, ask it, write the answer (NUL terminated) into
   out[max]. Returns the answer length or an error. */
int  tiny_answer(tiny_ctx *c, const char *q, char *out, int max);

/* Clean a typed line: printable ASCII only, no trailing spaces, at most
   TINY_MAXQ letters, ALL CAPS turned to lower case. out needs
   TINY_MAXQ+1 bytes. Returns the length. */
int  tiny_clean(const char *line, char *out);

/* ---- monitor ------------------------------------------------------ */

float tiny_confidence(const tiny_ctx *c);
float tiny_layer_activity(const tiny_ctx *c, int layer);
int   tiny_position(const tiny_ctx *c);

const char *tiny_strerror(int err);

/* sizes, for language bindings that treat the structs as opaque */
size_t tiny_model_sizeof(void);
size_t tiny_ctx_sizeof(void);

#ifdef __cplusplus
}
#endif
#endif
