# Parallel Predator-Prey Simulation — Actor Pattern

A parallel implementation of a predator-prey simulation using the **Actor
Pattern** with MPI for distributed-memory parallelism.  Written in C for
the Parallel Design Patterns coursework (Part Two).

---

## Project Structure

```
.
├── actor_framework.h      # Generic Actor framework API (model-agnostic)
├── actor_framework.c      # Framework implementation: routing, MPI, mailbox
├── predator_model.h       # Predator-prey model parameters and public API
├── predator_model.c       # Model logic: movement, predation, birth, death
├── main.c                 # Entry point: wires framework and model together
├── Makefile               # Build system
├── submit.sh              # Job submission script (Cirrus)
└── README.md              # This file
```

---

## Design Overview

The code is split into two clearly separated layers:

**Actor Framework** (`actor_framework.h/c`)
- Manages Actor lifecycle, mailboxes, and message routing
- Handles all MPI communication between processes
- Completely model-agnostic — contains no predator-prey logic
- Model behaviour is injected via four callback functions:
  - `ModelMoveFn`    — movement phase (before MPI exchange)
  - `ModelEcologyFn` — ecology phase (after MPI exchange)
  - `ModelMailboxFn` — handles incoming messages
  - `ModelCountFn`   — reports population counts

**Predator-Prey Model** (`predator_model.h/c`)
- Implements Lotka-Volterra predator-prey dynamics on a 2D grid
- Each Actor owns a horizontal band of the global grid
- Registers its callbacks with the framework via `model_register()`

Each MPI process (UE) manages `ACTORS_PER_UE` Actors.  Messages between
Actors on the same process are delivered directly to the mailbox without
any MPI overhead (internal routing).  Cross-process messages are exchanged
via `MPI_Sendrecv`.

---

## Requirements

- MPI library (SGI MPT on Cirrus, or OpenMPI locally)
- C compiler with C11 support
- `GRID_SIZE` must be divisible by `num_processes × ACTORS_PER_UE`

---

## Building

**On Cirrus:**
```bash
module load mpt
make
```

**Locally (with OpenMPI):**
```bash
# Install OpenMPI first: brew install open-mpi (Mac) or apt install libopenmpi-dev (Ubuntu)
make
```

**With custom parameters:**
```bash
make GRID=160 PREY=256000 PRED=6400
```

---

## Running

**Locally:**
```bash
mpirun -n 4 ./predator_sim
```

**On Cirrus (via Slurm):**
```bash
sbatch submit.sh
```

Check output:
```bash
cat slurm-*.out
```

---

## Simulation Parameters

All parameters are defined in `predator_model.h` and can be overridden
at compile time with `-D` flags:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `GRID_SIZE` | 40 | Width and height of the global grid |
| `MAX_PER_CELL` | 200 | Max animals per cell (prey and predators separate) |
| `INITIAL_PREY` | 16000 | Initial prey population |
| `INITIAL_PREDATORS` | 400 | Initial predator population |
| `STEPS_PER_DAY` | 50 | Simulation steps per day |
| `DAYS` | 20 | Number of days to simulate |
| `ACTORS_PER_UE` | 2 | Number of Actors per MPI process |

**Lotka-Volterra parameters** (in `predator_model.c`):

| Parameter | Value | Description |
|-----------|-------|-------------|
| `ALPHA` | 0.06 | Prey birth probability per step |
| `BETA` | 0.01 | Predator hunt success probability per step |
| `DELTA` | 0.1 | Predator birth probability after successful hunt |
| `GAMMA` | 0.04 | Predator natural death probability per step |

---

## Output Format

The simulation prints population counts in CSV format once per day:

```
Day,Prey,Predators
0,16000,400
1,18432,812
...
Total simulation time: 3.214 seconds
```

---

## Scaling Experiments

For strong scaling (fixed problem, increasing processes):
```bash
# Compile once
make GRID=160 PREY=256000 PRED=6400 TARGET=predator_sim_strong

# Run with 1, 2, 4, 8 processes
mpirun -n 1 ./predator_sim_strong
mpirun -n 4 ./predator_sim_strong
mpirun -n 8 ./predator_sim_strong
```

For weak scaling (fixed work per process):
```bash
make GRID=40  PREY=16000  PRED=400  TARGET=predator_sim_g40
make GRID=80  PREY=64000  PRED=1600 TARGET=predator_sim_g80
make GRID=160 PREY=256000 PRED=6400 TARGET=predator_sim_g160

mpirun -n 1  ./predator_sim_g40
mpirun -n 4  ./predator_sim_g80
mpirun -n 16 ./predator_sim_g160
```

---

## Constraint

`GRID_SIZE` must satisfy: `GRID_SIZE % (num_processes × ACTORS_PER_UE) == 0`

Valid combinations with `ACTORS_PER_UE=2`:

| Processes | Min GRID_SIZE divisor | Example GRID_SIZE |
|-----------|----------------------|-------------------|
| 1 | 2 | 40 |
| 2 | 4 | 40, 80 |
| 4 | 8 | 40, 80, 160 |
| 8 | 16 | 80, 160 |
| 16 | 32 | 160 |