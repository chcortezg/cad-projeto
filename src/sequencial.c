#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define ARQUIVO_SAIDA "sequencial.csv"
#define VERTICE_ORIGEM 0
#define NAO_VISITADO -1

typedef struct {
    char     assinatura[8]; // "BFSCSR1"
    uint32_t versao; // 1
    uint32_t flags; // bit 0 ligado = grafo nao dirigido
    int64_t  num_vertices;
    int64_t  num_arcos;
} Cabecalho;

static double agora(void)
{
    struct timespec instante;
    clock_gettime(CLOCK_MONOTONIC, &instante);
    return instante.tv_sec + instante.tv_nsec * 1e-9;
}

// Le o cabecalho e confere se o arquivo e um binario do preprocessador com o tamanho esperado. Retorna 1 se estiver tudo certo.
static int checarArquivo(FILE *arquivo, Cabecalho *cabecalho)
{
    if (fread(cabecalho, sizeof *cabecalho, 1, arquivo) != 1) return 0;
    if (memcmp(cabecalho->assinatura, "BFSCSR1", 8) != 0) return 0;
    if (cabecalho->versao != 1) return 0;
    if (cabecalho->num_vertices <= 0 || cabecalho->num_vertices > INT32_MAX) return 0;
    if (cabecalho->num_arcos < 0) return 0;

    long long tamanho_esperado = (long long)sizeof *cabecalho
                               + (cabecalho->num_vertices + 1) * (long long)sizeof(int64_t)
                               + cabecalho->num_arcos * (long long)sizeof(int32_t);

    fseek(arquivo, 0, SEEK_END);
    long long tamanho_real = ftell(arquivo);
    fseek(arquivo, sizeof *cabecalho, SEEK_SET);

    return tamanho_real == tamanho_esperado;
}

static int processarGrafoSequencial(const int64_t *inicio_vizinhos, const int32_t *vizinhos,
                                    int32_t *nivel_do_vertice, int32_t *fila,
                                    int32_t *alcancados)
{
    int32_t proximo_da_fila = 0;
    int32_t fim_da_fila = 0;

    nivel_do_vertice[VERTICE_ORIGEM] = 0;
    fila[fim_da_fila++] = VERTICE_ORIGEM;

    while (proximo_da_fila < fim_da_fila) {
        int32_t vertice_atual = fila[proximo_da_fila++];

        for (int64_t posicao = inicio_vizinhos[vertice_atual];
             posicao < inicio_vizinhos[vertice_atual + 1]; posicao++) {
            int32_t vizinho = vizinhos[posicao];

            if (nivel_do_vertice[vizinho] == NAO_VISITADO) {
                nivel_do_vertice[vizinho] = nivel_do_vertice[vertice_atual] + 1;
                fila[fim_da_fila++] = vizinho;
            }
        }
    }

    *alcancados = fim_da_fila;

    int32_t ultimo_vertice = fila[fim_da_fila - 1];
    return nivel_do_vertice[ultimo_vertice] + 1;
}

// Acrescenta uma linha em sequencial.csv (escreve o cabecalho se o arquivo estiver vazio).
static void gerarOutput(const char *nome_grafo, const Cabecalho *cabecalho, int32_t alcancados,
                        int niveis, double tempo_carga, double tempo_bfs)
{
    FILE *saida = fopen(ARQUIVO_SAIDA, "a");

    if (saida == NULL) {
        perror(ARQUIVO_SAIDA);
        return;
    }

    fseek(saida, 0, SEEK_END);
    if (ftell(saida) == 0) {
        fprintf(saida, "versao,grafo,dirigido,n_vertices,m_arcos,fonte,alcancados,niveis,"
                       "tempo_carga_s,tempo_bfs_s\n");
    }

    int dirigido = !(cabecalho->flags & 1);

    fprintf(saida, "sequencial,%s,%d,%lld,%lld,%d,%d,%d,%.6f,%.6f\n",
            nome_grafo, dirigido, (long long)cabecalho->num_vertices,
            (long long)cabecalho->num_arcos, VERTICE_ORIGEM, alcancados, niveis,
            tempo_carga, tempo_bfs);

    fclose(saida);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "uso: %s <grafo.csr.bin>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *nome_grafo = argv[1];

    // Leitura do binario (tempo de carga)
    double inicio_carga = agora();

    FILE *arquivo = fopen(nome_grafo, "rb");
    Cabecalho cabecalho;

    if (arquivo == NULL || !checarArquivo(arquivo, &cabecalho)) {
        fprintf(stderr, "%s: arquivo inexistente ou invalido\n", nome_grafo);
        return EXIT_FAILURE;
    }

    int32_t num_vertices = (int32_t)cabecalho.num_vertices;
    int64_t num_arcos = cabecalho.num_arcos;

    int64_t *inicio_vizinhos = malloc((size_t)(num_vertices + 1) * sizeof(int64_t));
    int32_t *vizinhos = malloc((size_t)num_arcos * sizeof(int32_t));
    int32_t *nivel_do_vertice = malloc((size_t)num_vertices * sizeof(int32_t));
    int32_t *fila = malloc((size_t)num_vertices * sizeof(int32_t));

    if (inicio_vizinhos == NULL || vizinhos == NULL || nivel_do_vertice == NULL || fila == NULL) {
        fprintf(stderr, "Erro de alocacao\n");
        return EXIT_FAILURE;
    }

    if (fread(inicio_vizinhos, sizeof(int64_t), (size_t)num_vertices + 1, arquivo) != (size_t)num_vertices + 1 ||
        fread(vizinhos, sizeof(int32_t), (size_t)num_arcos, arquivo) != (size_t)num_arcos) {
        fprintf(stderr, "%s: erro de leitura\n", nome_grafo);
        return EXIT_FAILURE;
    }

    fclose(arquivo);

    double tempo_carga = agora() - inicio_carga;

    // Preparacao (fora do tempo medido): ninguem visitado ainda
    for (int32_t vertice = 0; vertice < num_vertices; vertice++) {
        nivel_do_vertice[vertice] = NAO_VISITADO;
    }

    // BFS (tempo medido)
    double inicio_bfs = agora();

    int32_t alcancados;
    int niveis = processarGrafoSequencial(inicio_vizinhos, vizinhos, nivel_do_vertice, fila,
                                          &alcancados);

    double tempo_bfs = agora() - inicio_bfs;

    printf("%s | alcancados = %d de %d | niveis = %d | carga = %.3f s | BFS = %.6f s\n",
           nome_grafo, alcancados, num_vertices, niveis, tempo_carga, tempo_bfs);

    gerarOutput(nome_grafo, &cabecalho, alcancados, niveis, tempo_carga, tempo_bfs);

    free(inicio_vizinhos);
    free(vizinhos);
    free(nivel_do_vertice);
    free(fila);

    return EXIT_SUCCESS;
}
