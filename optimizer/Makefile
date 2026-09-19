CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -fsanitize=address -O2
INCLUDES := -I. -I../storage -I../btree -I../execution -I../buffer_pool_manager_skeleton
LIBS     := -pthread

HDRS     := $(wildcard *.h)
SRCS     := optimizer_test.cpp
TARGET   := optimizer_test

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
	rm -f $(TARGET)
