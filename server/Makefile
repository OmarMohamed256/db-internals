CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -fsanitize=address -O2
INCLUDES := -I. -I../storage -I../execution -I../btree -I../buffer_pool_manager_skeleton -I../concurrency -I../recovery -I../optimizer
LIBS     := -luring -pthread

STORAGE_SRCS := ../storage/table_heap.cpp
BPM_SRCS     := ../buffer_pool_manager_skeleton/async_disk_manager.cpp \
                ../buffer_pool_manager_skeleton/buffer_pool_manager.cpp \
                ../buffer_pool_manager_skeleton/disk_manager.cpp \
                ../buffer_pool_manager_skeleton/io_uring_manager.cpp \
                ../buffer_pool_manager_skeleton/lru_k_replacer.cpp

.PHONY: all clean test cli server

all: server_test aether_cli aether_server

server_test: server_test.cpp $(STORAGE_SRCS) $(BPM_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LIBS)

aether_cli: aether_cli.cpp $(STORAGE_SRCS) $(BPM_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LIBS)

aether_server: aether_server.cpp $(STORAGE_SRCS) $(BPM_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LIBS)

test: server_test
	./server_test

cli: aether_cli

server: aether_server

clean:
	rm -f server_test aether_cli aether_server *.db *.wal
