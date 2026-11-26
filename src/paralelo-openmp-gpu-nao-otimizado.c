#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <omp.h>

/* Quantidade de nos de cada arvore testada. */
static const size_t tamanhos[] = {
    1u << 20,
    1u << 22,
    1u << 24,
    1u << 26
};

#define NUM_TAMANHOS (sizeof(tamanhos) / sizeof(tamanhos[0]))

#define REPETICOES 5
#define ARQUIVO_CSV "paralelo-openmp-gpu-nao-otimizado.csv"

/* Parametros dos tipos de arvore (ver funcoes pai* abaixo). */
#define GRAU_BALANCEADA 2
#define NIVEIS_PROFUNDA 256

/* --------------------------------------------------
 * Arvore em formato CSR (Compressed Sparse Row)
 *
 * Os filhos do no u ficam em
 *     filhos[offsets[u]] ... filhos[offsets[u + 1] - 1]
 *
 * Uma arvore com n nos tem n - 1 arestas, entao int32_t
 * basta para todos os tamanhos da lista (n <= 2^26).
 * -------------------------------------------------- */
typedef struct {
    int32_t n;
    int32_t *offsets; /* n + 1 posicoes */
    int32_t *filhos;  /* n - 1 posicoes */
} Arvore;

/* --------------------------------------------------
 * Tipos de arvore
 *
 * Cada tipo e definido apenas por uma funcao pai(v, n),
 * que diz quem e o pai do no v numa arvore de n nos.
 * Regras comuns a todos os tipos:
 *   - a raiz e o no 0;
 *   - todo no v >= 1 tem pai < v.
 * Isso garante que o resultado e sempre uma arvore conexa
 * e sem ciclos.
 *
 * As funcoes sao formulas fixas, sem nenhum sorteio: para
 * o mesmo n, a arvore gerada e sempre exatamente a mesma,
 * em qualquer maquina e em qualquer execucao.
 * -------------------------------------------------- */

/* Raiz quadrada inteira exata: maior r com r * r <= x. */
static int32_t raizInteira(int32_t x)
{
    int32_t r = (int32_t)sqrt((double)x);

    while ((int64_t)r * r > x) r--;
    while ((int64_t)(r + 1) * (r + 1) <= x) r++;

    return r;
}

/*
 * BALANCEADA: arvore k-aria completa (k = GRAU_BALANCEADA).
 * Todo no interno tem exatamente k filhos e os niveis tem
 * 1, k, k^2, ... nos. Com k = 2 e n = 2^26 sao 27 niveis.
 * Carga identica para todas as threads de um nivel.
 */
static int32_t paiBalanceada(int32_t v, int32_t n)
{
    (void)n;
    return (v - 1) / GRAU_BALANCEADA;
}

/*
 * DESBALANCEADA: o no u tem 2u + 1 filhos (os nos v com
 * raiz_inteira(v - 1) = u). Quanto maior o numero do no,
 * mais filhos ele tem: no mesmo nivel convivem nos com
 * poucos filhos e nos com milhares (ate ~16 mil em 2^26).
 * A arvore e rasa (cerca de 6 niveis) e testa o
 * desbalanceamento de carga entre threads.
 */
static int32_t paiDesbalanceada(int32_t v, int32_t n)
{
    (void)n;
    return raizInteira(v - 1);
}

/*
 * PROFUNDA: a raiz tem L filhos e cada um deles inicia uma
 * cadeia (no v e pai de v + L). Todos os niveis tem L nos,
 * com L escolhido para dar NIVEIS_PROFUNDA niveis abaixo
 * da raiz. Testa o custo de sincronizacao: muitos
 * superpassos com pouco trabalho em cada um.
 */
static int32_t paiProfunda(int32_t v, int32_t n)
{
    int32_t largura = (n - 1 + NIVEIS_PROFUNDA - 1) / NIVEIS_PROFUNDA;

    return v <= largura ? 0 : v - largura;
}

typedef int32_t (*FuncaoPai)(int32_t v, int32_t n);

