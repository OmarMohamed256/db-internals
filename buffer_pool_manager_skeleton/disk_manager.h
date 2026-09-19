#pragma once

#include <string>
#include "page.h"
#include <vector>

class DiskManager {
public:
    DiskManager(const std::string &db_file);
    virtual ~DiskManager();

    virtual void WritePage(page_id_t page_id, const char *page_data);
    virtual void ReadPage(page_id_t page_id, char *page_data);
    virtual page_id_t AllocatePage();
    virtual void DeallocatePage(page_id_t page_id);

private:
    int db_io_;
    page_id_t num_pages_{0};
    std::vector<page_id_t> free_pages_;
};