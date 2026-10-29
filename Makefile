# Root Makefile for AetherDB / DBMS Internals Engine
# Orchestrates building and testing across all engine submodules.

.PHONY: all test test-all test-storage test-btree test-exec test-opt test-concurrency test-recovery clean help

# Default target: run the full test suite
all: test-all

test: test-all

test-storage:
	$(MAKE) -C storage test

test-btree:
	$(MAKE) -C btree test

test-exec:
	$(MAKE) -C execution test

test-opt:
	$(MAKE) -C optimizer test

test-concurrency:
	$(MAKE) -C concurrency test

test-recovery:
	$(MAKE) -C recovery test

test-server:
	$(MAKE) -C server test

cli:
	$(MAKE) -C server cli

server:
	$(MAKE) -C server server

test-all: test-storage test-btree test-exec test-opt test-concurrency test-recovery

clean:
	$(MAKE) -C storage clean
	$(MAKE) -C btree clean
	$(MAKE) -C execution clean
	$(MAKE) -C optimizer clean
	$(MAKE) -C concurrency clean
	$(MAKE) -C recovery clean
	$(MAKE) -C server clean
	rm -f *.db *.wal *.meta *.log

help:
	@echo "================================================================="
	@echo "AetherDB: Production-Grade Disk-Oriented Relational Engine (C++17)"
	@echo "================================================================="
	@echo "Available targets:"
	@echo "  make test-all         - Run all test suites across the entire engine (ASan)"
	@echo "  make test-storage     - TableHeap & Slotted-Page PageGuards"
	@echo "  make test-btree       - Concurrent B+ Tree with Optimistic Lock Coupling"
	@echo "  make test-exec        - Volcano Query Execution Engine (Joins, Aggs, Scans)"
	@echo "  make test-opt         - Selinger System-R Cost-Based Optimizer (DP Joins)"
	@echo "  make test-concurrency - Multi-Version Concurrency Control (MVCC) & 2PL"
	@echo "  make test-recovery    - Write-Ahead Logging & 3-Phase ARIES Crash Recovery"
	@echo "  make clean            - Remove all compiled binaries and test database files"
	@echo "================================================================="