typedef struct {
    const char *nome;
    FuncaoPai pai;
} TipoArvore;

static const TipoArvore tipos[] = {
    {"balanceada", paiBalanceada},
    {"desbalanceada", paiDesbalanceada},
    {"profunda", paiProfunda}
};

#define NUM_TIPOS (sizeof(tipos) / sizeof(tipos[0]))

/* --------------------------------------------------
 * Gera a arvore de n nos de um tipo, no formato CSR
 *
 * 1. Conta os filhos de cada no: cada v soma 1 no contador
 *    do seu pai (guardado em offsets[pai + 1]).
 * 2. Soma de prefixos: offsets[u] passa a ser a posicao em
 *    filhos onde comecam os filhos de u.
 * 3. Percorre os nos em ordem crescente e coloca cada um na
 *    proxima vaga livre (cursor) da lista do seu pai.
 *
 * Tudo e sequencial e em ordem fixa, entao o layout na
 * memoria tambem e sempre o mesmo. A geracao nao entra no
 * tempo da BFS.
 * -------------------------------------------------- */
static Arvore gerarArvore(size_t tamanho, const TipoArvore *tipo)
{
    Arvore a;
    a.n = (int32_t)tamanho;
    a.offsets = calloc(tamanho + 1, sizeof(int32_t));
    a.filhos = malloc((tamanho - 1) * sizeof(int32_t));
    int32_t *cursor = malloc(tamanho * sizeof(int32_t));

    if (a.offsets == NULL || a.filhos == NULL || cursor == NULL) {
        fprintf(stderr, "Erro de alocacao ao gerar arvore com %zu nos\n", tamanho);
        exit(EXIT_FAILURE);
    }

    /* 1: contagem de filhos */
    for (int32_t v = 1; v < a.n; v++) {
        int32_t p = tipo->pai(v, a.n);

        if (p < 0 || p >= v) {
            fprintf(stderr, "Arvore %s invalida: pai(%d) = %d\n", tipo->nome, v, p);
            exit(EXIT_FAILURE);
        }

        a.offsets[p + 1]++;
    }

    /* 2: soma de prefixos */
    for (int32_t u = 0; u < a.n; u++) {
        a.offsets[u + 1] += a.offsets[u];
    }

    /* 3: preenchimento das listas de filhos */
    memcpy(cursor, a.offsets, tamanho * sizeof(int32_t));

    for (int32_t v = 1; v < a.n; v++) {
        int32_t p = tipo->pai(v, a.n);
        a.filhos[cursor[p]++] = v;
    }

    free(cursor);

    return a;
}

static void liberarArvore(Arvore *a)
{
    free(a->offsets);
    free(a->filhos);
}

/* --------------------------------------------------
 * BFS de referencia (sequencial, com fila)
 *
 * Usada apenas para verificar o resultado das versoes
 * paralelas. Nao e cronometrada aqui; a versao sequencial
 * medida fica em sequencial.c.
 * -------------------------------------------------- */
static void bfsReferencia(const Arvore *a, int32_t *dist)
{
    int32_t *fila = malloc((size_t)a->n * sizeof(int32_t));

    if (fila == NULL) {
        exit(EXIT_FAILURE);
    }

    for (int32_t i = 0; i < a->n; i++) {
        dist[i] = -1;
    }

    int32_t inicio = 0, fim = 0;
    dist[0] = 0;
    fila[fim++] = 0;

    while (inicio < fim) {
        int32_t u = fila[inicio++];

        for (int32_t e = a->offsets[u]; e < a->offsets[u + 1]; e++) {
            int32_t v = a->filhos[e];
            dist[v] = dist[u] + 1;
            fila[fim++] = v;
        }
    }

    free(fila);
}

/* --------------------------------------------------
 * Prepara os vetores para uma nova BFS a partir da raiz
 * (fora do tempo medido)
 * -------------------------------------------------- */
