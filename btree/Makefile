CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -fsanitize=address -O2
INCLUDES := -I. -I../buffer_pool_manager_skeleton
LIBS     := -luring -pthread

BPM_SRCS := ../buffer_pool_manager_skeleton/async_disk_manager.cpp \
            ../buffer_pool_manager_skeleton/buffer_pool_manager.cpp \
            ../buffer_pool_manager_skeleton/disk_manager.cpp \
            ../buffer_pool_manager_skeleton/io_uring_manager.cpp \
            ../buffer_pool_manager_skeleton/lru_k_replacer.cpp

HDRS      := $(wildcard *.h) $(wildcard ../buffer_pool_manager_skeleton/*.h)
TEST_SRCS := b_plus_tree_test.cpp $(BPM_SRCS)
TARGET    := b_plus_tree_test

.PHONY: all clean test test1 test2 test3 test4 test5 test6 test7 test8 test9 test10 test11 test12 test_tsan

all: $(TARGET)

$(TARGET): $(TEST_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TEST_SRCS) -o $@ $(LIBS)

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

test5: $(TARGET)
	./$(TARGET) 5

test6: $(TARGET)
	./$(TARGET) 6

test7: $(TARGET)
	./$(TARGET) 7

test8: $(TARGET)
	./$(TARGET) 8

test9: $(TARGET)
	./$(TARGET) 9

test10: $(TARGET)
	./$(TARGET) 10

test11: $(TARGET)
	./$(TARGET) 11

test12: $(TARGET)
	./$(TARGET) 12

test_tsan:
	$(CXX) -std=c++17 -Wall -Wextra -g -fsanitize=thread -O2 $(INCLUDES) $(TEST_SRCS) -o $(TARGET)_tsan $(LIBS)
	./$(TARGET)_tsan 7
	rm -f $(TARGET)_tsan

clean:
	rm -f $(TARGET) $(TARGET)_tsan *.db
