/**
 *  VulcanEvolve cache eviction policy using libvulcan.
 */

#include <cassert>
#include <cstdlib>
#include <iostream>

#include "abstractRank.hpp"
#include "vulcan.h"
#include "vulcan.h"

namespace eviction {
class VulcanEvolve : public abstractRank {
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
  vulcan::feature_store* store = nullptr;

  VulcanEvolve() {
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
  }
};
}  // namespace eviction

#ifdef __cplusplus
extern "C" {
#endif

bool f_size_has_listeners;
bool f_insertion_time_has_listeners;
bool f_last_access_has_listeners;
bool f_count_has_listeners;
bool f_ghost_has_listeners;
bool f_curr_time_has_listeners;

static void VulcanEvolve_free(cache_t *cache);
static bool VulcanEvolve_get(cache_t *cache, const request_t *req);
static cache_obj_t *VulcanEvolve_find(cache_t *cache, const request_t *req,
                                bool update_cache);
static cache_obj_t *VulcanEvolve_insert(cache_t *cache, const request_t *req);
static cache_obj_t *VulcanEvolve_to_evict(cache_t *cache, const request_t *req);
static void VulcanEvolve_evict(cache_t *cache, const request_t *req);
static bool VulcanEvolve_remove(cache_t *cache, obj_id_t obj_id);

cache_t *VulcanEvolve_init(const common_cache_params_t ccache_params,
                     const char *cache_specific_params) {
  cache_t *cache = cache_struct_init("VulcanEvolve", ccache_params, cache_specific_params);
  auto *vulcan_evolve = new eviction::VulcanEvolve();
  f_size_has_listeners = vulcan_evolve->config.has_listeners(vulcan_evolve->f_size);
  f_insertion_time_has_listeners = vulcan_evolve->config.has_listeners(vulcan_evolve->f_insertion_time);
  f_last_access_has_listeners = vulcan_evolve->config.has_listeners(vulcan_evolve->f_last_access);
  f_count_has_listeners = vulcan_evolve->config.has_listeners(vulcan_evolve->f_count);
  f_ghost_has_listeners = vulcan_evolve->config.has_listeners(vulcan_evolve->f_ghost);
  f_curr_time_has_listeners = vulcan_evolve->config.has_listeners(vulcan_evolve->f_curr_time);
  
  if (std::getenv("PRINT_VULCAN_CACHE_PROMPT")) {
    std::cout << vulcan_evolve->policy->get_prompt() << std::endl;
  }

  cache->eviction_params = vulcan_evolve;

  cache->cache_init = VulcanEvolve_init;
  cache->cache_free = VulcanEvolve_free;
  cache->get = VulcanEvolve_get;
  cache->find = VulcanEvolve_find;
  cache->insert = VulcanEvolve_insert;
  cache->evict = VulcanEvolve_evict;
  cache->remove = VulcanEvolve_remove;
  assert (!ccache_params.consider_obj_metadata && "Accounting for metadata size with listeners is hard; hence we ignore.");
  return cache;
}

static void VulcanEvolve_free(cache_t *cache) {
  auto *vulcan_evolve = static_cast<eviction::VulcanEvolve *>(cache->eviction_params);
  delete vulcan_evolve;
  cache_struct_free(cache);
}

static bool VulcanEvolve_get(cache_t *cache, const request_t *req) {
  return cache_get_base(cache, req);
}

static cache_obj_t *VulcanEvolve_find(cache_t *cache, const request_t *req,
                                bool update_cache) {
  auto *vulcan_evolve = static_cast<eviction::VulcanEvolve *>(cache->eviction_params);
  cache_obj_t *obj = cache_find_base(cache, req, update_cache);

  if (update_cache) {
    if(f_curr_time_has_listeners) vulcan_evolve->store->update(vulcan_evolve->f_curr_time, cache->n_req);
  }

  if (obj != nullptr && update_cache) {
    obj->lfu.freq++; // using lfu struct for local count tracking

    // Update feature registry
    if(f_last_access_has_listeners) vulcan_evolve->store->update(vulcan_evolve->f_last_access, obj->obj_id, cache->n_req);
    if(f_count_has_listeners) vulcan_evolve->store->update(vulcan_evolve->f_count, obj->obj_id, obj->lfu.freq);
  }

  return obj;
}

static cache_obj_t *VulcanEvolve_insert(cache_t *cache, const request_t *req) {
  auto *vulcan_evolve = static_cast<eviction::VulcanEvolve *>(cache->eviction_params);

  cache_obj_t *obj = cache_insert_base(cache, req);
  obj->lfu.freq = 1;

  // Add object to policy and registry
  vulcan_evolve->policy->add_object(obj->obj_id);
  
  if(f_insertion_time_has_listeners) vulcan_evolve->store->update(vulcan_evolve->f_insertion_time, obj->obj_id, cache->n_req);
  if(f_size_has_listeners) vulcan_evolve->store->update(vulcan_evolve->f_size, obj->obj_id, obj->obj_size);
  if(f_count_has_listeners) vulcan_evolve->store->update(vulcan_evolve->f_count, obj->obj_id, 1);
  if(f_last_access_has_listeners) vulcan_evolve->store->update(vulcan_evolve->f_last_access, obj->obj_id, cache->n_req);

  return obj;
}

static cache_obj_t *VulcanEvolve_to_evict(cache_t *cache, const request_t *req) {
  DEBUG_ASSERT(false);
  return NULL;
}

static void VulcanEvolve_evict(cache_t *cache, const request_t *req) {
  auto *vulcan_evolve = static_cast<eviction::VulcanEvolve *>(cache->eviction_params);

  // Use vulcan to decide which object to evict
  int64_t victim_id = vulcan::decision(*(vulcan_evolve->policy));
  if(f_ghost_has_listeners) vulcan_evolve->store->update(vulcan_evolve->f_ghost, victim_id);

  cache_obj_t *obj = hashtable_find_obj_id(cache->hashtable, victim_id);
  if (obj != nullptr) {
      vulcan_evolve->policy->remove_object(victim_id);
      cache_remove_obj_base(cache, obj, true);
  } else {
      std::cerr << "Vulcan logic error: Object " << victim_id << " not found in hashtable." << std::endl;
      DEBUG_ASSERT(false);
  }
}

static void VulcanEvolve_remove_obj(cache_t *cache, cache_obj_t *obj) {
  DEBUG_ASSERT(false);
}

static bool VulcanEvolve_remove(cache_t *cache, obj_id_t obj_id) {
  DEBUG_ASSERT(false);
}

#ifdef __cplusplus
}
#endif
