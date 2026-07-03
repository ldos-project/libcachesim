/**
 *  VulcanPQEvolve cache eviction policy using libvulcan with a priority queue.
 *
 *  Uses the same Vulcan feature/listener infrastructure as VulcanEvolve, but
 *  maintains an ordered priority queue of (score, object) pairs. Scores are
 *  updated lazily — only the touched object is re-scored on insert/access —
 *  and eviction pops the highest-score (most evictable) object directly,
 *  avoiding a full re-score at every eviction.
 *
 *  Scoring convention: the stored score represents an object's **utility**
 *  (higher = keep longer). The object with the lowest utility is evicted
 *  first. Ties are broken by insertion sequence (older-inserted evicted
 *  first among ties).
 *
 *  Staleness caveat: we rescore only the touched object on each per-object
 *  update — we never rescore the rest of the cache. PQ entries therefore go
 *  stale w.r.t. anything that shifts ranking without touching the object
 *  itself: (1) direct global features (`f_curr_time`, `f_ghost`), and (2)
 *  population aggregates (percentiles, EWMAs) that change when OTHER objects
 *  are updated. The PQ stays fresh only when scoring is object-local; any
 *  cross-object dependency will drift from a full all-objects rescore.
 */

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <unordered_map>

#include "dataStructure/hashtable/hashtable.h"
#include "libCacheSim/cache.h"
#include "libCacheSim/cacheObj.h"
#include "vulcan.h"

namespace eviction {

struct VulcanPQEntry {
  double score;
  int64_t seq;
  cache_obj_t *obj;

  // Ordered so that begin() is the next-to-evict (lowest utility).
  // Ties broken by seq (earlier-inserted evicted first).
  bool operator<(const VulcanPQEntry &o) const {
    if (score != o.score) return score < o.score;
    if (seq != o.seq) return seq < o.seq;
    return obj < o.obj;
  }
};

class VulcanPQEvolve {
 public:
  vulcan::feature_registry registry;
  vulcan::feature_handle<int64_t> f_size;
  vulcan::feature_handle<int64_t> f_insertion_time;
  vulcan::feature_handle<int64_t> f_last_access;
  vulcan::feature_handle<int64_t> f_count;
  vulcan::feature_handle<int64_t> f_ghost;
  vulcan::feature_handle<int64_t> f_curr_time;

  vulcan::rank_config config;
  std::unique_ptr<vulcan::rank_policy> policy;
  vulcan::feature_store *store = nullptr;

  // Listener-presence flags to avoid no-op updates on features with no listeners.
  bool f_size_has_listeners = false;
  bool f_insertion_time_has_listeners = false;
  bool f_last_access_has_listeners = false;
  bool f_count_has_listeners = false;
  bool f_ghost_has_listeners = false;
  bool f_curr_time_has_listeners = false;

  // Priority queue of candidates, keyed by score.
  std::set<VulcanPQEntry> pq;
  std::unordered_map<cache_obj_t *, VulcanPQEntry> pq_map;
  int64_t seq_counter = 0;

  VulcanPQEvolve() {
    f_size = registry.object.declare_i64("f_size", "Size of the object (in bytes).");
    f_insertion_time = registry.object.declare_i64("f_insertion_time", "Logical time (request sequence number) when this object was inserted.");
    f_last_access = registry.object.declare_i64("f_last_access", "Logical time (request sequence number) of the object's most recent access.");
    f_count = registry.object.declare_i64("f_count", "Number of times this object has been accessed since insertion into cache.");
    f_ghost = registry.global.declare_i64("f_ghost", "Recently evicted object IDs.");
    f_curr_time = registry.global.declare_i64("f_curr_time", "Current logical time (request sequence number).");

    #include "LLMCode.h"

    config.set_information(
      "You are building a cache eviction policy. Whenever the cache is full "
      "and a new object must be inserted, this policy is invoked to select which "
      "cached object to evict. You have access to per-object features such as"
      "size, insertion time, last access time, and access count, as well as a"
      "global feature tracking recently evicted object IDs, and the current "
      "logical time (request sequence number)."
    );

    policy = std::make_unique<vulcan::rank_policy>(vulcan::instantiate_rank_policy(registry, config));
    store = &(policy->get_feature_store());

    f_size_has_listeners = config.has_listeners(f_size);
    f_insertion_time_has_listeners = config.has_listeners(f_insertion_time);
    f_last_access_has_listeners = config.has_listeners(f_last_access);
    f_count_has_listeners = config.has_listeners(f_count);
    f_ghost_has_listeners = config.has_listeners(f_ghost);
    f_curr_time_has_listeners = config.has_listeners(f_curr_time);
  }

  // Re-score the given object using the current feature store and update its
  // position in the priority queue.
  void upsert_pq(cache_obj_t *obj) {
    double score = config.get_scoring_fn()(*store, static_cast<int64_t>(obj->obj_id));
    // Guard against NaN/inf utilities — treat non-finite values as lowest
    // utility so the broken-score object is evicted first.
    if (!std::isfinite(score)) {
      score = std::numeric_limits<double>::lowest();
    }

    auto it = pq_map.find(obj);
    if (it != pq_map.end()) {
      pq.erase(it->second);
    }
    VulcanPQEntry entry{score, seq_counter++, obj};
    pq.insert(entry);
    pq_map[obj] = entry;
  }

  cache_obj_t *pop_evict_candidate() {
    if (pq.empty()) return nullptr;
    auto it = pq.begin();
    cache_obj_t *obj = it->obj;
    pq.erase(it);
    pq_map.erase(obj);
    return obj;
  }

