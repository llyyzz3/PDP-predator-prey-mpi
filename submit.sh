#!/bin/bash
#SBATCH --job-name=predator_sim
#SBATCH --time=00:05:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=4
#SBATCH --account=m25oc
#SBATCH --partition=standard
#SBATCH --qos=standard

# Load the recommended MPI environment for Cirrus HPC cluster
module load mpt

# Ensure the script is executed in the current working directory
echo "Current working directory: $(pwd)"
echo "Starting distributed predator prey MPI simulation..."

# Run the compiled executable using srun
srun --unbuffered ./predator_sim