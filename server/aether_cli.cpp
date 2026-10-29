#include <iostream>
#include <memory>
#include <string>

#include "../buffer_pool_manager_skeleton/async_disk_manager.cpp"
#include "../buffer_pool_manager_skeleton/buffer_pool_manager.cpp"
#include "../buffer_pool_manager_skeleton/disk_manager.cpp"
#include "../buffer_pool_manager_skeleton/io_uring_manager.cpp"
#include "../buffer_pool_manager_skeleton/lru_k_replacer.cpp"
#include "../storage/table_heap.cpp"
#include "session.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    std::cout << "=================================================================\n";
    std::cout << "        AetherDB Interactive SQL Shell (C++17)                  \n";
    std::cout << "  Type 'HELP;' for commands, 'EXIT;' or '\\q' to quit.             \n";
    std::cout << "=================================================================\n";

    const std::string db_file = "aether_cli.db";
    const std::string wal_file = "aether_cli.wal";

    // 1. Initialize core database subsystems:
    auto disk_mgr = std::make_unique<AsyncDiskManager>(db_file);
    auto replacer = std::make_unique<LRUKReplacer>(100);
    auto bpm = std::make_unique<BufferPoolManager>(100, disk_mgr.get(), replacer.get());
    auto table_heap = std::make_unique<TableHeap>(bpm.get());
    BPlusTree<int64_t, RID> primary_index("aether_idx.db", bpm.get());
    LogManager log_mgr(wal_file);
    TransactionManager txn_mgr;

    // 2. Initialize interactive client session:
    ClientSession session(bpm.get(), table_heap.get(), &primary_index, &txn_mgr, &log_mgr);


    std::string line;
    while (true) {
        std::cout << "aetherdb> ";
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line.empty()) continue;

        if (line == "EXIT;" || line == "exit;" || line == "\\q") {
            std::cout << "Bye!\n";
            break;
        }

        std::string output = session.Execute(line);
        std::cout << output;
    }

    return 0;
}
