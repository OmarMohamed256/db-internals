#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "disk_manager.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

DiskManager::DiskManager(const std::string &db_file) {
  // Open with O_DIRECT to bypass the OS kernel page cache
  db_io_ = open(db_file.c_str(), O_RDWR | O_CREAT | O_DIRECT, 0666);
  if (db_io_ < 0) {
    perror("Failed to open DB file with O_DIRECT");
  } else {
    off_t file_size = lseek(db_io_, 0, SEEK_END);
    if (file_size > 0) {
      num_pages_ = static_cast<page_id_t>(file_size / PAGE_SIZE);
    }
  }
}

DiskManager::~DiskManager() {
  if (db_io_ >= 0) {
    close(db_io_);
  }
}

void DiskManager::WritePage(page_id_t page_id, const char *page_data) {
  off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
  ssize_t bytes_written = pwrite(db_io_, page_data, PAGE_SIZE, offset);
  if (bytes_written != PAGE_SIZE) {
    perror("WritePage failed");
  }

  // Flush physical device controller cache to guarantee durability on disk
  if (fsync(db_io_) < 0) {
    perror("fsync failed");
  }
}

void DiskManager::ReadPage(page_id_t page_id, char *page_data) {
  off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
  ssize_t bytes_read = pread(db_io_, page_data, PAGE_SIZE, offset);
  if (bytes_read < 0) {
    perror("ReadPage failed");
  }
}

page_id_t DiskManager::AllocatePage() {
  if (!free_pages_.empty()) {
    page_id_t reused_id = free_pages_.back();
    free_pages_.pop_back();
    return reused_id;
  }
  return num_pages_++;
}

void DiskManager::DeallocatePage(page_id_t page_id) {
  free_pages_.push_back(page_id);
}
