# Compiler and Flags
CC      := gcc
CFLAGS  := -Wall -Wextra -pthread -g -std=c17 -D_GNU_SOURCE -O2
INCLUDES:= -I.

# Wildcard discovery
SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)
TARGETS := server threaded_server stress_test

# Default target: build all executables
.PHONY: all
all: $(TARGETS)

# Pattern rule: compile any .c file into .o object file
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Link individual executables
server: server.o
	$(CC) $(CFLAGS) $^ -o $@

threaded_server: threaded_server.o
	$(CC) $(CFLAGS) $^ -o $@

stress_test: stress_test.o
	$(CC) $(CFLAGS) $^ -o $@

# Clean target
.PHONY: clean
clean:
	rm -f *.o $(TARGETS)

# Valgrind target
.PHONY: valgrind
valgrind: server
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./server

# Help target
.PHONY: help test-storage test-btree test-exec test-opt test-all

test-storage:
	$(MAKE) -C storage test

test-btree:
	$(MAKE) -C btree test

test-exec:
	$(MAKE) -C execution test

test-opt:
	$(MAKE) -C optimizer test

test-all: test-btree test-storage test-exec test-opt

help:
	@echo "Available Makefile targets:"
	@echo "  make              - Build all binaries (server, threaded_server, stress_test)"
	@echo "  make server       - Build only server"
	@echo "  make test-storage - Build and run TableHeap & PageGuard tests (ASan)"
	@echo "  make test-btree   - Build and run B+ Tree tests (ASan)"
	@echo "  make test-exec    - Build and run Volcano Execution Engine tests (ASan)"
	@echo "  make test-opt     - Build and run Cost-Based Optimizer & DP Join tests (ASan)"
	@echo "  make test-all     - Run B+ Tree, Storage, Execution, and Optimizer test suites"
	@echo "  make clean        - Delete build artifacts and binaries"
	@echo "  make valgrind     - Run Valgrind on server binary"