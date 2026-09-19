#pragma once

#include <string>
#include <vector>
#include <memory>
#include "page.h"
#include "io_uring_manager.h"

class AsyncDiskManager {
public:
  explicit AsyncDiskManager(const std::string &db_file, unsigned sq_depth = 128);
  ~AsyncDiskManager() = default;

  // Asynchronously writes page to disk
  void WritePageAsync(page_id_t page_id, const char *page_data, IOCompletionCallback callback);

  // Asynchronously reads page from disk
  void ReadPageAsync(page_id_t page_id, char *page_data, IOCompletionCallback callback);

  // Synchronous page allocation/deallocation metadata helpers
  page_id_t AllocatePage();
  void DeallocatePage(page_id_t page_id);

  // Polls completions from the underlying io_uring ring
  int PollCompletions();

private:
  std::unique_ptr<IOUringManager> io_manager_;
  page_id_t num_pages_{0};
  std::vector<page_id_t> free_pages_;
};
