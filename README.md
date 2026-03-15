# Distributed Predator Prey Simulation Using Actor Pattern

This project implements a distributed predator prey simulation system based on the Actor pattern. The code separates the distributed execution framework from the domain specific ecological model. It supports running multiple Actors per MPI process to achieve finer grained spatial partitioning.

## 1. Target Platform and Compiler Requirements

According to the coursework requirements, this project is explicitly configured and tested for the following computing platform and compiler.

- Target Machine: Cirrus HPC Cluster.

- Compiler: mpicc using the SGI Message Passing Toolkit library.

- Programming Language: C language compliant with the C11 standard.

## 2. Build and Submission Scripts

The project includes the required files for building the code and running the executable on Cirrus compute nodes.

- Makefile: A makefile is included for building the code. It compiles the source files into the final executable named predator_sim.

- submit.sh: A submission script is included to run your executable on Cirrus. It is configured with the appropriate Slurm directives to request compute nodes and execute the parallel simulation.

## 3. Project File Structure

- actor_framework.h and actor_framework.c: These files contain the Actor runtime framework layer. This layer is responsible for Actor lifecycle management, MPI inter process communication, and local or cross process message routing.

- predator_model.h and predator_model.c: These files contain the domain simulation model layer. This layer defines ecological behavior rules such as animal movement, reproduction, predation, and death.

- main.c: The main entry point of the program. It is responsible for initializing MPI and registering model callback functions into the framework.

## 4. How to Build the Code

Before compiling this code on the Cirrus cluster, you must load the SGI MPT module. Please execute the following command in your terminal.

```bash
module load mpt
```

After loading the module, you can compile the code using the included Makefile.

```bash
make
```

**Custom Compilation Parameters:**
You can override macro definitions during compilation to change the initial simulation parameters. This is very useful when conducting performance and scalability tests.

```bash
make GRID=160 PREY=256000 PRED=6400
```

## 5. How to Run the Code

To ensure that the global two dimensional grid can be evenly divided among all Actors, the grid size must be perfectly divisible by the product of the number of processes and the number of actors per process. By default, the framework configures two actors per process.

You can submit the job to the Cirrus compute nodes using the provided submission script.

```bash
sbatch submit.sh
```

If you have already requested an interactive compute node, you can also run the program directly using mpirun. For instance, to run with 4 processes, use the following command.

```bash
mpirun -n 4 ./predator_sim
```

## 6. Output Format

The simulation will output the total number of surviving prey and predators globally to the terminal at the end of each simulation day. 

```bash
Day,Prey,Predators
0,64000,1600
1,68432,1812
...
Time: 25.427 seconds
```
