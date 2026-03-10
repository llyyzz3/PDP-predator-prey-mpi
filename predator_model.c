/**
 * predator_prey_model.c
 *
 * Predator-prey simulation model using the Actor Pattern Framework.
 *
 * This file contains ALL model-specific logic:
 *   - Animal and Cell data structures
 *   - Lotka-Volterra parameters
 *   - Movement, predation, death and reproduction rules
 *   - Population initialisation
 *
 * It interacts with the framework exclusively through the public API
 * defined in actor_framework.h.  No MPI calls appear here — cross-Actor
 * communication is handled entirely by framework_route_message().
 */

#include "predator_model.h"
#include "actor_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* =========================================================================
 * Lotka-Volterra model parameters
 * ========================================================================= */

/** Prey birth probability per step. */
#define ALPHA  0.06

/** Predator-eats-prey probability per step when sharing a cell. */
#define BETA   0.01

/** Probability a predator reproduces after a successful hunt. */
#define DELTA  0.1

/** Predator natural death probability per step. */
#define GAMMA  0.04


/* =========================================================================
 * Data structures (model-private)
 * ========================================================================= */

/** Species identifiers used in ActorMsg.type. */
#define TYPE_PREY      0
#define TYPE_PREDATOR  1

/**
 * Animal - a single agent in the simulation.
 *
 * Stored in a singly-linked list hanging off each Cell.  The has_moved
 * flag prevents an animal being stepped twice in the same simulation step
 * when it moves forward in the iteration order.
 */
typedef struct Animal {
    int          type;       /**< TYPE_PREY or TYPE_PREDATOR. */
    int          x, y;       /**< Current global grid coordinates. */
    bool         has_moved;  /**< Set to true once this animal has moved. */
    struct Animal *next;
} Animal;

/**
 * Cell - one grid cell owned by an Actor.
 *
 * Maintains separate linked lists for prey and predators plus a combined
 * occupancy count used to enforce the per-cell capacity limit.
 */
typedef struct {
    Animal *preys;
    Animal *predators;
    int     count;   /**< Total animals (prey + predators) in this cell. */
} Cell;

/**
 * ModelState - the model-specific data attached to each Actor.
 *
 * The framework stores a void* pointer (actor->state) which is cast to
 * this type inside the model callbacks.
 */
typedef struct {
    Cell **grid;      /**< 2-D array: grid[local_row][col]. */
    int    prey_count;
    int    pred_count;
} ModelState;


/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/** Return a uniform random double in [0, 1). */
static inline double rand_double(void)
{
    return rand() / (double)RAND_MAX;
}

/**
 * get_state - Safely retrieve the ModelState from an Actor.
 */
static inline ModelState *get_state(Actor *a)
{
    return (ModelState *)a->state;
}

/**
 * add_animal - Place a new animal into a cell of the given Actor.
 *
 * Returns false (and frees nothing) if the cell is at capacity.
 * The cell capacity limit (MAX_PER_CELL) is a model-level constraint.
 */
static bool add_animal(Actor *a, int type, int global_x, int global_y)
{
    ModelState *s       = get_state(a);
    int         local_x = global_x - a->start_row;

    if (local_x < 0 || local_x >= a->local_rows) return false;
    // if (s->grid[local_x][global_y].count >= MAX_PER_CELL)  return false;
    // 修改后：各自独立判断
    if (type == TYPE_PREY) {
        // 统计当前格子里的猎物数量
        int prey_count = 0;
        Animal *a = s->grid[local_x][global_y].preys;
        while (a) { prey_count++; a = a->next; }
        if (prey_count >= MAX_PER_CELL) return false;
    } else {
        int pred_count = 0;
        Animal *a = s->grid[local_x][global_y].predators;
        while (a) { pred_count++; a = a->next; }
        if (pred_count >= MAX_PER_CELL) return false;
    }

    Animal *anim   = malloc(sizeof(Animal));
    anim->type     = type;
    anim->x        = global_x;
    anim->y        = global_y;
    anim->has_moved = false;

    Cell *cell = &s->grid[local_x][global_y];
    if (type == TYPE_PREY) {
        anim->next  = cell->preys;
        cell->preys = anim;
        s->prey_count++;
    } else {
        anim->next       = cell->predators;
        cell->predators  = anim;
        s->pred_count++;
    }
    cell->count++;
    return true;
}


/* =========================================================================
 * Model step callback — movement
 *
 * Each animal attempts to move one step in a random direction.
 * If the destination cell belongs to a different Actor, the animal is
 * removed locally and dispatched via framework_route_message().
 * ========================================================================= */

