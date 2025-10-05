#include <glib.h>
#include <iostream> 

#include "../../../dataStructure/hashtable/hashtable.h"
#include "../../../include/libCacheSim/evictionAlgo.h"
#include "PQEvolve.h"

#ifdef __cplusplus
extern "C" {
#endif

static void PQEvolve_free(cache_t *cache);
static cache_obj_t *PQEvolve_find(cache_t *cache, const request_t *req, const bool update_cache);
static cache_obj_t *PQEvolve_insert(cache_t *cache, const request_t *req);
static cache_obj_t *PQEvolve_to_evict(cache_t *cache, const request_t *req);
static void PQEvolve_evict(cache_t *cache, const request_t *req);
static bool PQEvolve_remove(cache_t *cache, const obj_id_t obj_id);
static void PQEvolve_print_cache(const cache_t *cache);

cache_t *PQEvolve_init(const common_cache_params_t ccache_params,
                             const char *cache_specific_params) {
  cache_t *cache = cache_struct_init("PQEvolve", ccache_params,
                                     cache_specific_params);
  cache->cache_init = PQEvolve_init;
  cache->cache_free = PQEvolve_free;
  cache->get = cache_get_base;
  cache->find = PQEvolve_find;
  cache->insert = PQEvolve_insert;
  cache->evict = PQEvolve_evict;
  cache->remove = PQEvolve_remove;
  cache->to_evict = PQEvolve_to_evict;
  cache->get_occupied_byte = cache_get_occupied_byte_default;
  cache->can_insert = cache_can_insert_default;
  cache->get_n_obj = cache_get_n_obj_default;
  cache->print_cache = PQEvolve_print_cache;

  if (ccache_params.consider_obj_metadata) cache->obj_md_size = 8 * 2;
  else cache->obj_md_size = 0;

  PQEvolve_params_t *params = new PQEvolve_params_t{};
  params->metadata = new PQEvolveData(ccache_params.cache_size);

  cache->eviction_params = params;
  return cache;
}

static void PQEvolve_free(cache_t *cache) {
  // Free the frequency map before freeing the cache.
  PQEvolve_params_t *params = (PQEvolve_params_t *)(cache->eviction_params);
  delete params->metadata;
  delete params;
  cache_struct_free(cache);
}

// ***********************************************************************
// ****       developer facing APIs (used by cache developer)         ****
// ***********************************************************************

static cache_obj_t *PQEvolve_find(cache_t *cache, const request_t *req, const bool update_cache) {
  PQEvolve_params_t *params = (PQEvolve_params_t *)cache->eviction_params;
  cache_obj_t *cache_obj = cache_find_base(cache, req, update_cache);

  if (cache_obj && likely(update_cache)) {
    params->metadata->update_metadata_access(cache, cache_obj);
    // move_obj_to_head(&params->q_head, &params->q_tail, cache_obj);
  }
  return cache_obj;
}

static cache_obj_t *PQEvolve_insert(cache_t *cache, const request_t *req) {
  PQEvolve_params_t *params = (PQEvolve_params_t *)cache->eviction_params;
  cache_obj_t *obj = cache_insert_base(cache, req);
  // std::cout << "Inserted:" << obj << std::endl;

  // Prepend the object to the head of the queue for fast LRU access.
  // prepend_obj_to_head(&params->q_head, &params->q_tail, obj);

  // Update the metadata of the cache for the inserted object.
  params->metadata->update_metadata_insert(cache, obj);
  return obj;
}

static cache_obj_t *PQEvolve_to_evict(cache_t *cache, const request_t *req) {
  assert(false); return NULL;
}

static void PQEvolve_evict(cache_t *cache, const request_t *req) {
  // pop the head of pq.
  PQEvolve_params_t *params = (PQEvolve_params_t *)cache->eviction_params;
  int og_size = params->metadata->pq.size();
  cache_obj_t *obj_to_evict = params->metadata->pq.pop_min();
  assert((int(og_size - 1)) == (int)params->metadata->pq.size());
  // std::cout << "Evicted obj:" << obj_to_evict << std::endl;
  assert(obj_to_evict != nullptr);
  
  if (obj_to_evict == NULL) {
    // No object to evict, return early.
    VERBOSE("No object to evict in PQEvolve.\n");
    return;
  }

  auto *evolve_metadata = static_cast<PQEvolveData *>(params->metadata);
  evolve_metadata->update_metadata_evict(cache, obj_to_evict);

  // Remove the object from the linked list of cached objects.
  // remove_obj_from_list(&params->q_head, &params->q_tail, obj_to_evict);
  cache_evict_base(cache, obj_to_evict, true);
}

static void PQEvolve_remove_obj(cache_t *cache, cache_obj_t *obj) {
  assert(false);
}
static bool PQEvolve_remove(cache_t *cache, const obj_id_t obj_id) {
  assert(false); return true;
}

static void PQEvolve_print_cache(const cache_t *cache) {
  PQEvolve_params_t *params =
      (PQEvolve_params_t *)cache->eviction_params;
  printf("Occupied bytes: %ld\n", cache->occupied_byte);
  printf("Occupied objects: %ld\n", cache->n_obj);
  printf("Cache size: %ld\n", cache->cache_size);
  printf("Cache metadata size: %d\n", cache->obj_md_size);
}

#ifdef __cplusplus
}
#endif
