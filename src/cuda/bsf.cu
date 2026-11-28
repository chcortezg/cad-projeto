// BFS por niveis em GPU (CUDA), grafo em CSR.
// Aceita lista de arestas (SNAP) e Matrix Market (.mtx).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cuda_runtime.h>

#define CK(x) do { cudaError_t e = (x); if (e != cudaSuccess) { \
    fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, cudaGetErrorString(e)); exit(1); } } while (0)

// Um thread por vertice: se o vertice esta no nivel atual, marca os vizinhos nao visitados.
__global__ void bfs_level(int n, const int* rowptr, const int* col,
                          int* dist, int level, int* changed) {
    int v = blockIdx.x * blockDim.x + threadIdx.x;
    if (v >= n || dist[v] != level) return;
    for (int i = rowptr[v]; i < rowptr[v + 1]; i++) {
        int u = col[i];
        if (dist[u] == -1) { dist[u] = level + 1; *changed = 1; }  // varios threads escrevem o mesmo valor, sem problema
    }
}

// Le o arquivo e monta o CSR (rowptr: inicio da lista de vizinhos de cada vertice; col: os vizinhos).
static void read_graph(const char* path, bool directed, int& n,
                       std::vector<int>& rowptr, std::vector<int>& col) {
    FILE* f = fopen(path, "r");
    if (!f) { perror("abrir arquivo"); exit(1); }
    bool mtx = strstr(path, ".mtx") != nullptr;
    std::vector<int> src, dst;
    char line[256];
    bool header_done = !mtx;  // no .mtx a primeira linha sem comentario e o cabecalho
    int maxid = -1;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '%') continue;
        if (!header_done) { header_done = true; continue; }
        int a, b;
        if (sscanf(line, "%d %d", &a, &b) != 2) continue;
        if (mtx) { a--; b--; }  // .mtx comeca em 1
        src.push_back(a); dst.push_back(b);
        maxid = std::max(maxid, std::max(a, b));
    }
    fclose(f);
    n = maxid + 1;
    size_t m = src.size();

    rowptr.assign(n + 1, 0);
    for (size_t i = 0; i < m; i++) {
        rowptr[src[i] + 1]++;
        if (!directed) rowptr[dst[i] + 1]++;
    }
    for (int v = 0; v < n; v++) rowptr[v + 1] += rowptr[v];
    col.resize(rowptr[n]);
    std::vector<int> pos(rowptr.begin(), rowptr.end() - 1);
    for (size_t i = 0; i < m; i++) {
        col[pos[src[i]]++] = dst[i];
        if (!directed) col[pos[dst[i]]++] = src[i];
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "uso: %s grafo [origem] [reps] [direcionado]\n", argv[0]); return 1; }
    int source = argc > 2 ? atoi(argv[2]) : 0;
    int reps = argc > 3 ? atoi(argv[3]) : 5;
    bool directed = argc > 4 ? atoi(argv[4]) != 0 : false;

    int n;
    std::vector<int> h_rowptr, h_col;
    read_graph(argv[1], directed, n, h_rowptr, h_col);
    int m = h_col.size();

    cudaDeviceProp prop;
    CK(cudaGetDeviceProperties(&prop, 0));
    printf("GPU: %s | vertices: %d | arestas (CSR): %d\n", prop.name, n, m);

    int *d_rowptr, *d_col, *d_dist, *d_changed;
    CK(cudaMalloc(&d_rowptr, (n + 1) * sizeof(int)));
    CK(cudaMalloc(&d_col, (size_t)m * sizeof(int)));
    CK(cudaMalloc(&d_dist, n * sizeof(int)));
    CK(cudaMalloc(&d_changed, sizeof(int)));

    cudaEvent_t t0, t1;
    CK(cudaEventCreate(&t0)); CK(cudaEventCreate(&t1));

    // Tempo de copia do grafo para a GPU
    CK(cudaEventRecord(t0));
    CK(cudaMemcpy(d_rowptr, h_rowptr.data(), (n + 1) * sizeof(int), cudaMemcpyHostToDevice));
    CK(cudaMemcpy(d_col, h_col.data(), (size_t)m * sizeof(int), cudaMemcpyHostToDevice));
    CK(cudaEventRecord(t1)); CK(cudaEventSynchronize(t1));
    float copy_ms; CK(cudaEventElapsedTime(&copy_ms, t0, t1));
    printf("copia para GPU: %.3f ms\n", copy_ms);

    const int B = 256, G = (n + B - 1) / B;
    float total_ms = 0;
    int levels = 0, reached = 0;
    std::vector<int> h_dist(n);

    for (int r = 0; r < reps; r++) {
        CK(cudaMemset(d_dist, 0xFF, n * sizeof(int)));  // tudo = -1 (nao visitado)
        int zero = 0;
        CK(cudaMemcpy(d_dist + source, &zero, sizeof(int), cudaMemcpyHostToDevice));

        CK(cudaEventRecord(t0));
        int level = 0, changed = 1;
        while (changed) {
            changed = 0;
            CK(cudaMemcpy(d_changed, &changed, sizeof(int), cudaMemcpyHostToDevice));
            bfs_level<<<G, B>>>(n, d_rowptr, d_col, d_dist, level, d_changed);
            CK(cudaMemcpy(&changed, d_changed, sizeof(int), cudaMemcpyDeviceToHost));
            if (changed) level++;
        }
        CK(cudaEventRecord(t1)); CK(cudaEventSynchronize(t1));
        float ms; CK(cudaEventElapsedTime(&ms, t0, t1));
        total_ms += ms;
        levels = level + 1;
    }

    CK(cudaMemcpy(h_dist.data(), d_dist, n * sizeof(int), cudaMemcpyDeviceToHost));
    for (int v = 0; v < n; v++) if (h_dist[v] >= 0) reached++;

    printf("BFS: %.3f ms (media de %d execucoes) | niveis: %d | alcancados: %d\n",
           total_ms / reps, reps, levels, reached);

    cudaFree(d_rowptr); cudaFree(d_col); cudaFree(d_dist); cudaFree(d_changed);
    return 0;
}
