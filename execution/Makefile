CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -fsanitize=address -O2
INCLUDES := -I. -I../storage -I../btree -I../buffer_pool_manager_skeleton
LIBS     := -luring -pthread

BPM_SRCS := ../buffer_pool_manager_skeleton/async_disk_manager.cpp \
            ../buffer_pool_manager_skeleton/buffer_pool_manager.cpp \
            ../buffer_pool_manager_skeleton/disk_manager.cpp \
            ../buffer_pool_manager_skeleton/io_uring_manager.cpp \
            ../buffer_pool_manager_skeleton/lru_k_replacer.cpp

STORAGE_SRCS := ../storage/table_heap.cpp

HDRS    := $(wildcard *.h) $(wildcard ../storage/*.h) $(wildcard ../btree/*.h) $(wildcard ../buffer_pool_manager_skeleton/*.h)
SRCS     := execution_test.cpp $(STORAGE_SRCS) $(BPM_SRCS)
TARGET   := execution_test

.PHONY: all clean test test1 test2 test3 test4

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $@ $(LIBS)

test: $(TARGET)
	./$(TARGET)

test1: $(TARGET)
	./$(TARGET) 1

test2: $(TARGET)
	./$(TARGET) 2

test3: $(TARGET)
	./$(TARGET) 3

test4: $(TARGET)
	./$(TARGET) 4

clean:
	rm -f $(TARGET) *.db
