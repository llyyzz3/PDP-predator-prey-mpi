/**
 * actor_framework.c
 *
 * Implementation of the generic Actor Pattern Framework.
 *
 * This file contains NO model-specific logic.  All predator-prey behaviour
 * lives in predator_prey_model.c and is injected via the registered callbacks.
 */

#include "actor_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * Framework global state
 * ========================================================================= */

int        fw_rank          = 0;
int        fw_size          = 1;
int        fw_total_actors  = 0;
int        fw_rows_per_actor = 0;
int        fw_grid_cols     = 0;
int        fw_actors_per_ue = 1;
Actor      fw_actors[MAX_ACTORS_PER_UE];
MPI_Datatype MPI_ACTOR_MSG;

/* Registered model callbacks (set by framework_register_callbacks). */
static ModelMoveFn    g_move_fn    = NULL;
static ModelEcologyFn g_ecology_fn = NULL;
static ModelMailboxFn g_mailbox_fn = NULL;
static ModelCountFn   g_count_fn   = NULL;

/* MPI send/receive buffers for inter-process Actor migration. */
static ActorMsg send_up_buf[MAX_MSG_BUF];
static ActorMsg send_down_buf[MAX_MSG_BUF];
static int      send_up_count   = 0;
static int      send_down_count = 0;

static ActorMsg recv_up_buf[MAX_MSG_BUF];
static ActorMsg recv_down_buf[MAX_MSG_BUF];
static int      recv_up_count   = 0;
static int      recv_down_count = 0;


/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/**
 * setup_mpi_type - Register ActorMsg as an MPI derived datatype.
 *
 * Using a proper MPI struct type avoids padding issues and makes the
 * communication portable across compilers and platforms.
 把我们在 C 语言里定义的结构体 ActorMsg，
 翻译成 MPI 认识的“派生数据类型（Derived Datatype）” MPI_ACTOR_MSG
 */
static void setup_mpi_type(void)
{
    int          blocklengths[3] = {1, 1, 1};
    MPI_Aint     disps[3]        = { offsetof(ActorMsg, type),
                                     offsetof(ActorMsg, x),
                                     offsetof(ActorMsg, y) };
    MPI_Datatype types[3]        = {MPI_INT, MPI_INT, MPI_INT};

    MPI_Type_create_struct(3, blocklengths, disps, types, &MPI_ACTOR_MSG);
    MPI_Type_commit(&MPI_ACTOR_MSG);
}

/**
 * exchange_external_messages - Exchange cross-boundary messages via MPI.
 *
 * Uses non-blocking-free MPI_Sendrecv to communicate with the two
 * neighbouring MPI processes (up = lower rank, down = higher rank).
 当前 MPI 进程（UE）里那些要越界到别的进程的动物发出去，
 并接收别的进程发来的动物
 * After exchange, received messages are re-routed into the appropriate
 * local Actor mailbox.
 */
