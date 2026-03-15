#!/bin/bash
#SBATCH --job-name=actor_granularity
#SBATCH --nodes=2
#SBATCH --exclusive
#SBATCH --ntasks-per-node=4
#SBATCH --time=00:30:00
#SBATCH --account=m25oc
#SBATCH --partition=standard
#SBATCH --qos=standard

module load mpt

# Fixed: 4 MPI processes, GRID=160, same problem size throughout
# Variable: ACTORS_PER_UE = 1, 2, 4, 8
# Purpose: measure overhead of internal routing at increasing granularity

GRID=160
PREY=256000
PRED=6400

echo "================================================"
echo "Actor Granularity Test"
echo "Fixed: 4 MPI processes, GRID=${GRID}"
echo "Variable: ACTORS_PER_UE = 1, 2, 4, 8"
echo "================================================"

# Compile four versions, one per granularity level
mpicc -O2 -DGRID_SIZE=${GRID} -DINITIAL_PREY=${PREY} -DINITIAL_PREDATORS=${PRED} \
    -DACTORS_PER_UE=1 \
    main.c actor_framework.c predator_model.c -o predator_sim_ape1

mpicc -O2 -DGRID_SIZE=${GRID} -DINITIAL_PREY=${PREY} -DINITIAL_PREDATORS=${PRED} \
    -DACTORS_PER_UE=2 \
    main.c actor_framework.c predator_model.c -o predator_sim_ape2

mpicc -O2 -DGRID_SIZE=${GRID} -DINITIAL_PREY=${PREY} -DINITIAL_PREDATORS=${PRED} \
    -DACTORS_PER_UE=4 \
    main.c actor_framework.c predator_model.c -o predator_sim_ape4

mpicc -O2 -DGRID_SIZE=${GRID} -DINITIAL_PREY=${PREY} -DINITIAL_PREDATORS=${PRED} \
    -DACTORS_PER_UE=8 \
    main.c actor_framework.c predator_model.c -o predator_sim_ape8

echo ""
echo "--- ACTORS_PER_UE=1 (4 total actors, 40 rows/actor) ---"
srun -n 4 ./predator_sim_ape1

echo ""
echo "--- ACTORS_PER_UE=2 (8 total actors, 20 rows/actor) ---"
srun -n 4 ./predator_sim_ape2

echo ""
echo "--- ACTORS_PER_UE=4 (16 total actors, 10 rows/actor) ---"
srun -n 4 ./predator_sim_ape4

echo ""
echo "--- ACTORS_PER_UE=8 (32 total actors, 5 rows/actor) ---"
srun -n 4 ./predator_sim_ape8
```
