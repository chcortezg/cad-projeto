#!/bin/bash
#SBATCH --job-name=bfs-cuda
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --gres=gpu:1
#SBATCH --time=00:30:00
#SBATCH --output=bfs_%j.out
#SBATCH --error=bfs_%j.err
# A particiao e escolhida na hora de enviar:
#   GRAFO=web-Google.txt sbatch -p gpu-4-a100 run_bfs.sh
#   GRAFO=web-Google.txt sbatch -p gpu-8-h100 run_bfs.sh
#   GRAFO=web-Google.txt sbatch -p gpu-4-h200 run_bfs.sh

GRAFO=${GRAFO:-web-Google.txt}
ORIGEM=${ORIGEM:-0}
REPS=${REPS:-10}

module load cuda

echo "Particao: $SLURM_JOB_PARTITION | No: $SLURMD_NODENAME | Grafo: $GRAFO"
nvidia-smi --query-gpu=name,memory.total --format=csv,noheader

# Um binario so, com codigo para A100 (sm_80) e H100/H200 (sm_90)
ARCH="-gencode arch=compute_80,code=sm_80 -gencode arch=compute_90,code=sm_90"
nvcc -O3 $ARCH bfs.cu -o bfs_$SLURM_JOB_ID
nvcc -O3 $ARCH bfs_frontier.cu -o bfs_frontier_$SLURM_JOB_ID

echo "=== Versao simples ==="
./bfs_$SLURM_JOB_ID "$GRAFO" "$ORIGEM" "$REPS"

echo "=== Versao com fronteira ==="
./bfs_frontier_$SLURM_JOB_ID "$GRAFO" "$ORIGEM" "$REPS"

rm -f bfs_$SLURM_JOB_ID bfs_frontier_$SLURM_JOB_ID
