/* tiny.c - TINY 2 transformer core. See tiny.h.
   Portable C99: needs <math.h> (sqrtf, expf) and <string.h> (memcpy) only.
   Ported from the DOS version (dos/TINY.C); same maths, same TINY.BIN. */

#include "tiny.h"
#include <math.h>
#include <string.h>

/* ---- reading the little-endian image ------------------------------ */

static float ld_f32(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static int ld_u16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

typedef struct {
    const uint8_t *p, *end;
    int ok;
} reader;

static const uint8_t *take(reader *r, size_t n)
{
    const uint8_t *p = r->p;
    if (!r->ok || (size_t)(r->end - r->p) < n) {
        r->ok = 0;
        return NULL;
    }
    r->p += n;
    return p;
}

static void take_table(reader *r, tiny_table *t, int rows, int cols)
{
    t->rows = rows;
    t->cols = cols;
    t->data = take(r, (size_t)rows * (size_t)(4 + cols));
}

/* ---- model -------------------------------------------------------- */

int tiny_model_init(tiny_model *m, const void *image, size_t len)
{
    reader r;
    const uint8_t *h;
    int l, D, L, H, HID;

    if (m == NULL || image == NULL)
        return TINY_ERR_ARG;
    memset(m, 0, sizeof(*m));
    r.p = (const uint8_t *)image;
    r.end = r.p + len;
    r.ok = 1;
    h = take(&r, 12);
    if (h == NULL || memcmp(h, "TNY2", 4) != 0)
        return TINY_ERR_FORMAT;
    D = ld_u16(h + 4); L = ld_u16(h + 6); H = ld_u16(h + 8); HID = ld_u16(h + 10);
    if (D < 8 || D > TINY_MAXD || L < 1 || L > TINY_MAXL || H < 1 || D % H != 0 ||
        HID < 8 || HID > TINY_MAXHID)
        return TINY_ERR_SIZE;
    m->d = D; m->layers = L; m->heads = H; m->hidden = HID;
    m->head_dim = D / H;
    m->att_div = sqrtf((float)m->head_dim);

    take_table(&r, &m->emb, TINY_VOCAB, D);
    take_table(&r, &m->pos, TINY_CTX, D);
    for (l = 0; l < L; l++) {
        m->g1[l] = take(&r, 4 * (size_t)D);
        take_table(&r, &m->wq[l], D, D);
        take_table(&r, &m->wk[l], D, D);
        take_table(&r, &m->wv[l], D, D);
        take_table(&r, &m->wo[l], D, D);
        m->g2[l] = take(&r, 4 * (size_t)D);
        take_table(&r, &m->f1[l], HID, D);
        take_table(&r, &m->f2[l], D, HID);
    }
    m->gf = take(&r, 4 * (size_t)D);
    if (!r.ok || r.p != r.end)
        return TINY_ERR_LENGTH;
    return TINY_OK;
}

int tiny_model_params(const tiny_model *m)
{
    int D = m->d;
    return TINY_VOCAB * D + TINY_CTX * D +
           m->layers * (4 * D * D + 2 * D * m->hidden + 2 * D) + D;
}

int tiny_model_shape(const tiny_model *m, int which)
{
    switch (which) {
    case 0: return m->d;
    case 1: return m->layers;
    case 2: return m->heads;
    case 3: return m->hidden;
    default: return TINY_ERR_ARG;
    }
}

/* ---- context ------------------------------------------------------ */

size_t tiny_ctx_mem_size(const tiny_model *m)
{
    size_t per = 2 * TINY_CTX * sizeof(float) + 2 * (size_t)TINY_CTX * m->d;
    return m->layers * per + sizeof(float);       /* + alignment slack */
}

int tiny_ctx_init(tiny_ctx *c, const tiny_model *m, void *mem, size_t len)
{
    uint8_t *p;
    int l;

    if (c == NULL || m == NULL || mem == NULL)
        return TINY_ERR_ARG;
    if (len < tiny_ctx_mem_size(m))
        return TINY_ERR_MEM;
    memset(c, 0, sizeof(*c));
    c->m = m;
    p = (uint8_t *)mem;
    while ((uintptr_t)p % sizeof(float)) p++;
    for (l = 0; l < m->layers; l++) {
        c->ks[l] = (float *)p; p += TINY_CTX * sizeof(float);
        c->vs[l] = (float *)p; p += TINY_CTX * sizeof(float);
    }
    for (l = 0; l < m->layers; l++) {
        c->kc[l] = (int8_t *)p; p += (size_t)TINY_CTX * m->d;
        c->vc[l] = (int8_t *)p; p += (size_t)TINY_CTX * m->d;
    }
    c->creativity = 5;
    c->rng = 0x9E3779B9u;
    tiny_reset(c);
    return TINY_OK;
}

void tiny_set_creativity(tiny_ctx *c, int level)
{
    c->creativity = level < 0 ? 0 : level > 9 ? 9 : level;
}

void tiny_seed(tiny_ctx *c, uint32_t seed)
{
    c->rng = seed ? seed : 0x9E3779B9u;
}

static uint32_t rnd(tiny_ctx *c)                  /* xorshift32 */
{
    uint32_t x = c->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return c->rng = x;
}

/* ---- the network -------------------------------------------------- */

/* y = table * x */
static void matvec(const tiny_table *t, const float *x, float *y)
{
    int r, c, cols = t->cols;
    const uint8_t *row = t->data;
    const int8_t *w;
    float s;
    for (r = 0; r < t->rows; r++) {
        w = (const int8_t *)(row + 4);
        s = 0.0f;
        for (c = 0; c < cols; c++)
            s += w[c] * x[c];
        y[r] = s * ld_f32(row);
        row += 4 + cols;
    }
}

/* y = x scaled to a standard size, times the gains g */
static void norm(int D, const float *x, const uint8_t *g, float *y)
{
    int i;
    float s = 0.0f;
    for (i = 0; i < D; i++)
        s += x[i] * x[i];
    s = 1.0f / sqrtf(s / D + 1e-5f);
    for (i = 0; i < D; i++)
        y[i] = x[i] * s * ld_f32(g + 4 * i);
}

static float safe_exp(float x)
{
    return x < -60.0f ? 0.0f : expf(x);
}

/* keep a list of D numbers as bytes, returning the scale */
static float store(int D, int8_t *dst, const float *src)
{
    int i;
    float big = 0.0f, v, k;
    for (i = 0; i < D; i++) {
        v = src[i] < 0 ? -src[i] : src[i];
        if (v > big) big = v;
    }
    if (big < 1e-20f)
        big = 1e-20f;
    k = 127.0f / big;
    for (i = 0; i < D; i++) {
        v = src[i] * k;
        dst[i] = (int8_t)(v < 0 ? v - 0.5f : v + 0.5f);
    }
    return big / 127.0f;
}

/* Feed token `tok` at position p; fills c->score. */
static void step(tiny_ctx *c, int tok, int p)
{
    const tiny_model *m = c->m;
    int D = m->d, H = m->heads, HD = m->head_dim, HID = m->hidden;
    int i, j, l, h;
    const uint8_t *er = m->emb.data + (size_t)tok * (4 + D);
    const uint8_t *pr = m->pos.data + (size_t)p * (4 + D);
    const int8_t *e = (const int8_t *)(er + 4), *q = (const int8_t *)(pr + 4);
    const int8_t *kp, *vp;
    float s, big, sum, es = ld_f32(er), ps = ld_f32(pr);
    float *bx = c->bx, *bh = c->bh, *bq = c->bq, *bt = c->bt, *ba = c->ba;
    float *bm = c->bm, *bs = c->bs;

    for (i = 0; i < D; i++)
        bx[i] = e[i] * es + q[i] * ps;

    for (l = 0; l < m->layers; l++) {
        /* attention over the earlier letters */
        norm(D, bx, m->g1[l], bh);
        matvec(&m->wk[l], bh, bt);
        c->ks[l][p] = store(D, c->kc[l] + p * D, bt);
        matvec(&m->wv[l], bh, bt);
        c->vs[l][p] = store(D, c->vc[l] + p * D, bt);
        matvec(&m->wq[l], bh, bq);
        for (h = 0; h < H; h++) {
            big = -1e30f;
            for (j = 0; j <= p; j++) {
                kp = c->kc[l] + j * D + h * HD;
                s = 0.0f;
                for (i = 0; i < HD; i++)
                    s += bq[h * HD + i] * kp[i];
                s = s * c->ks[l][j] / m->att_div;
                bs[j] = s;
                if (s > big) big = s;
            }
            sum = 0.0f;
            for (j = 0; j <= p; j++) {
                bs[j] = safe_exp(bs[j] - big);
                sum += bs[j];
            }
            for (i = 0; i < HD; i++)
                ba[h * HD + i] = 0.0f;
            for (j = 0; j <= p; j++) {
                if (bs[j] == 0.0f)
                    continue;
                vp = c->vc[l] + j * D + h * HD;
                s = bs[j] * c->vs[l][j] / sum;
                for (i = 0; i < HD; i++)
                    ba[h * HD + i] += s * vp[i];
            }
        }
        matvec(&m->wo[l], ba, bt);
        for (i = 0; i < D; i++)
            bx[i] += bt[i];

        /* feed-forward */
        norm(D, bx, m->g2[l], bh);
        matvec(&m->f1[l], bh, bm);
        s = 0.0f;
        for (i = 0; i < HID; i++) {
            if (bm[i] < 0.0f) bm[i] = 0.0f;
            else s += bm[i];
        }
        c->layer_act[l] = s / HID;
        matvec(&m->f2[l], bm, bt);
        for (i = 0; i < D; i++)
            bx[i] += bt[i];
    }
    norm(D, bx, m->gf, bh);
    matvec(&m->emb, bh, c->score);
}

/* ---- choosing letters --------------------------------------------- */

static int chosen(tiny_ctx *c, int tok)
{
    int i;
    float sum = 0.0f;
    for (i = 0; i < TINY_VOCAB; i++)
        sum += safe_exp(c->score[i] - c->score[tok]);
    c->conf = 1.0f / sum;
    return tok;
}

/* With creativity 0 the best letter; otherwise a random one among those
   that fit nearly as well. */
static int pick(tiny_ctx *c)
{
    int i, best = 0;
    float t, sum = 0.0f, r, *w = c->bs, *score = c->score;

    for (i = 1; i < TINY_VOCAB; i++)
        if (score[i] > score[best]) best = i;
    if (c->creativity <= 0)
        return chosen(c, best);
    t = c->creativity / 5.0f;
    for (i = 0; i < TINY_VOCAB; i++) {
        w[i] = safe_exp((score[i] - score[best]) / t);
        if (w[i] < 0.15f) w[i] = 0.0f;           /* never pick unlikely letters */
        sum += w[i];
    }
    r = sum * (float)(rnd(c) % 10000u) / 10000.0f;
    for (i = 0; i < TINY_VOCAB; i++) {
        r -= w[i];
        if (r < 0.0f && w[i] > 0.0f) return chosen(c, i);
    }
    return chosen(c, best);
}

/* ---- asking ------------------------------------------------------- */

void tiny_reset(tiny_ctx *c)
{
    int l;
    c->pos = 0;
    c->phase = 0;
    c->answer_len = 0;
    c->conf = 0.0f;
    for (l = 0; l < TINY_MAXL; l++)
        c->layer_act[l] = 0.0f;
}

int tiny_feed(tiny_ctx *c, int ch)
{
    if (c->phase != 0)
        return TINY_ERR_STATE;
    if (ch < 32 || ch > 126)
        return TINY_ERR_ARG;
    if (c->pos >= TINY_MAXQ)
        return TINY_ERR_FULL;
    step(c, ch - 30, c->pos++);
    return TINY_OK;
}

int tiny_end_question(tiny_ctx *c)
{
    if (c->phase != 0)
        return TINY_ERR_STATE;
    step(c, 1, c->pos++);
    c->phase = 1;
    return TINY_OK;
}

int tiny_next(tiny_ctx *c)
{
    int tok;
    if (c->phase != 1)
        return c->phase == 2 ? 0 : TINY_ERR_STATE;
    if (c->pos >= TINY_CTX) {
        c->phase = 2;
        return 0;
    }
    tok = pick(c);
    if (tok <= 1) {
        c->phase = 2;
        return 0;
    }
    step(c, tok, c->pos++);
    c->answer_len++;
    return tok + 30;
}

int tiny_clean(const char *line, char *q)
{
    int i, n = 0, lower = 0, upper = 0;
    for (i = 0; line[i] && n < TINY_MAXQ; i++)
        if (line[i] >= 32 && line[i] <= 126)
            q[n++] = line[i];
    while (n > 0 && q[n - 1] == ' ')
        n--;
    q[n] = 0;
    for (i = 0; i < n; i++) {
        if (q[i] >= 'a' && q[i] <= 'z') lower++;
        if (q[i] >= 'A' && q[i] <= 'Z') upper++;
    }
    if (upper > 0 && lower == 0)
        for (i = 0; i < n; i++)
            if (q[i] >= 'A' && q[i] <= 'Z') q[i] += 'a' - 'A';
    return n;
}

int tiny_answer(tiny_ctx *c, const char *q, char *out, int max)
{
    char buf[TINY_MAXQ + 1];
    int i, n, ch, err;

    if (c == NULL || q == NULL || out == NULL || max < 1)
        return TINY_ERR_ARG;
    n = tiny_clean(q, buf);
    tiny_reset(c);
    for (i = 0; i < n; i++)
        if ((err = tiny_feed(c, buf[i])) < 0)
            return err;
    if ((err = tiny_end_question(c)) < 0)
        return err;
    n = 0;
    while ((ch = tiny_next(c)) > 0)
        if (n < max - 1)
            out[n++] = (char)ch;
    out[n] = 0;
    return ch < 0 ? ch : n;
}

/* ---- monitor ------------------------------------------------------ */

float tiny_confidence(const tiny_ctx *c) { return c->conf; }

float tiny_layer_activity(const tiny_ctx *c, int layer)
{
    return layer >= 0 && layer < c->m->layers ? c->layer_act[layer] : 0.0f;
}

int tiny_position(const tiny_ctx *c) { return c->pos; }

const char *tiny_strerror(int err)
{
    switch (err) {
    case TINY_OK:         return "ok";
    case TINY_ERR_ARG:    return "bad argument";
    case TINY_ERR_FORMAT: return "not a version 2 network file (TNY2)";
    case TINY_ERR_SIZE:   return "network size beyond this build's limits";
    case TINY_ERR_LENGTH: return "network file too short, too long or damaged";
    case TINY_ERR_MEM:    return "not enough memory given";
    case TINY_ERR_FULL:   return "question too long";
    case TINY_ERR_STATE:  return "call out of order";
    default:              return "unknown error";
    }
}

size_t tiny_model_sizeof(void) { return sizeof(tiny_model); }
size_t tiny_ctx_sizeof(void)   { return sizeof(tiny_ctx); }