static void inicializarBFS(int32_t n, int32_t *dist, int32_t *atual)
{
    for (int32_t i = 0; i < n; i++) {
        dist[i] = -1;
    }

    dist[0] = 0;
    atual[0] = 0;
}

/* --------------------------------------------------
 * BFS por niveis com OpenMP na GPU - NAO OTIMIZADA
 *
 * Cada iteracao do while e um superpasso BSP:
 *   - computacao: cada thread da GPU pega um no da
 *     fronteira atual e visita seus filhos;
 *   - comunicacao: os filhos sao escritos na proxima
 *     fronteira;
 *   - sincronizacao: a barreira implicita no fim da regiao
 *     target separa um nivel do outro.
 *
 * Pontos nao otimizados (alvos das proximas versoes):
 *   - a arvore inteira, dist e a proxima fronteira sao
 *     copiadas entre CPU e GPU a cada nivel (map dentro do
 *     laco), como na versao nao otimizada da Tarefa 13;
 *   - todas as threads disputam um unico contador atomico
 *     para reservar posicao na proxima fronteira;
 *   - niveis pequenos tambem vao para a GPU, mesmo quando a
 *     CPU seria mais rapida.
 *
 * Como cada no da arvore tem um unico pai, um filho nunca
 * e descoberto por duas threads: nao e preciso verificar
 * "visitado" nem proteger dist[v].
 *
 * Retorna o numero de niveis percorridos.
 * -------------------------------------------------- */
static int bfsGPUNaoOtimizado(const Arvore *a, int32_t *dist,
                              int32_t *atual, int32_t *proxima)
{
    const int32_t n = a->n;
    const int32_t *offsets = a->offsets;
    const int32_t *filhos = a->filhos;

    int32_t tam_atual = 1;
    int32_t nivel = 0;

    while (tam_atual > 0) {
        int32_t tam_proxima = 0;

        #pragma omp target teams distribute parallel for \
            map(to: offsets[0:n + 1], filhos[0:n - 1], atual[0:tam_atual]) \
            map(tofrom: dist[0:n], tam_proxima) \
            map(from: proxima[0:n])
        for (int32_t i = 0; i < tam_atual; i++) {
            int32_t u = atual[i];

            for (int32_t e = offsets[u]; e < offsets[u + 1]; e++) {
                int32_t v = filhos[e];
                int32_t pos;

                dist[v] = nivel + 1;

                #pragma omp atomic capture
                pos = tam_proxima++;

                proxima[pos] = v;
            }
        }

        int32_t *tmp = atual;
        atual = proxima;
        proxima = tmp;

        tam_atual = tam_proxima;
        nivel++;
    }

    return nivel;
}

static int verificarResultado(const int32_t *dist, const int32_t *ref, int32_t n)
{
    for (int32_t i = 0; i < n; i++) {
        if (dist[i] != ref[i]) {
            return 0;
        }
    }

    return 1;
}

/* Grau maximo da arvore e quantidade de nos do nivel mais largo. */
static void estatisticas(const Arvore *a, const int32_t *dist, int niveis,
                         int32_t *grau_max, int32_t *maior_nivel)
{
    int32_t *contagem = calloc((size_t)niveis + 1, sizeof(int32_t));

    if (contagem == NULL) {
        exit(EXIT_FAILURE);
    }

    *grau_max = 0;
    *maior_nivel = 0;

    for (int32_t u = 0; u < a->n; u++) {
        int32_t grau = a->offsets[u + 1] - a->offsets[u];

        if (grau > *grau_max) *grau_max = grau;

        contagem[dist[u]]++;
    }

    for (int k = 0; k <= niveis; k++) {
        if (contagem[k] > *maior_nivel) *maior_nivel = contagem[k];
    }

    free(contagem);
}

