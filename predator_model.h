/**
 * predator_prey_model.h
 *
 * Public interface for the predator-prey simulation model.
 *
 * This header exposes only the functions that main.c needs to call.
 * All model internals (Animal, Cell, ModelState) remain private to
 * predator_prey_model.c.
 */

#ifndef PREDATOR_MODEL_H
#define PREDATOR_MODEL_H

/* ── Simulation parameters ───────────────────────────────────────────────── */

/** Width and height of the global grid (must be divisible by total actors). */
#ifndef GRID_SIZE
#define GRID_SIZE         80
#endif

/**
 * Maximum number of animals (prey + predators) allowed in one cell.
 * Increased from 100 to 200 so the grid can sustain the ~55 prey/cell
 * density required for predators to survive (mathematical threshold N > 50.8
 * derived from: (1-(1-BETA)^N) * DELTA > GAMMA).
 */
#define MAX_PER_CELL       200

/**
 * Initial prey population (~55 prey/cell average across 40x40 grid).
 * This exceeds the minimum density threshold needed for predator survival.
 */
#ifndef INITIAL_PREY
#define INITIAL_PREY      64000
#endif

/**
 * Initial predator population (1:4 ratio with prey).
 */
#ifndef INITIAL_PREDATORS
#define INITIAL_PREDATORS 1600
#endif

/** Number of simulation steps executed per day. */
#define STEPS_PER_DAY      50

/** Number of days the simulation runs. */
#define DAYS               20

/** Number of Actors managed by each MPI process (UE). */
#define ACTORS_PER_UE      2

/** Fixed RNG seed used for reproducible initial placement. */
#define MODEL_SEED         123456


/* ── Public model functions ──────────────────────────────────────────────── */

/**
 * model_init - Allocate model state and place initial animals.
 *
 * Must be called after framework_init() so that Actor slots and grid
 * dimensions are already set up.
 *
 * @param initial_prey        Number of prey to place at startup.
 * @param initial_predators   Number of predators to place at startup.
 */
void model_init(int initial_prey, int initial_predators);

/**
 * model_register - Register model callbacks with the framework.
 *
 * Must be called before framework_run().
 */
void model_register(void);

#endif /* PREDATOR_MODEL_H */