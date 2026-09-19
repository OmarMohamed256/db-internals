#pragma once

#include "replacer.h"
#include <mutex>
#include <unordered_map>
#include <list>

class LRUKReplacer : public Replacer {
public:
    explicit LRUKReplacer(size_t num_pages);
    ~LRUKReplacer() override = default;

    bool Victim(frame_id_t *frame_id) override;
    void Pin(frame_id_t frame_id) override;
    void Unpin(frame_id_t frame_id) override;
    size_t Size() override;

    void RecordAccess(frame_id_t frame_id);

private:
    struct FrameNode {
        frame_id_t frame_id;
        size_t access_count{0};
        bool is_evictable{false};
        bool in_cache_queue{false}; // false = in history_list_ (FIFO), true = in cache_list_ (LRU)
        std::list<frame_id_t>::iterator pos; // Iterator to location in history_list_ or cache_list_
    };

    std::mutex latch_;
    size_t replacer_size_{0}; // Count of currently evictable (unpinned) frames

    // Queue 1: FIFO history queue (frames with < 2 accesses)
    std::list<frame_id_t> history_list_;

    // Queue 2: LRU cache queue (frames with >= 2 accesses)
    std::list<frame_id_t> cache_list_;

    size_t replacer_capacity_{0};

    // O(1) Fast lookup from frame_id to its node & list iterator
    std::unordered_map<frame_id_t, FrameNode> node_store_;
};