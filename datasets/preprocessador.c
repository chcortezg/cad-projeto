#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define CSR_MAGIC "BFSCSR1"
#define CSR_VERSAO 1u
#define CSR_NAO_DIRIGIDO 1u

typedef struct {
    char     magic[8];
    uint32_t versao;
    uint32_t flags;
    int64_t  n;
    int64_t  m;
} CabecalhoCSR;
_Static_assert(sizeof(CabecalhoCSR) == 32, "cabecalho deve ter 32 bytes");

static double agora(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void *xcalloc(size_t qtd, size_t tam) {
    void *p = calloc(qtd, tam);
    if (!p && qtd) { fprintf(stderr, "sem memoria (%zu x %zu bytes)\n", qtd, tam); exit(1); }
    return p;
}

static void escrever(const void *p, size_t tam, size_t qtd, FILE *f) {
    if (fwrite(p, tam, qtd, f) != qtd) { perror("fwrite"); exit(1); }
}

/* 1 = aresta lida, 0 = comentario/linha vazia, -1 = malformada */
static int parse_linha(const char *s, uint64_t *u, uint64_t *v) {
    uint64_t x[2];
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#' || *s == '\n' || *s == '\r' || *s == '\0') return 0;
    for (int k = 0; k < 2; k++) {
        while (*s == ' ' || *s == '\t') s++;
        if (*s < '0' || *s > '9') return -1;
        uint64_t a = 0;
        while (*s >= '0' && *s <= '9') a = a * 10 + (uint64_t)(*s++ - '0');
        x[k] = a;
    }
    *u = x[0]; *v = x[1];
    return 1;
}

static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "uso: %s <entrada.txt> <saida.bin>\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    setvbuf(f, NULL, _IOFBF, 1 << 24);

    char buf[256];
    uint64_t u, v;
    long long linha = 0, arestas = 0, lacos = 0;
    double t0 = agora();

    /* Passe 1: maior ID, IDs presentes e graus */
    uint64_t cap = 1u << 20, max_id = 0;
    int64_t *grau = xcalloc(cap, sizeof *grau);
    uint8_t *presente = xcalloc(cap, 1);
    while (fgets(buf, sizeof buf, f)) {
        linha++;
        int r = parse_linha(buf, &u, &v);
        if (r == 0) continue;
        if (r < 0) { fprintf(stderr, "linha %lld malformada: %s", linha, buf); return 1; }
        uint64_t maior = u > v ? u : v;
        if (maior >= cap) {
            uint64_t novo = cap;
            while (novo <= maior) novo *= 2;
            grau = realloc(grau, novo * sizeof *grau);
            presente = realloc(presente, novo);
            if (!grau || !presente) { fprintf(stderr, "sem memoria\n"); return 1; }
            memset(grau + cap, 0, (novo - cap) * sizeof *grau);
            memset(presente + cap, 0, novo - cap);
            cap = novo;
        }
        if (maior > max_id) max_id = maior;
        presente[u] = presente[v] = 1;
        arestas++;
        if (u == v) { lacos++; continue; }
        grau[u]++; grau[v]++;
    }
    double t1 = agora();

    /* Renumeracao para 0..n-1 */
    int32_t *novo_id = xcalloc(max_id + 1, sizeof *novo_id);
    int64_t n = 0;
    for (uint64_t i = 0; i <= max_id; i++)
        novo_id[i] = presente[i] ? (int32_t)n++ : -1;
    if (n > INT32_MAX) { fprintf(stderr, "n excede int32\n"); return 1; }
    free(presente);

    /* Offsets por soma de prefixos */
    int64_t *offsets = xcalloc((size_t)n + 1, sizeof *offsets);
    for (uint64_t i = 0; i <= max_id; i++)
        if (novo_id[i] >= 0) offsets[novo_id[i] + 1] = grau[i];
    for (int64_t i = 0; i < n; i++) offsets[i + 1] += offsets[i];
    int64_t m_bruto = offsets[n];

    int64_t *cursor = grau;  /* reaproveita o vetor de graus */
    memcpy(cursor, offsets, (size_t)n * sizeof *cursor);
    int32_t *destinos = xcalloc((size_t)m_bruto, sizeof *destinos);

    /* Passe 2: preenche os dois sentidos de cada aresta */
    rewind(f);
    while (fgets(buf, sizeof buf, f)) {
        if (parse_linha(buf, &u, &v) != 1 || u == v) continue;
        int32_t a = novo_id[u], b = novo_id[v];
        destinos[cursor[a]++] = b;
        destinos[cursor[b]++] = a;
    }
    fclose(f);
    for (int64_t i = 0; i < n; i++)
        if (cursor[i] != offsets[i + 1]) { fprintf(stderr, "arquivo mudou entre os passes\n"); return 1; }
    free(cursor);
    free(novo_id);
    double t2 = agora();

    /* Ordena cada lista e remove duplicatas no lugar */
    #pragma omp parallel for schedule(dynamic, 4096)
    for (int64_t i = 0; i < n; i++)
        qsort(destinos + offsets[i], (size_t)(offsets[i + 1] - offsets[i]), sizeof *destinos, cmp_i32);

    int64_t w = 0, ini = 0, grau_max = 0;
    for (int64_t i = 0; i < n; i++) {
        int64_t fim = offsets[i + 1], w0 = w;
        offsets[i] = w0;
        for (int64_t e = ini; e < fim; e++)
            if (w == w0 || destinos[w - 1] != destinos[e]) destinos[w++] = destinos[e];
        if (w - w0 > grau_max) grau_max = w - w0;
        ini = fim;
    }
    offsets[n] = w;
    double t3 = agora();

    /* Gravacao */
    CabecalhoCSR h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, CSR_MAGIC, sizeof h.magic);
    h.versao = CSR_VERSAO;
    h.flags = CSR_NAO_DIRIGIDO;
    h.n = n;
    h.m = w;
    FILE *g = fopen(argv[2], "wb");
    if (!g) { perror(argv[2]); return 1; }
    escrever(&h, sizeof h, 1, g);
    escrever(offsets, sizeof *offsets, (size_t)n + 1, g);
    escrever(destinos, sizeof *destinos, (size_t)w, g);
    if (fclose(g) != 0) { perror("fclose"); return 1; }
    double t4 = agora();

    printf("arestas lidas: %lld | lacos: %lld | duplicatas removidas: %lld\n",
           arestas, lacos, (long long)((m_bruto - w) / 2));
    printf("maior ID: %llu | n: %lld | m (arcos): %lld | grau max: %lld | grau medio: %.2f\n",
           (unsigned long long)max_id, (long long)n, (long long)w, (long long)grau_max, (double)w / n);
    printf("tempos (s): passe1 %.2f | passe2 %.2f | ordenar %.2f | gravar %.2f | total %.2f\n",
           t1 - t0, t2 - t1, t3 - t2, t4 - t3, t4 - t0);
    free(offsets);
    free(destinos);
    return 0;
}