static void move_actor(Actor *a)
{
    ModelState *s = get_state(a);

    /* Clear has_moved flags before the movement pass. */
    for (int i = 0; i < a->local_rows; i++)
        for (int j = 0; j < fw_grid_cols; j++) {
            for (Animal *an = s->grid[i][j].preys;      an; an = an->next) an->has_moved = false;
            for (Animal *an = s->grid[i][j].predators;  an; an = an->next) an->has_moved = false;
        }

    /* Process prey then predators. */
    for (int species = 0; species <= 1; species++) {
        int current_type = (species == 0) ? TYPE_PREY : TYPE_PREDATOR;

        for (int i = 0; i < a->local_rows; i++) {
            for (int j = 0; j < fw_grid_cols; j++) {

                Animal **ptr = (current_type == TYPE_PREY)
                               ? &s->grid[i][j].preys
                               : &s->grid[i][j].predators;

                while (*ptr) {
                    Animal *anim = *ptr;

                    if (anim->has_moved) {
                        ptr = &(*ptr)->next;
                        continue;
                    }

                    /* Random ±1 step (toroidal wrap). */
                    int dx   = (rand() % 3) - 1;
                    int dy   = (rand() % 3) - 1;
                    int newX = (anim->x + dx + fw_grid_cols) % fw_grid_cols;
                    int newY = (anim->y + dy + fw_grid_cols) % fw_grid_cols;

                    int target_actor_id = newX / fw_rows_per_actor;

                    if (target_actor_id == a->id) {
                        /* ── Stays inside this Actor's territory. */
                        int new_local_x = newX - a->start_row;
                        Cell *dest      = &s->grid[new_local_x][newY];

                        if ((newX != anim->x || newY != anim->y)
                            && dest->count < MAX_PER_CELL)
                        {
                            /* Move animal to destination cell. */
                            *ptr = anim->next;
                            s->grid[i][j].count--;

                            anim->x        = newX;
                            anim->y        = newY;
                            anim->has_moved = true;

                            if (current_type == TYPE_PREY) {
                                anim->next  = dest->preys;
                                dest->preys = anim;
                            } else {
                                anim->next       = dest->predators;
                                dest->predators  = anim;
                            }
                            dest->count++;
                        } else {
                            /* Destination full or same cell: stay put. */
                            anim->has_moved = true;
                            ptr = &(*ptr)->next;
                        }

                    } else {
                        /* ── Crosses into another Actor's territory.
                           Remove from local grid and dispatch via the
                           framework router — no direct MPI calls here. */
                        *ptr = anim->next;
                        s->grid[i][j].count--;
                        if (current_type == TYPE_PREY) s->prey_count--;
                        else                           s->pred_count--;

                        ActorMsg msg = { current_type, newX, newY };
                        framework_route_message(msg);
                        free(anim);
                    }
                }
            }
        }
    }
}


/* =========================================================================
 * Ecological interaction callbacks
 * ========================================================================= */

/**
 * resolve_predation - Predators attempt to eat prey in the same cell.
 *
 * Each predator makes one attempt per step.  On a successful hunt the
 * prey is removed; the predator may immediately reproduce.
 */
static void resolve_predation(Actor *a)
{
    ModelState *s = get_state(a);

    for (int i = 0; i < a->local_rows; i++) {
        for (int j = 0; j < fw_grid_cols; j++) {
            Cell *cell = &s->grid[i][j];

            Animal **pred_ptr = &cell->predators;
            while (*pred_ptr) {
                Animal *pred     = *pred_ptr;
                Animal **prey_ptr = &cell->preys;

                while (*prey_ptr) {
                    if (rand_double() < BETA) {
                        /* Prey is eaten. */
                        Animal *prey = *prey_ptr;
                        cell->count--;
                        *prey_ptr = prey->next;
                        free(prey);
                        s->prey_count--;

                        /* Predator may reproduce. */
                        if (rand_double() < DELTA)
                            add_animal(a, TYPE_PREDATOR, pred->x, pred->y);

                        break;  /* One meal per predator per step. */
                    }
                    prey_ptr = &(*prey_ptr)->next;
                }
                pred_ptr = &(*pred_ptr)->next;
            }
        }
    }
}

/**
 * apply_predator_death - Each predator dies with probability GAMMA per step.
 */
static void apply_predator_death(Actor *a)
{
    ModelState *s = get_state(a);

    for (int i = 0; i < a->local_rows; i++) {
        for (int j = 0; j < fw_grid_cols; j++) {
            Animal **ptr = &s->grid[i][j].predators;
            while (*ptr) {
                Animal *anim = *ptr;
                if (rand_double() < GAMMA) {
                    s->grid[i][j].count--;
                    *ptr = anim->next;
                    free(anim);
                    s->pred_count--;
                } else {
                    ptr = &(*ptr)->next;
                }
            }
        }
    }
}

