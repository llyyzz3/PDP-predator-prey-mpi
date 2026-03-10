/**
 * main.c
 *
 * Entry point for the parallel predator-prey simulation.
 *
 * This file is intentionally minimal: it initialises MPI, wires together
 * the actor framework and the predator-prey model, runs the simulation,
 * and shuts everything down cleanly.
 *
 * To change the simulation parameters (grid size, number of actors,
 * initial populations, etc.) edit the constants in predator_prey_model.h.
 *
 * Build:
 *   make
 *
 * Run (e.g. 4 MPI processes):
 *   mpirun -n 4 ./predator_prey
 *
 * Constraint:
 *   GRID_SIZE must be divisible by (num_processes × ACTORS_PER_UE).
 */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#include "actor_framework.h"
#include "predator_model.h"

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    /* ── 1. Initialise the actor framework ──────────────────────────────── */
    int err = framework_init(GRID_SIZE, GRID_SIZE, ACTORS_PER_UE);
    if (err != 0) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    /* ── 2. Register model behaviour with the framework ─────────────────── */
    model_register();

    /* ── 3. Populate the simulation with initial animals ────────────────── */
    model_init(INITIAL_PREY, INITIAL_PREDATORS);

    /* ── 4. Run the simulation ──────────────────────────────────────────── */
    framework_run(DAYS, STEPS_PER_DAY);

    /* ── 5. Clean up ────────────────────────────────────────────────────── */
    framework_finalise();
    MPI_Finalize();
    return EXIT_SUCCESS;
}