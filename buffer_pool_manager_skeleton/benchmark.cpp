#include <iostream>
#include <vector>
#include <random>
#include <iomanip>
#include <memory>
#include <cstdio>
#include <atomic>
#include "buffer_pool_manager.h"
#include "lru_k_replacer.h"
#include "async_disk_manager.h"

// A Test Disk Manager that tracks the number of physical disk reads/writes asynchronously
class InstrumentedAsyncDiskManager : public AsyncDiskManager {
public:
    explicit InstrumentedAsyncDiskManager(const std::string &db_file) : AsyncDiskManager(db_file, 128) {}

    void ResetStats() {
        read_count_ = 0;
        write_count_ = 0;
    }

    size_t GetReadCount() const { return read_count_; }
    size_t GetWriteCount() const { return write_count_; }

    void ReadPageAsync(page_id_t page_id, char *page_data, IOCompletionCallback callback) {
        read_count_++;
        AsyncDiskManager::ReadPageAsync(page_id, page_data, std::move(callback));
    }

    void WritePageAsync(page_id_t page_id, const char *page_data, IOCompletionCallback callback) {
        write_count_++;
        AsyncDiskManager::WritePageAsync(page_id, page_data, std::move(callback));
    }

private:
    std::atomic<size_t> read_count_{0};
    std::atomic<size_t> write_count_{0};
};

// Fast Zipfian random distribution generator
class ZipfianGenerator {
public:
    ZipfianGenerator(int n, double alpha = 0.9) : n_(n), alpha_(alpha), dist_(0.0, 1.0) {
        c_ = 0.0;
        for (int i = 1; i <= n_; ++i) {
            c_ += (1.0 / std::pow(static_cast<double>(i), alpha_));
        }
        c_ = 1.0 / c_;

        cdf_.resize(n_ + 1, 0.0);
        double sum = 0.0;
        for (int i = 1; i <= n_; ++i) {
            sum += c_ / std::pow(static_cast<double>(i), alpha_);
            cdf_[i] = sum;
        }
    }

    int Next(std::mt19937 &rng) {
        double r = dist_(rng);
        // Binary search for rank
        int low = 1, high = n_;
        while (low < high) {
            int mid = low + (high - low) / 2;
            if (cdf_[mid] >= r) {
                high = mid;
            } else {
                low = mid + 1;
            }
        }
        return low - 1; // 0-indexed page_id
    }

private:
    int n_;
    double alpha_;
    double c_;
    std::vector<double> cdf_;
    std::uniform_real_distribution<double> dist_;
};

void RunHitRateBenchmark() {
    const int TOTAL_PAGES = 100;     // Database has 100 pages on disk
    const size_t BUFFER_POOL_SIZE = 15; // Buffer pool can only hold 15 frames in RAM (15% capacity)
    const int NUM_REQUESTS = 20000;  // 20,000 page accesses per workload
    const std::string db_file = "benchmark.db";

    std::remove(db_file.c_str());

    std::cout << "===============================================================" << std::endl;
    std::cout << "          BUFFER POOL MANAGER HIT RATE BENCHMARK              " << std::endl;
    std::cout << "===============================================================" << std::endl;
    std::cout << " Database Size on Disk  : " << TOTAL_PAGES << " pages (400 KB)" << std::endl;
    std::cout << " Buffer Pool RAM Size   : " << BUFFER_POOL_SIZE << " frames (" << (BUFFER_POOL_SIZE * 100 / TOTAL_PAGES) << "% of total DB)" << std::endl;
    std::cout << " Accesses per Workload  : " << NUM_REQUESTS << " operations" << std::endl;
    std::cout << " Replacement Policy     : LRU-K (K=2) with FIFO History Queue" << std::endl;
    std::cout << "===============================================================\n" << std::endl;

    auto disk_manager = std::make_unique<InstrumentedAsyncDiskManager>(db_file);
    auto replacer = std::make_unique<LRUKReplacer>(BUFFER_POOL_SIZE);
    auto bpm = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, disk_manager.get(), replacer.get());

    // 1. Pre-allocate the 100 pages on disk
    std::vector<page_id_t> page_ids;
    for (int i = 0; i < TOTAL_PAGES; ++i) {
        page_id_t pid = INVALID_PAGE_ID;
        bool allocated = false;
        bpm->NewPageAsync([&pid, &allocated](Page *p) {
            if (p) pid = p->GetPageId();
            allocated = true;
        });
        while (!allocated) {
            bpm->PollCompletions();
        }
        page_ids.push_back(pid);
        bpm->UnpinPage(pid, true);
    }
    
    bool flushed = false;
    bpm->FlushAllPagesAsync([&flushed](bool success) {
        flushed = success;
    });
    while (!flushed) {
        bpm->PollCompletions();
    }

    std::mt19937 rng(42); // Deterministic seed

    auto ExecuteWorkload = [&](const std::string &workload_name, const std::vector<page_id_t> &access_pattern) {
        disk_manager->ResetStats();

        size_t hits = 0;
        size_t misses = 0;

        for (page_id_t pid : access_pattern) {
            size_t reads_before = disk_manager->GetReadCount();
            bool done = false;
            bpm->FetchPageAsync(pid, [&reads_before, &disk_manager, &done, &misses, &hits](Page *page) {
                if (page != nullptr) {
                    size_t reads_after = disk_manager->GetReadCount();
                    if (reads_after > reads_before) {
                        misses++;
                    } else {
                        hits++;
                    }
                }
                done = true;
            });
            while (!done) {
                bpm->PollCompletions();
            }
            bpm->UnpinPage(pid, false);
        }

        double hit_rate = (static_cast<double>(hits) / access_pattern.size()) * 100.0;

        std::cout << "Workload: " << std::left << std::setw(20) << workload_name << " | "
                  << "Hits: " << std::setw(6) << hits << " | "
                  << "Misses: " << std::setw(6) << misses << " | "
                  << "Hit Rate: " << std::fixed << std::setprecision(2) << std::setw(6) << hit_rate << "%" << std::endl;
    };

    // -------------------------------------------------------------
    // Workload 1: Sequential Access (Repeated table full scans)
    // -------------------------------------------------------------
    std::vector<page_id_t> sequential_workload;
    for (int i = 0; i < NUM_REQUESTS; ++i) {
        sequential_workload.push_back(i % TOTAL_PAGES);
    }
    ExecuteWorkload("1. Sequential Scan", sequential_workload);

    // -------------------------------------------------------------
    // Workload 2: Uniform Random Access (No locality)
    // -------------------------------------------------------------
    std::vector<page_id_t> random_workload;
    std::uniform_int_distribution<int> uniform_dist(0, TOTAL_PAGES - 1);
    for (int i = 0; i < NUM_REQUESTS; ++i) {
        random_workload.push_back(uniform_dist(rng));
    }
    ExecuteWorkload("2. Uniform Random", random_workload);

    // -------------------------------------------------------------
    // Workload 3: Zipfian Access (Skewed 80-20 locality)
    // -------------------------------------------------------------
    std::vector<page_id_t> zipfian_workload;
    ZipfianGenerator zipf(TOTAL_PAGES, 0.95);
    for (int i = 0; i < NUM_REQUESTS; ++i) {
        zipfian_workload.push_back(zipf.Next(rng));
    }
    ExecuteWorkload("3. Zipfian (Skewed)", zipfian_workload);

    std::cout << "\n===============================================================" << std::endl;
    std::cout << "                      BENCHMARK COMPLETE                       " << std::endl;
    std::cout << "===============================================================" << std::endl;

    std::remove(db_file.c_str());
}

int main() {
    RunHitRateBenchmark();
    return 0;
}