static void exchange_external_messages(void)
{
    if (fw_size == 1) return;

    int up_nb   = (fw_rank - 1 + fw_size) % fw_size;
    int down_nb = (fw_rank + 1)            % fw_size;

    /* Step 1: exchange message counts so each side knows how much to recv. */
    MPI_Sendrecv(&send_up_count,   1, MPI_INT, up_nb,   10,
                 &recv_down_count, 1, MPI_INT, down_nb, 10,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    MPI_Sendrecv(&send_down_count, 1, MPI_INT, down_nb, 11,
                 &recv_up_count,   1, MPI_INT, up_nb,   11,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    /* Step 2: exchange the actual messages. */
    if (send_up_count > 0 || recv_down_count > 0)
        MPI_Sendrecv(send_up_buf,   send_up_count,   MPI_ACTOR_MSG, up_nb,   12,
                     recv_down_buf, recv_down_count,  MPI_ACTOR_MSG, down_nb, 12,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (send_down_count > 0 || recv_up_count > 0)
        MPI_Sendrecv(send_down_buf, send_down_count, MPI_ACTOR_MSG, down_nb, 13,
                     recv_up_buf,   recv_up_count,   MPI_ACTOR_MSG, up_nb,   13,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    /* Step 3: route received messages into local Actor mailboxes. */
    for (int i = 0; i < recv_down_count; i++)
        framework_route_message(recv_down_buf[i]);
    for (int i = 0; i < recv_up_count; i++)
        framework_route_message(recv_up_buf[i]);
}

/**
 * process_mailboxes - Deliver all queued mailbox messages to Actors.
 *
 * For each Actor, iterates over its mailbox and calls the registered
 * ModelMailboxFn callback so the model can place the entity into its
 * local state.  Clears the mailbox after processing.
 */
static void process_mailboxes(void)
{
    for (int i = 0; i < fw_actors_per_ue; i++) {
        Actor *a = &fw_actors[i];
        for (int m = 0; m < a->mailbox_count; m++) {
            g_mailbox_fn(a, a->mailbox[m]);
        }
        a->mailbox_count = 0;
    }
}

/**
 * run_step - Execute one simulation step across all local Actors.
 *
 * Step sequence:
 *   1. Each Actor runs its model step (move agents, local interactions).
 *      Agents crossing Actor boundaries are buffered via route_message.
 *   2. Cross-boundary messages are exchanged with MPI neighbours.
 *   3. Received messages are delivered to Actor mailboxes via the
 *      ModelMailboxFn callback.
 */
static void run_step(void)
{
    /* Reset MPI send buffers. */
    send_up_count   = 0;
    send_down_count = 0;

    /* 1. Movement phase: each Actor moves its agents locally.
          Agents crossing Actor boundaries are buffered via route_message. */
    for (int i = 0; i < fw_actors_per_ue; i++)
        g_move_fn(&fw_actors[i]);

    /* 2. MPI exchange: send/receive cross-boundary agents between ranks. */
    exchange_external_messages();

    /* 3. Deliver mailbox messages so newly arrived agents join the grid. */
    process_mailboxes();

    /* 4. Ecology phase: predation, death, reproduction.
          Runs after delivery so arriving animals participate this step. */
    for (int i = 0; i < fw_actors_per_ue; i++)
        g_ecology_fn(&fw_actors[i]);
}

/**
 * collect_global_counts - Gather population statistics from all processes.
 *
 * Calls ModelCountFn for every local Actor, sums locally, then reduces
 * across all MPI ranks with MPI_Allreduce.
 *
 * @param global_a  Output: total count A across all processes.
 * @param global_b  Output: total count B across all processes.
 */
static void collect_global_counts(int *global_a, int *global_b)
{
    int local_a = 0, local_b = 0;
    for (int i = 0; i < fw_actors_per_ue; i++) {
        int ca = 0, cb = 0;
        g_count_fn(&fw_actors[i], &ca, &cb);
        local_a += ca;
        local_b += cb;
    }

    int local[2]  = {local_a, local_b};
    int global[2] = {0, 0};
    MPI_Allreduce(local, global, 2, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  //不需要一个中心化的 Master 节点去逐个收集，MPI_Allreduce 是一种极其高效的
  // 树状规约算法
    *global_a = global[0];
    *global_b = global[1];
}


/* =========================================================================
 * Public API implementation
 * ========================================================================= */

int framework_init(int grid_rows, int grid_cols, int actors_per_ue)
{
    MPI_Comm_rank(MPI_COMM_WORLD, &fw_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &fw_size);

    fw_actors_per_ue = actors_per_ue;
    fw_total_actors  = fw_size * actors_per_ue;
    fw_grid_cols     = grid_cols;

    /* Validate that the grid divides evenly. */
    if (grid_rows % fw_total_actors != 0) {
        if (fw_rank == 0)
            fprintf(stderr,
                    "Error: grid_rows (%d) must be divisible by "
                    "total_actors (%d).\n",
                    grid_rows, fw_total_actors);
        return -1;
    }

    fw_rows_per_actor = grid_rows / fw_total_actors;

    /* Initialise each Actor slot. */
    for (int i = 0; i < actors_per_ue; i++) {
        Actor *a        = &fw_actors[i];
        a->id           = fw_rank * actors_per_ue + i;
        a->start_row    = a->id * fw_rows_per_actor;
        a->local_rows   = fw_rows_per_actor;
        a->mailbox_count = 0;
        a->state        = NULL;  /* Model will set this. */
    }

    setup_mpi_type();
    return 0;
}

void framework_register_callbacks(ModelMoveFn    move_fn,
                                  ModelEcologyFn ecology_fn,
                                  ModelMailboxFn mailbox_fn,
                                  ModelCountFn   count_fn)
{
    g_move_fn    = move_fn;
    g_ecology_fn = ecology_fn;
    g_mailbox_fn = mailbox_fn;
    g_count_fn   = count_fn;
}

Actor *framework_get_actor(int local_idx)
{
    if (local_idx < 0 || local_idx >= fw_actors_per_ue) return NULL;
    return &fw_actors[local_idx];
}

void framework_route_message(ActorMsg msg)
{
    /* Determine which Actor owns the destination row. */
    int target_actor_id = msg.x / fw_rows_per_actor;
    int target_rank     = target_actor_id / fw_actors_per_ue;

    if (target_rank == fw_rank) {
        /* ── Internal routing: deliver directly to the local Actor's mailbox.
           No MPI involved — this is the key efficiency gain of the actor
           pattern when multiple Actors share the same UE. */
        int local_idx  = target_actor_id % fw_actors_per_ue;
        Actor *target  = &fw_actors[local_idx];
        if (target->mailbox_count < MAX_MAILBOX) {
            target->mailbox[target->mailbox_count++] = msg;
        } else {
            /* Mailbox full: log a warning. Increase MAX_MAILBOX if needed. */
            if (fw_rank == 0)
                fprintf(stderr,
                        "Warning: mailbox full for actor %d on rank %d.\n",
                        local_idx, fw_rank);
        }
    } else {
        /* ── External routing: buffer for MPI send to the neighbour rank. */
        if (target_rank == (fw_rank - 1 + fw_size) % fw_size) {
            if (send_up_count < MAX_MSG_BUF)
                send_up_buf[send_up_count++] = msg;
        } else if (target_rank == (fw_rank + 1) % fw_size) {
            if (send_down_count < MAX_MSG_BUF)
                send_down_buf[send_down_count++] = msg;
        }
    }
}

void framework_run(int days, int steps_per_day)
{
    double t_start = MPI_Wtime();  // ← 开始计时
    if (fw_rank == 0) printf("Day,Prey,Predators\n");

    for (int day = 0; day < days; day++) {

        /* Run all steps for this day. */
        for (int step = 0; step < steps_per_day; step++)
            run_step();

        /* Collect and print global population counts once per day. */
        int total_a = 0, total_b = 0;
        collect_global_counts(&total_a, &total_b);

        if (fw_rank == 0)
            printf("%d,%d,%d\n", day, total_a, total_b);

        /* Early exit if all animals are extinct. */
        if (total_a == 0 && total_b == 0) {
            if (fw_rank == 0) printf("All animals extinct.\n");
            break;
        }
    }
    double t_end = MPI_Wtime();    // ← 结束计时
    if (fw_rank == 0)
        printf("Time: %.3f seconds\n", t_end - t_start);
}

void framework_finalise(void)
{
    MPI_Type_free(&MPI_ACTOR_MSG);
}