int main(int argc, char *argv[])
{
    /* Argumento opcional: maior tamanho a executar (para maquinas com pouca memoria). */
    size_t limite = argc > 1 ? (size_t)atol(argv[1]) : (size_t)-1;

    /* Inicializa o dispositivo fora das medicoes e descobre onde o target executa. */
    int na_gpu = 0;

    #pragma omp target map(from: na_gpu)
    {
        na_gpu = !omp_is_initial_device();
    }

    FILE *csv = fopen(ARQUIVO_CSV, "w");

    if (csv == NULL) {
        return EXIT_FAILURE;
    }

    fprintf(csv, "versao,tipo_arvore,executou_em,dispositivos,threads_cpu,n_nos,niveis,"
                 "maior_nivel,grau_max,repeticoes,tempo_geracao_s,tempo_medio_s,"
                 "tempo_min_s,tempo_max_s,desvio_padrao_s,verificado\n");

    printf("Dispositivos: %d | target executando em: %s | threads CPU: %d\n",
           omp_get_num_devices(), na_gpu ? "GPU" : "host", omp_get_max_threads());

    for (size_t k = 0; k < NUM_TIPOS; k++) {
        for (size_t t = 0; t < NUM_TAMANHOS; t++) {
            size_t tamanho = tamanhos[t];

            if (tamanho > limite) {
                break;
            }

            double inicio_geracao = omp_get_wtime();
            Arvore arv = gerarArvore(tamanho, &tipos[k]);
            double tempo_geracao = omp_get_wtime() - inicio_geracao;

            int32_t *ref = malloc(tamanho * sizeof(int32_t));
            int32_t *dist = malloc(tamanho * sizeof(int32_t));
            int32_t *atual = malloc(tamanho * sizeof(int32_t));
            int32_t *proxima = malloc(tamanho * sizeof(int32_t));

            if (ref == NULL || dist == NULL || atual == NULL || proxima == NULL) {
                fprintf(stderr, "Erro de alocacao para %zu nos\n", tamanho);
                return EXIT_FAILURE;
            }

            bfsReferencia(&arv, ref);

            double soma = 0.0, soma_quadrados = 0.0;
            double tempo_min = INFINITY, tempo_max = 0.0;
            int niveis = 0;
            int verificado = 1;

            for (int r = 0; r < REPETICOES; r++) {
                inicializarBFS(arv.n, dist, atual);

                double inicio = omp_get_wtime();
                niveis = bfsGPUNaoOtimizado(&arv, dist, atual, proxima);
                double tempo = omp_get_wtime() - inicio;

                if (!verificarResultado(dist, ref, arv.n)) {
                    verificado = 0;
                }

                soma += tempo;
                soma_quadrados += tempo * tempo;
                if (tempo < tempo_min) tempo_min = tempo;
                if (tempo > tempo_max) tempo_max = tempo;
            }

            double media = soma / REPETICOES;
            double variancia = soma_quadrados / REPETICOES - media * media;
            double desvio = variancia > 0.0 ? sqrt(variancia) : 0.0;

            int32_t grau_max, maior_nivel;
            estatisticas(&arv, ref, niveis, &grau_max, &maior_nivel);

            fprintf(csv, "nao_otimizado,%s,%s,%d,%d,%zu,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%d\n",
                    tipos[k].nome, na_gpu ? "gpu" : "host", omp_get_num_devices(),
                    omp_get_max_threads(), tamanho, niveis, maior_nivel, grau_max,
                    REPETICOES, tempo_geracao, media, tempo_min, tempo_max, desvio,
                    verificado);
            fflush(csv);

            printf("[NAO OTIMIZADO] %-13s n = %-9zu | niveis = %-4d | maior nivel = %-9d | "
                   "grau max = %-6d | BFS media = %.6f s (min %.6f s) | %s\n",
                   tipos[k].nome, tamanho, niveis, maior_nivel, grau_max, media,
                   tempo_min, verificado ? "OK" : "ERRO");
            fflush(stdout);

            free(ref);
            free(dist);
            free(atual);
            free(proxima);
            liberarArvore(&arv);

            if (!verificado) {
                fclose(csv);
                return EXIT_FAILURE;
            }
        }
    }

    fclose(csv);

    return EXIT_SUCCESS;
}
