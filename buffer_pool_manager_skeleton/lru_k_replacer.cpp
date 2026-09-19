#include "lru_k_replacer.h"

LRUKReplacer::LRUKReplacer(size_t num_pages) : replacer_capacity_(num_pages){}

bool LRUKReplacer::Victim(frame_id_t* frame_id) {
  std::lock_guard<std::mutex> guard(latch_);
  if (replacer_size_ == 0) return false;

  for (frame_id_t f_id: history_list_)
  {
    // must be evictable and the history list works in FIFO fashion
    if(node_store_[f_id].is_evictable){
      *frame_id = f_id;
      history_list_.erase(node_store_[f_id].pos);
      node_store_.erase(f_id);
      replacer_size_--;
      return true;
    }
  }

  for (frame_id_t f_id: cache_list_)
  {
    // must be evictable and the history list works in FIFO fashion
    if(node_store_[f_id].is_evictable){
      *frame_id = f_id;
      cache_list_.erase(node_store_[f_id].pos);
      node_store_.erase(f_id);
      replacer_size_--;
      return true;
    }
  }
  
  return false;
}

void LRUKReplacer::Pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> guard(latch_);
    auto it = node_store_.find(frame_id);
    if (it != node_store_.end()) {
        if (it->second.is_evictable) {
            it->second.is_evictable = false;
            replacer_size_--; // Only decrement if transitioning from true -> false
        }
    }
}

void LRUKReplacer::Unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> guard(latch_);
    auto it = node_store_.find(frame_id);
    if (it != node_store_.end()) {
        if (!it->second.is_evictable) {
            it->second.is_evictable = true;
            replacer_size_++; // Only increment if transitioning from false -> true
        }
    }
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
    std::lock_guard<std::mutex> guard(latch_);

    auto it = node_store_.find(frame_id);

    // Case 1: First time accessing this frame (Count = 1)
    if (it == node_store_.end()) {
        history_list_.push_back(frame_id);
        
        FrameNode new_node;
        new_node.frame_id = frame_id;
        new_node.access_count = 1;
        new_node.is_evictable = false;
        new_node.in_cache_queue = false;
        new_node.pos = --history_list_.end(); // Iterator to the item in history_list_

        node_store_[frame_id] = new_node;
        return;
    }

    // Existing frame
    it->second.access_count++;

    // Case 2: 2nd access -> Promote from history_list_ to cache_list_
    if (it->second.access_count == 2) {
        history_list_.erase(it->second.pos); // O(1) removal using iterator

        cache_list_.push_back(frame_id);
        it->second.in_cache_queue = true;
        it->second.pos = --cache_list_.end(); // Iterator to item in cache_list_
    }
    // Case 3: 3rd+ access -> Move to the back of cache_list_ (Most Recently Used)
    else if (it->second.access_count > 2) {
        cache_list_.erase(it->second.pos);
        cache_list_.push_back(frame_id);
        it->second.pos = --cache_list_.end();
    }
}

size_t LRUKReplacer::Size() {
  std::lock_guard<std::mutex> guard(latch_);
  return replacer_size_;
}