  void erase_from_pq(cache_obj_t *obj) {
    auto it = pq_map.find(obj);
    if (it != pq_map.end()) {
      pq.erase(it->second);
      pq_map.erase(it);
    }
  }
};

}  // namespace eviction

#ifdef __cplusplus
extern "C" {
#endif

static void VulcanPQEvolve_free(cache_t *cache);
static bool VulcanPQEvolve_get(cache_t *cache, const request_t *req);
static cache_obj_t *VulcanPQEvolve_find(cache_t *cache, const request_t *req,
                                       bool update_cache);
static cache_obj_t *VulcanPQEvolve_insert(cache_t *cache, const request_t *req);
static cache_obj_t *VulcanPQEvolve_to_evict(cache_t *cache, const request_t *req);
static void VulcanPQEvolve_evict(cache_t *cache, const request_t *req);
static bool VulcanPQEvolve_remove(cache_t *cache, obj_id_t obj_id);

cache_t *VulcanPQEvolve_init(const common_cache_params_t ccache_params,
                            const char *cache_specific_params) {
  cache_t *cache = cache_struct_init("VulcanPQEvolve", ccache_params, cache_specific_params);
  auto *ve = new eviction::VulcanPQEvolve();

  if (std::getenv("PRINT_VULCAN_CACHE_PROMPT")) {
    std::cout << ve->policy->get_prompt() << std::endl;
  }

  cache->eviction_params = ve;

  cache->cache_init = VulcanPQEvolve_init;
  cache->cache_free = VulcanPQEvolve_free;
  cache->get = VulcanPQEvolve_get;
  cache->find = VulcanPQEvolve_find;
  cache->insert = VulcanPQEvolve_insert;
  cache->evict = VulcanPQEvolve_evict;
  cache->remove = VulcanPQEvolve_remove;
  assert(!ccache_params.consider_obj_metadata &&
         "Accounting for metadata size with listeners is hard; hence we ignore.");
  return cache;
}

static void VulcanPQEvolve_free(cache_t *cache) {
  auto *ve = static_cast<eviction::VulcanPQEvolve *>(cache->eviction_params);
  delete ve;
  cache_struct_free(cache);
}

static bool VulcanPQEvolve_get(cache_t *cache, const request_t *req) {
  return cache_get_base(cache, req);
}

static cache_obj_t *VulcanPQEvolve_find(cache_t *cache, const request_t *req,
                                       bool update_cache) {
  auto *ve = static_cast<eviction::VulcanPQEvolve *>(cache->eviction_params);
  cache_obj_t *obj = cache_find_base(cache, req, update_cache);

  if (update_cache) {
    if (ve->f_curr_time_has_listeners)
      ve->store->update(ve->f_curr_time, cache->n_req);
  }

  if (obj != nullptr && update_cache) {
    obj->lfu.freq++;  // using lfu struct for local count tracking

    if (ve->f_last_access_has_listeners)
      ve->store->update(ve->f_last_access, obj->obj_id, cache->n_req);
    if (ve->f_count_has_listeners)
      ve->store->update(ve->f_count, obj->obj_id, obj->lfu.freq);

    // Re-score this object after its features updated.
    ve->upsert_pq(obj);
  }

  return obj;
}

static cache_obj_t *VulcanPQEvolve_insert(cache_t *cache, const request_t *req) {
  auto *ve = static_cast<eviction::VulcanPQEvolve *>(cache->eviction_params);

  cache_obj_t *obj = cache_insert_base(cache, req);
  obj->lfu.freq = 1;

  ve->policy->add_object(obj->obj_id);

  if (ve->f_insertion_time_has_listeners)
    ve->store->update(ve->f_insertion_time, obj->obj_id, cache->n_req);
  if (ve->f_size_has_listeners)
    ve->store->update(ve->f_size, obj->obj_id, obj->obj_size);
  if (ve->f_count_has_listeners)
    ve->store->update(ve->f_count, obj->obj_id, 1);
  if (ve->f_last_access_has_listeners)
    ve->store->update(ve->f_last_access, obj->obj_id, cache->n_req);

  // Insert the new object into the priority queue with its initial score.
  ve->upsert_pq(obj);

  return obj;
}

static cache_obj_t *VulcanPQEvolve_to_evict(cache_t *cache, const request_t *req) {
  DEBUG_ASSERT(false);
  return NULL;
}

static void VulcanPQEvolve_evict(cache_t *cache, const request_t *req) {
  auto *ve = static_cast<eviction::VulcanPQEvolve *>(cache->eviction_params);

  cache_obj_t *victim = ve->pop_evict_candidate();
  if (victim == nullptr) {
    std::cerr << "VulcanPQEvolve: priority queue empty at eviction." << std::endl;
    DEBUG_ASSERT(false);
    return;
  }

  int64_t victim_id = victim->obj_id;
  if (ve->f_ghost_has_listeners)
    ve->store->update(ve->f_ghost, victim_id);

  ve->policy->remove_object(victim_id);
  cache_remove_obj_base(cache, victim, true);
}

static void VulcanPQEvolve_remove_obj(cache_t *cache, cache_obj_t *obj) {
  DEBUG_ASSERT(false);
}

static bool VulcanPQEvolve_remove(cache_t *cache, obj_id_t obj_id) {
  DEBUG_ASSERT(false);
  return false;
}

#ifdef __cplusplus
}
#endif
