#pragma once
#include <unordered_map>

#include "../../../dataStructure/hashtable/hashtable.h"
#include "../../../include/libCacheSim/evictionAlgo.h"
#include "Percentile.h"
#include "History.h"

class CachePQ {
  struct Entry {
    int pr;
    cache_obj_t*  ptr;
    
    // bool operator<(const Entry& o)  const { return pr <  o.pr; }
    bool operator<(const Entry& o)  const { return pr < o.pr || (pr == o.pr && ptr < o.ptr); }
    bool operator==(const Entry& o) const { return pr == o.pr && ptr == o.ptr; }
    bool operator!=(const Entry& o) const { return !(*this == o); }
  };
  
  OrderedMultiset<Entry> pq_;
  std::unordered_map<cache_obj_t*, int> ptr_prio_map;

public:
    /* insert / update object with priority */
    void insert_or_update(cache_obj_t* ptr, int p)
    {
      assert(ptr != nullptr);
      auto it = ptr_prio_map.find(ptr);
      if (it != ptr_prio_map.end()) pq_.remove({it->second, ptr});
      pq_.insert({p, ptr});
      ptr_prio_map[ptr] = p;
    }

    cache_obj_t* pop_min()
    {
        if (pq_.empty()) return nullptr;
        Entry e = pq_.begin();
        pq_.remove(e);
        ptr_prio_map.erase(e.ptr);
        assert(e.ptr != nullptr);
        return e.ptr; 
    }

    bool        empty() const { return pq_.empty(); }
    std::size_t size()  const { return pq_.size();  }
};

class pq_cache_obj_info {
  public:
  int32_t count; // number of times object accessed
  int64_t last_access_vtime; // last vtime object accessed

  // updated ONLY once when object is inserted
  int64_t size;  // bytes
  int64_t addition_to_cache_vtime;  // vtime of addition to cache

  #ifdef USE_DELTAS
    const size_t num_deltas;    // Number of deltas recorded for the object
    std::list<int32_t> deltas;  // List of last num_deltas deltas for the object
    pq_cache_obj_info(int32_t c, int64_t l, int64_t s,
                                  int32_t d = 3)
        : count(c), last_access_vtime(l), size(s), addition_to_cache_vtime(l), num_deltas(d) {
      deltas = std::list<int32_t>();
    }
    ~pq_cache_obj_info() { deltas.clear(); }
    void add_delta(int32_t delta) {
      deltas.push_back(delta);
      if (deltas.size() > num_deltas) {
        deltas.pop_front();
      }
    }
  #else
    pq_cache_obj_info(int32_t c, int64_t l, int64_t s) : count(c), last_access_vtime(l), size(s), addition_to_cache_vtime(l) {}
    ~pq_cache_obj_info() {}
  #endif
};


class PQEvolveData {
 public:
  // per-object information or features. Maps: object id -> features, forall items in cache.
  std::unordered_map<obj_id_t, std::shared_ptr<pq_cache_obj_info>> cache_obj_metadata;

  // global features of the cache
  OrderedMultiset<int32_t> counts;
  OrderedMultiset<int64_t> addition_vtime_timestamps;
  OrderedMultiset<int64_t> sizes;

  // recently evicted objects
  History history;

  // priority queue for object eviction
  CachePQ pq;

  // function definitions
  PQEvolveData(size_t h = 100) : history(h) {};
  ~PQEvolveData() {}
  int priority_wrapper(const cache_t *cache, obj_id_t obj_id, pq_cache_obj_info& obj_info);
  void update_metadata_access(const cache_t *cache, cache_obj_t *obj);
  void update_metadata_insert(const cache_t *cache, cache_obj_t *obj);
  void update_metadata_evict(const cache_t *cache, cache_obj_t *obj);
};

typedef struct {
  PQEvolveData* metadata; // Pointer to the Evolve metadata
} PQEvolve_params_t;