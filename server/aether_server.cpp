#include <arpa/inet.h>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "../buffer_pool_manager_skeleton/async_disk_manager.cpp"
#include "../buffer_pool_manager_skeleton/buffer_pool_manager.cpp"
#include "../buffer_pool_manager_skeleton/disk_manager.cpp"
#include "../buffer_pool_manager_skeleton/io_uring_manager.cpp"
#include "../buffer_pool_manager_skeleton/lru_k_replacer.cpp"
#include "../storage/table_heap.cpp"
#include "session.h"
#include "thread_pool.h"

static constexpr int DEFAULT_PORT = 3490;
static constexpr int BACKLOG = 64;

void HandleClient(int client_fd,
                  BufferPoolManager *bpm,
                  TableHeap *table_heap,
                  BPlusTree<int64_t, RID> *primary_index,
                  TransactionManager *txn_mgr,
                  LogManager *log_mgr) {
    (void)client_fd;
    (void)bpm;
    (void)table_heap;
    (void)primary_index;
    (void)txn_mgr;
    (void)log_mgr;
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    // Ignore SIGPIPE to avoid abrupt process termination when a client disconnects unexpectedly
    std::signal(SIGPIPE, SIG_IGN);

    std::cout << "=================================================================\n";
    std::cout << "          AetherDB TCP Network Server (C++17)                    \n";
    std::cout << "  Listening on 0.0.0.0:" << port << " | Thread Pool Size: 8      \n";
    std::cout << "=================================================================\n";

    const std::string db_file = "aether_server.db";
    const std::string wal_file = "aether_server.wal";

    // 1. Initialize core database subsystems:
    auto disk_mgr = std::make_unique<AsyncDiskManager>(db_file);
    auto replacer = std::make_unique<LRUKReplacer>(100);
    auto bpm = std::make_unique<BufferPoolManager>(100, disk_mgr.get(), replacer.get());
    auto table_heap = std::make_unique<TableHeap>(bpm.get());
    BPlusTree<int64_t, RID> primary_index("aether_server_idx.db", bpm.get());
    LogManager log_mgr(wal_file);
    TransactionManager txn_mgr;

    // 2. Initialize worker thread pool:
    ThreadPool pool(8);


    return 0;
}
