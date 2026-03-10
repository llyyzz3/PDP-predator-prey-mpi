
/**
 * actor_framework.h
 *
 * Generic Actor Pattern Framework for distributed simulations using MPI.
 *
 * This framework implements the actor pattern: each MPI process (UE) manages
 * one or more Actors. Actors communicate via message passing — within the same
 * UE messages are delivered directly to the target Actor's mailbox; across UE
 * boundaries they are exchanged via MPI.
 *
 * The framework is fully model-agnostic. Model-specific behaviour (movement,
 * birth, death, etc.) is injected by the user through callback functions.
 *
 * Usage:
 *   1. Call framework_init() after MPI_Init.
 *   2. Register callbacks with framework_register_callbacks().
 *   3. Populate Actor state via framework_get_actor() and actor->state.
 *   4. Call framework_run() to execute the simulation.
 *   5. Call framework_finalise() before MPI_Finalize.
 */

#ifndef ACTOR_FRAMEWORK_H
#define ACTOR_FRAMEWORK_H

#include <mpi.h>
#include <stdbool.h>

/* =========================================================================
 * Configuration constants
 * ========================================================================= */

/** Maximum number of Actors per MPI process (UE). */
#define MAX_ACTORS_PER_UE 16

/** Maximum number of messages an Actor's mailbox can hold per step. */
#define MAX_MAILBOX 50000

/** Maximum number of messages in the MPI send/recv buffers per step. */
#define MAX_MSG_BUF 50000


/* =========================================================================
 * Generic message type
 *
 * ActorMsg is the unit of communication between Actors. The 'type' field
 * carries model-defined meaning (e.g. species ID). 'x' and 'y' are the
 * global grid coordinates of the destination cell.
 * ========================================================================= */
typedef struct {
    int type;   /**< Model-defined message type (e.g. species). */
    int x;      /**< Global row coordinate of destination cell.  */
    int y;      /**< Global column coordinate of destination cell. */
} ActorMsg;


/* =========================================================================
 * Actor struct
 *
 * Each Actor owns a contiguous horizontal band of the global grid.
 * Model-specific data (grid cells, animal lists, counts, etc.) is stored
 * in the opaque 'state' pointer — the framework never inspects it.
 * ========================================================================= */
typedef struct {
    int id;           /**< Global Actor ID (0 … total_actors-1). */
    int start_row;    /**< First global row this Actor is responsible for. */
    int local_rows;   /**< Number of rows this Actor owns. */

    /** Mailbox: incoming messages queued for processing this step. */
    ActorMsg mailbox[MAX_MAILBOX];
    int      mailbox_count;

    /** Opaque pointer to model-specific state (e.g. Cell grid, counters). */
    void *state;
} Actor;


/* =========================================================================
 * Callback function types
 *
 * The model injects behaviour by providing implementations of these
 * function-pointer types and registering them with the framework.
 * ========================================================================= */

/**
 * ModelMoveFn - called once per Actor BEFORE MPI exchange.
 *
 * The model should perform movement here only.  When an agent crosses into
 * another Actor's territory the model calls framework_route_message() to
 * dispatch it — the framework handles delivery and MPI exchange transparently.
 *
 * @param actor  Pointer to the Actor being stepped.
 */
typedef void (*ModelMoveFn)(Actor *actor);

/**
 * ModelEcologyFn - called once per Actor AFTER MPI exchange and mailbox delivery.
 *
 * The model should perform all ecological interactions here (predation,
 * birth, death).  This ordering ensures that animals arriving from
 * neighbouring Actors this step are included in the ecology phase,
 * matching the behaviour of the original serial code.
 *
 * @param actor  Pointer to the Actor being stepped.
 */
typedef void (*ModelEcologyFn)(Actor *actor);

/**
 * ModelMailboxFn - called for each message delivered to an Actor.
 *
 * After MPI exchange, the framework delivers each received message by
 * calling this function.  The model should place the incoming entity
 * into the Actor's local state.
 *
 * @param actor  The receiving Actor.
 * @param msg    The incoming message.
 */
