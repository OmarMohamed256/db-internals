# Buffer Pool Manager Skeleton

This is a C-flavoured C++ skeleton for building your Phase 2.1 Buffer Pool Manager.
It follows the standard CMU 15-445 architecture structure.

## Files included:
1. `page.h`: Represents a standard 4KB database memory page.
2. `disk_manager.h`: The interface connecting memory to file I/O operations.
3. `replacer.h`: Abstract class for your eviction policy (e.g., LRU-K or Clock).
4. `buffer_pool_manager.h`: The header outlining the core logic API.
5. `buffer_pool_manager.cpp`: The source file containing the TODO blocks detailing exactly how to implement Fetch, Unpin, Flush, and New page logic.

## Next Steps:
1. Implement `disk_manager.cpp` using basic POSIX system calls (`open`, `pread`, `pwrite`) or C++ fstreams to read/write 4096-byte blocks from a file.
2. Implement a subclass of `Replacer` (e.g., `LRUReplacer` or `ClockReplacer`).
3. Fill in the TODO blocks in `buffer_pool_manager.cpp`.
4. Compile with: `g++ -std=c++17 -Wall -Wextra -g -fsanitize=address *.cpp -o buffer_pool_test`
