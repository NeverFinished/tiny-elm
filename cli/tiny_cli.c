/* tiny_cli.c - plain text front end for the TINY core (stdin/stdout).

   usage: tiny [-m TINY.BIN] [-s SEED] [0-9]
     0-9   creativity (default 5; 0 = always the surest answer)
   Questions are read line by line; QUIT or EXIT stops. */

#include "tiny.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void *load_file(const char *name, size_t *len)
{
    FILE *fp = fopen(name, "rb");
    long n;
    void *p;
    if (fp == NULL) return NULL;
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    p = n > 0 ? malloc((size_t)n) : NULL;
    if (p && fread(p, 1, (size_t)n, fp) != (size_t)n) { free(p); p = NULL; }
    fclose(fp);
    *len = (size_t)n;
    return p;
}

static int is_word(const char *q, const char *w)
{
    int i;
    for (i = 0; q[i] && w[i]; i++)
        if ((q[i] | 32) != w[i]) return 0;
    return q[i] == 0 && w[i] == 0;
}

int main(int argc, char **argv)
{
    static tiny_model m;
    static tiny_ctx c;
    const char *path = "model/TINY.BIN";
    char line[256], q[TINY_MAXQ + 1];
    int i, err, ch, letters, creativity = 5;
    unsigned long seed = (unsigned long)time(NULL);
    size_t len, memlen;
    void *image, *mem;
    clock_t t0;
    double secs;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-m") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) seed = strtoul(argv[++i], NULL, 10);
        else if (argv[i][0] >= '0' && argv[i][0] <= '9') creativity = argv[i][0] - '0';
        else {
            fprintf(stderr, "usage: %s [-m TINY.BIN] [-s SEED] [0-9]\n", argv[0]);
            return 2;
        }
    }
    image = load_file(path, &len);
    if (image == NULL) {
        fprintf(stderr, "tiny: cannot read %s\n", path);
        return 1;
    }
    if ((err = tiny_model_init(&m, image, len)) < 0) {
        fprintf(stderr, "tiny: %s: %s\n", path, tiny_strerror(err));
        return 1;
    }
    memlen = tiny_ctx_mem_size(&m);
    mem = malloc(memlen);
    if (mem == NULL || (err = tiny_ctx_init(&c, &m, mem, memlen)) < 0) {
        fprintf(stderr, "tiny: %s\n", tiny_strerror(mem ? err : TINY_ERR_MEM));
        return 1;
    }
    tiny_set_creativity(&c, creativity);
    tiny_seed(&c, (uint32_t)seed);

    printf("TINY 2 - a neural network that answers questions.\n");
    printf("Ready: %d layers, %d wide, %d weights. Creativity %d.\n",
           m.layers, m.d, tiny_model_params(&m), creativity);
    printf("Type QUIT to stop.\n");
    for (;;) {
        printf("\n> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL)
            break;
        if (tiny_clean(line, q) == 0)
            continue;
        if (is_word(q, "quit") || is_word(q, "exit"))
            break;
        printf("  ");
        t0 = clock();
        tiny_reset(&c);
        for (i = 0; q[i]; i++)
            tiny_feed(&c, q[i]);
        tiny_end_question(&c);
        letters = 0;
        while ((ch = tiny_next(&c)) > 0) {
            putchar(ch);
            fflush(stdout);
            letters++;
        }
        secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("\n   [%d letters in %.2f seconds]\n", letters, secs);
    }
    printf("Goodbye!\n");
    free(mem);
    free(image);
    return 0;
}