typedef void (*ModelMailboxFn)(Actor *actor, ActorMsg msg);

/**
 * ModelCountFn - called once per Actor per day to gather global statistics.
 *
 * The model should write the current population counts into *count_a and
 * *count_b.  The framework sums these across all processes with
 * MPI_Allreduce and returns the totals.
 *
 * @param actor    The Actor being queried.
 * @param count_a  Output: first population count  (e.g. prey).
 * @param count_b  Output: second population count (e.g. predators).
 */
typedef void (*ModelCountFn)(const Actor *actor, int *count_a, int *count_b);


/* =========================================================================
 * Framework state (read-only for the model)
 * ========================================================================= */

/** MPI rank of the current process. */
extern int fw_rank;

/** Total number of MPI processes. */
extern int fw_size;

/** Total number of Actors across all processes. */
extern int fw_total_actors;

/** Number of global grid rows each Actor owns. */
extern int fw_rows_per_actor;

/** Number of columns in the global grid (set via framework_init). */
extern int fw_grid_cols;

/** Number of Actors on this process. */
extern int fw_actors_per_ue;

/** Array of Actors managed by this process. */
extern Actor fw_actors[MAX_ACTORS_PER_UE];

/** MPI datatype for ActorMsg. */
extern MPI_Datatype MPI_ACTOR_MSG;


/* =========================================================================
 * Framework API
 * ========================================================================= */

/**
 * framework_init - Initialise the framework.
 *
 * Must be called after MPI_Init and before any other framework function.
 * Allocates Actor slots and computes the grid decomposition.
 *
 * @param grid_rows      Total number of rows in the global grid.
 * @param grid_cols      Total number of columns in the global grid.
 * @param actors_per_ue  Number of Actors each MPI process should manage.
 *                       grid_rows must be divisible by (mpi_size * actors_per_ue).
 * @return  0 on success, non-zero if the grid cannot be divided evenly.
 */
int framework_init(int grid_rows, int grid_cols, int actors_per_ue);

/**
 * framework_register_callbacks - Register model behaviour callbacks.
 *
 * All three callbacks must be non-NULL.
 *
 * @param step_fn     Called once per Actor per simulation step.
 * @param mailbox_fn  Called for each message arriving in an Actor's mailbox.
 * @param count_fn    Called once per Actor per day to collect statistics.
 */
void framework_register_callbacks(ModelMoveFn    move_fn,
                                  ModelEcologyFn ecology_fn,
                                  ModelMailboxFn mailbox_fn,
                                  ModelCountFn   count_fn);

/**
 * framework_get_actor - Retrieve a local Actor by its local index.
 *
 * @param local_idx  Index in [0, actors_per_ue).
 * @return Pointer to the Actor, or NULL if out of range.
 */
Actor *framework_get_actor(int local_idx);

/**
 * framework_route_message - Dispatch a message to the correct Actor.
 *
 * If the target Actor lives on this process the message is placed directly
 * into its mailbox (zero MPI overhead).  Otherwise it is buffered for the
 * next MPI exchange.
 *
 * This function is intended to be called from inside the ModelStepFn
 * callback when an entity crosses an Actor boundary.
 *
 * @param msg  The message to route (msg.x determines the target Actor).
 */
void framework_route_message(ActorMsg msg);

/**
 * framework_run - Execute the full simulation.
 *
 * Runs for the specified number of days, each consisting of steps_per_day
 * simulation steps.  After every day, population counts are gathered from
 * all processes and printed by rank 0 in CSV format.
 *
 * The simulation stops early if both population counts reach zero.
 *
 * @param days          Number of days to simulate.
 * @param steps_per_day Number of steps per day.
 */
void framework_run(int days, int steps_per_day);

/**
 * framework_finalise - Clean up framework resources.
 *
 * Frees the MPI datatype.  Call before MPI_Finalize.
 */
void framework_finalise(void);

#endif /* ACTOR_FRAMEWORK_H */