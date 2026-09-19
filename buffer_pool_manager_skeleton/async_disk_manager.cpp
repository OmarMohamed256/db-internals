#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "async_disk_manager.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

AsyncDiskManager::AsyncDiskManager(const std::string &db_file, unsigned sq_depth) {
  io_manager_ = std::make_unique<IOUringManager>(db_file, sq_depth);

  // Determine existing database size to initialize allocation metadata
  int fd = open(db_file.c_str(), O_RDONLY);
  if (fd >= 0) {
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size > 0) {
      num_pages_ = static_cast<page_id_t>(file_size / PAGE_SIZE);
    }
    close(fd);
  }
}

void AsyncDiskManager::WritePageAsync(page_id_t page_id, const char *page_data, IOCompletionCallback callback) {
  io_manager_->AsyncWritePage(page_id, page_data, std::move(callback));
}

void AsyncDiskManager::ReadPageAsync(page_id_t page_id, char *page_data, IOCompletionCallback callback) {
  io_manager_->AsyncReadPage(page_id, page_data, std::move(callback));
}

page_id_t AsyncDiskManager::AllocatePage() {
  if (!free_pages_.empty()) {
    page_id_t reused_id = free_pages_.back();
    free_pages_.pop_back();
    return reused_id;
  }
  return num_pages_++;
}

void AsyncDiskManager::DeallocatePage(page_id_t page_id) {
  free_pages_.push_back(page_id);
}

int AsyncDiskManager::PollCompletions() {
  return io_manager_->PollCompletions();
}
