#!/bin/bash
#SBATCH --job-name=weak_scaling
#SBATCH --nodes=2
#SBATCH --exclusive
#SBATCH --ntasks-per-node=8
#SBATCH --time=00:30:00
#SBATCH --account=m25oc
#SBATCH --partition=standard
#SBATCH --qos=standard

module load mpt

# 编译三个版本
mpicc -O2 -DGRID_SIZE=40  -DINITIAL_PREY=16000  -DINITIAL_PREDATORS=400  \
    main.c actor_framework.c predator_model.c -o predator_sim_g40

# 2 processes: Area = 3136 (56x56 ≈ 2x 基准)
mpicc -O2 -DGRID_SIZE=56  -DINITIAL_PREY=31360  -DINITIAL_PREDATORS=784 \
    main.c actor_framework.c predator_model.c -o predator_sim_g56

mpicc -O2 -DGRID_SIZE=80  -DINITIAL_PREY=64000  -DINITIAL_PREDATORS=1600 \
    main.c actor_framework.c predator_model.c -o predator_sim_g80

# 8 processes: Area = 12544 (112x112, 适配 16 Actors)
mpicc -O2 -DGRID_SIZE=112 -DINITIAL_PREY=125440 -DINITIAL_PREDATORS=3136 \
    main.c actor_framework.c predator_model.c -o predator_sim_g112

mpicc -O2 -DGRID_SIZE=160 -DINITIAL_PREY=256000 -DINITIAL_PREDATORS=6400 \
    main.c actor_framework.c predator_model.c -o predator_sim_g160

echo "================================"
echo "Weak Scaling Test"
echo "Fixed density: 10 prey/cell, 0.25 pred/cell"
echo "================================"

echo "--- 1 process, GRID=40x40 (Area=1600) ---"
srun -n 1 ./predator_sim_g40

echo "--- 2 processes, GRID=56x56 (Area=3136) ---"
srun -n 2 ./predator_sim_g56

echo "--- 4 processes, GRID=80x80 (Area=6400) ---"
srun -n 4 ./predator_sim_g80

echo "--- 8 processes, GRID=113x113 (Area=12769) ---"
srun -n 8 ./predator_sim_g112

echo "--- 16 processes, GRID=160x160 (Area=25600) ---"
srun -n 16 ./predator_sim_g160