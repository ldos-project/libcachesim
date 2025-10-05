#ifndef EVOLVE_COMPLETE__H
#define EVOLVE_COMPLETE__H
class History {
  size_t max_total_size = 0;
  size_t current_total_size = 0;

  struct Entry {
    obj_id_t id;
    size_t size;
    int32_t count;
    int64_t age_at_eviction_time;
  };

  std::list<Entry> fifo;
  std::unordered_map<obj_id_t, std::list<Entry>::iterator> index;

public:
  History(size_t max_size_bytes) : max_total_size(max_size_bytes) {}

  void insert(obj_id_t id, size_t size, int32_t count, int64_t age_at_eviction_time) {
    // Remove existing entry if present
    auto it = index.find(id);
    if (it != index.end()) {
      current_total_size -= it->second->size;
      fifo.erase(it->second);
      index.erase(it);
    }

    // Insert new entry at the back
    fifo.push_back({id, size, count, age_at_eviction_time});
    index[id] = std::prev(fifo.end());
    current_total_size += size;

    // Evict from front until within size budget
    while (current_total_size > max_total_size && !fifo.empty()) {
      const Entry& front = fifo.front();
      current_total_size -= front.size;
      index.erase(front.id);
      fifo.pop_front();
    }
  }
    
  bool contains(obj_id_t id) const {
    return index.count(id) > 0;
  }

  bool in_last_k_bytes(obj_id_t id, size_t k_bytes) const {
    size_t scanned = 0;
    for (auto it = fifo.rbegin(); it != fifo.rend(); ++it) {
      if (scanned >= k_bytes) break;
      if (it->id == id) return true;
      scanned += it->size;
    }
    return false;
  }

  const Entry* get_metadata(obj_id_t id) const {
    auto it = index.find(id);
    return (it != index.end()) ? &(*it->second) : nullptr;
  }

  void clear() {
    fifo.clear();
    index.clear();
    current_total_size = 0;
  }

  const std::list<Entry>& get_entries() const {
    return fifo;
  }

  size_t total_size() const {
    return current_total_size;
  }
};
#endif