#!/bin/bash
#SBATCH --job-name=strong_scaling
#SBATCH --nodes=1
#SBATCH --exclusive
#SBATCH --ntasks-per-node=8
#SBATCH --time=00:30:00
#SBATCH --account=m25oc
#SBATCH --partition=standard
#SBATCH --qos=standard

module load mpt

# 编译一个版本即可，问题大小固定不变
# GRID_SIZE=160 能被 1/2/4/8 进程 × ACTORS_PER_UE(2) 整除
mpicc -O2 -DGRID_SIZE=160 -DINITIAL_PREY=256000 -DINITIAL_PREDATORS=6400 \
    main.c actor_framework.c predator_model.c -o predator_sim_strong

echo "================================"
echo "Strong Scaling Test"
echo "Fixed problem: GRID=160, PREY=256000, PRED=6400"
echo "================================"

echo "--- 1 process ---"
srun -n 1 ./predator_sim_strong

echo "--- 2 processes ---"
srun -n 2 ./predator_sim_strong

echo "--- 4 processes ---"
srun -n 4 ./predator_sim_strong

echo "--- 8 processes ---"
srun -n 8 ./predator_sim_strong

echo "--- 16 processes ---"
srun -n 16 ./predator_sim_strong

