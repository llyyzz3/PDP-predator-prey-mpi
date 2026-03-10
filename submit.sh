#!/bin/bash
#SBATCH --job-name=predator_sim
#SBATCH --time=00:05:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=4
#SBATCH --account=m25oc
#SBATCH --partition=standard
#SBATCH --qos=standard

# 加载 Cirrus 推荐的 MPI 环境
module load mpt

# 确保你在工作目录下执行
echo "Current working directory: $(pwd)"
echo "Starting MPI simulation..."

# 运行你刚刚编译出来的 predator_sim 可执行文件
srun --unbuffered ./predator_sim