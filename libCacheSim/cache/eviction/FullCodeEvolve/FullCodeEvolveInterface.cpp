#include "../../../dataStructure/hashtable/hashtable.h"
#include "../../../include/libCacheSim/evictionAlgo.h"

// Include the generated logic
#include "LLMCode.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  CacheManager* manager;
} FullCodeEvolve_params_t;

// function declarations
static void FullCodeEvolve_free(cache_t *cache);
static bool FullCodeEvolve_get(cache_t *cache, const request_t *req);
static cache_obj_t *FullCodeEvolve_find(cache_t *cache, const request_t *req, const bool update_cache);
static cache_obj_t *FullCodeEvolve_insert(cache_t *cache, const request_t *req);
static cache_obj_t *FullCodeEvolve_to_evict(cache_t *cache, const request_t *req);
static void FullCodeEvolve_evict(cache_t *cache, const request_t *req);
static bool FullCodeEvolve_remove(cache_t *cache, const obj_id_t obj_id);
static void FullCodeEvolve_print_cache(const cache_t *cache);

// ***********************************************************************
// ****                   end user facing functions                   ****
// ****                       init, free, get                         ****
// ***********************************************************************
/**
 * @brief initialize a FullCodeEvolve cache
 *
 * @param ccache_params some common cache parameters
 * @param cache_specific_params FullCodeEvolve specific parameters, should be NULL
 */
cache_t *FullCodeEvolve_init(const common_cache_params_t ccache_params,
                  const char *cache_specific_params) {
  cache_t *cache =
      cache_struct_init("FullCodeEvolve", ccache_params, cache_specific_params);
  cache->cache_init = FullCodeEvolve_init;
  cache->cache_free = FullCodeEvolve_free;
  cache->get = cache_get_base;
  cache->find = FullCodeEvolve_find;
  cache->insert = FullCodeEvolve_insert;
  cache->evict = FullCodeEvolve_evict;
  cache->remove = FullCodeEvolve_remove;
  cache->to_evict = FullCodeEvolve_to_evict;
  cache->get_occupied_byte = cache_get_occupied_byte_default;
  cache->can_insert = cache_can_insert_default;
  cache->get_n_obj = cache_get_n_obj_default;
  cache->print_cache = FullCodeEvolve_print_cache;

  if (ccache_params.consider_obj_metadata) {
    cache->obj_md_size = 8 * 2;
  } else {
    cache->obj_md_size = 0;
  }

  FullCodeEvolve_params_t* params = (FullCodeEvolve_params_t*) malloc(sizeof(FullCodeEvolve_params_t));
  params->manager = new CacheManager();
  cache->eviction_params = params;
  return cache;
}

// ***********************************************************************
// ****       developer facing APIs (used by cache developer)         ****
// ***********************************************************************

/**
 * @brief check whether an object is in the cache
 *
 * @param cache
 * @param req
 * @param update_cache whether to update the cache,
 *  if true, the object is promoted
 *  and if the object is expired, it is removed from the cache
 * @return true on hit, false on miss
 */
static cache_obj_t *FullCodeEvolve_find(cache_t *cache, const request_t *req,
                             const bool update_cache) {
  assert(update_cache);
  FullCodeEvolve_params_t *params = (FullCodeEvolve_params_t *)cache->eviction_params;
  cache_obj_t *cache_obj = cache_find_base(cache, req, update_cache);
  
  bool cache_obj_LLM = params->manager->find(req->obj_id);
  
  // correctness checks: either both reference cache+LLM Cache (a) find object, or (b) miss object.
  if(cache_obj) assert(cache_obj_LLM); 
  else assert(!cache_obj_LLM);
  
  return cache_obj;
}

/**
 * @brief insert an object into the cache,
 * update the hash table and cache metadata
 * this function assumes the cache has enough space
 * and eviction is not part of this function
 *
 * @param cache
 * @param req
 * @return the inserted object
 */
static cache_obj_t *FullCodeEvolve_insert(cache_t *cache, const request_t *req) {
  FullCodeEvolve_params_t *params = (FullCodeEvolve_params_t *)cache->eviction_params;

  cache_obj_t *obj = cache_insert_base(cache, req);
  params->manager->insert(req->obj_id);
  
  return obj;
}

/**
 * @brief evict an object from the cache
 * it needs to call cache_evict_base before returning
 * which updates some metadata such as n_obj, occupied size, and hash table
 *
 * @param cache
 * @param req not used
 */
static void FullCodeEvolve_evict(cache_t *cache, const request_t *req) {
  FullCodeEvolve_params_t *params = (FullCodeEvolve_params_t *)cache->eviction_params;
  obj_id_t victim_id = params->manager->evict();

  request_t fake_req = {};
  fake_req.obj_id = victim_id;

  cache_obj_t* victim = cache_find_base(cache, &fake_req, false);
  if(victim){
    cache_evict_base(cache, victim, true);
  }
  else assert(false);
}

static void FullCodeEvolve_free(cache_t *cache) {
  FullCodeEvolve_params_t *params = (FullCodeEvolve_params_t *)cache->eviction_params;
  
  delete params->manager;
  free(params);
  cache->eviction_params = nullptr;
  
  cache_struct_free(cache);
}


static void FullCodeEvolve_remove_obj(cache_t *cache, cache_obj_t *obj) {
  assert(false);
}

static bool FullCodeEvolve_remove(cache_t *cache, const obj_id_t obj_id) {
  assert(false);
}

static void FullCodeEvolve_print_cache(const cache_t *cache) {
  assert(false);
}

static cache_obj_t *FullCodeEvolve_to_evict(cache_t *cache, const request_t *req) {
  assert(false);
  return new cache_obj_t();
}

#ifdef __cplusplus
}
#endif