/**
 * reproduce_prey - Each prey reproduces with probability ALPHA per step.
 */
static void reproduce_prey(Actor *a)
{
    ModelState *s = get_state(a);

    for (int i = 0; i < a->local_rows; i++) {
        for (int j = 0; j < fw_grid_cols; j++) {
            /* Snapshot the list end to avoid processing newborns this step. */
            Animal *anim = s->grid[i][j].preys;
            while (anim) {
                Animal *next = anim->next;
                if (rand_double() < ALPHA)
                    add_animal(a, TYPE_PREY, anim->x, anim->y);
                anim = next;
            }
        }
    }
}


/* =========================================================================
 * Framework callbacks (registered with framework_register_callbacks)
 * ========================================================================= */

/**
 * model_move - Movement callback (ModelMoveFn).
 *
 * Called by the framework BEFORE MPI exchange each step.
 * Moves all animals in the Actor; those crossing territory boundaries are
 * dispatched to framework_route_message() and removed from local state.
 */
static void model_move(Actor *a)
{
    move_actor(a);
}

/**
 * model_ecology - Ecology callback (ModelEcologyFn).
 *
 * Called by the framework AFTER MPI exchange and mailbox delivery each step.
 * Applies predation, predator natural death, and prey reproduction.
 * Running ecology after delivery means animals that just arrived from a
 * neighbouring Actor participate in this step's interactions.
 */
static void model_ecology(Actor *a)
{
    resolve_predation(a);
    apply_predator_death(a);
    reproduce_prey(a);
}

/**
 * model_deliver_message - Mailbox callback (ModelMailboxFn).
 *
 * Called by the framework for every message arriving in an Actor's mailbox
 * after MPI exchange.  Places the incoming animal into the Actor's grid.
 */
static void model_deliver_message(Actor *a, ActorMsg msg)
{
    add_animal(a, msg.type, msg.x, msg.y);
}

/**
 * model_get_counts - Count callback (ModelCountFn).
 *
 * Reports prey and predator populations to the framework for global
 * aggregation and daily output.
 */
static void model_get_counts(const Actor *a, int *count_a, int *count_b)
{
    const ModelState *s = (const ModelState *)a->state;
    *count_a = s->prey_count;
    *count_b = s->pred_count;
}


/* =========================================================================
 * Public model API
 * ========================================================================= */

void model_init(int initial_prey, int initial_predators)
{
    /* Allocate and attach ModelState to each Actor. */
    for (int i = 0; i < fw_actors_per_ue; i++) {
        Actor      *a = framework_get_actor(i);
        ModelState *s = malloc(sizeof(ModelState));
        s->prey_count = 0;
        s->pred_count = 0;

        /* Allocate the 2-D cell grid for this Actor's row band. */
        s->grid = malloc(a->local_rows * sizeof(Cell *));
        for (int r = 0; r < a->local_rows; r++) {
            s->grid[r] = calloc(fw_grid_cols, sizeof(Cell));
        }

        a->state = s;
    }

    /* All processes run the same RNG sequence from the same seed, but
       each only keeps animals that fall inside its own row band.
       This guarantees a consistent global initial placement regardless
       of the number of MPI processes. */
    srand(MODEL_SEED);

    for (int i = 0; i < initial_prey; i++) {
        int gx = rand() % fw_grid_cols;
        int gy = rand() % fw_grid_cols;
        int target_actor = gx / fw_rows_per_actor;
        int target_rank  = target_actor / fw_actors_per_ue;
        if (target_rank == fw_rank) {
            int local_idx = target_actor % fw_actors_per_ue;
            add_animal(framework_get_actor(local_idx), TYPE_PREY, gx, gy);
        }
    }

    for (int i = 0; i < initial_predators; i++) {
        int gx = rand() % fw_grid_cols;
        int gy = rand() % fw_grid_cols;
        int target_actor = gx / fw_rows_per_actor;
        int target_rank  = target_actor / fw_actors_per_ue;
        if (target_rank == fw_rank) {
            int local_idx = target_actor % fw_actors_per_ue;
            add_animal(framework_get_actor(local_idx), TYPE_PREDATOR, gx, gy);
        }
    }

    /* Switch to a per-rank seed so each process evolves differently. */
    srand(MODEL_SEED + fw_rank);
}

void model_register(void)
{
    framework_register_callbacks(model_move,
                                 model_ecology,
                                 model_deliver_message,
                                 model_get_counts);
}