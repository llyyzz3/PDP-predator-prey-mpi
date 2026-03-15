# =============================================================================
# Makefile - Parallel Predator-Prey Simulation Using Actor Pattern
#
# Targets:
#   make              - build default executable predator_sim
#   make clean        - remove all build artifacts and executables
#
# Simulation parameters can be overridden at compile time using -D flags.
# Example:
#   make GRID=160 PREY=256000 PRED=6400
#
# Target machine: Cirrus HPC Cluster
# Compiler:       mpicc using SGI MPT 
# Load module:    module load mpt
# =============================================================================

CC      = mpicc
CFLAGS  = -O2 -Wall -std=c11
TARGET  = predator_sim
SRCS    = main.c actor_framework.c predator_model.c
OBJS    = $(SRCS:.c=.o)

# Optional parameter overrides for scaling experiments
GRID ?=
PREY ?=
PRED ?=

ifneq ($(GRID),)
    CFLAGS += -DGRID_SIZE=$(GRID)
endif
ifneq ($(PREY),)
    CFLAGS += -DINITIAL_PREY=$(PREY)
endif
ifneq ($(PRED),)
    CFLAGS += -DINITIAL_PREDATORS=$(PRED)
endif

# =============================================================================
.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Header dependencies
main.o:            actor_framework.h predator_model.h
actor_framework.o: actor_framework.h
predator_model.o:  predator_model.h actor_framework.h

clean:
	rm -f $(OBJS) $(TARGET)