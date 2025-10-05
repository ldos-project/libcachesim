#include <algorithm>
#include <list>

#include "PQEvolve.h"
template <typename T> using CountsInfo = OrderedMultiset<T>;
template <typename T> using AgeInfo = AgePercentileView<T>;
template <typename T> using SizeInfo = OrderedMultiset<T>;

#include "LLMCode.h" 

// int priority(
//   uint64_t current_time, obj_id_t obj_id, pq_cache_obj_info& obj_info, 
//   CountsInfo<int32_t>& counts, AgeInfo<int64_t> ages, SizeInfo<int64_t>& sizes,
//   History& history
// ){
//   return obj_info.last_access_vtime; // LRU
//   // return current_time; // another way to implement LRU
//   // return -1 * obj_info.last_access_vtime; // MRU
//   // return obj_info.addition_to_cache_vtime; // FIFO
//   // return obj_info.count; // approximately LFU (not sure what baseline LFU does if there are multiple objects of same size)
// }
// #endif

int PQEvolveData::priority_wrapper(const cache_t *cache, obj_id_t obj_id, pq_cache_obj_info& obj_info){
  return priority(
    cache->n_req, obj_id, obj_info, 
    this->counts, 
    AgePercentileView<int64_t>(this->addition_vtime_timestamps, cache->n_req),
    this->sizes,
    this->history
  );
}

void PQEvolveData::update_metadata_access(const cache_t *cache,
                                            cache_obj_t *obj) {
  // Find the associated object metadata in the map.
  std::shared_ptr<pq_cache_obj_info> obj_metadata_ptr;
  auto it = cache_obj_metadata.find(obj->obj_id);
  if (it != cache_obj_metadata.end()) {
    obj_metadata_ptr = it->second;
  } else {
    // Object not found in the metadata map, nothing to update.
    VERBOSE("Object %lu not found in metadata map.\n", obj->obj_id);
    return;
  }
  

  // update per-object features 
  int32_t prev_count = obj_metadata_ptr->count;
  obj_metadata_ptr->count += 1;
  obj_metadata_ptr->last_access_vtime = cache->n_req;
  #ifdef USE_DELTAS
  // Compute the delta between current access and last access time.
  int32_t delta = cache->n_req - obj_metadata_ptr->last_access_vtime;
  obj_metadata_ptr->add_delta(delta);
  #endif

  // update global features
  this->counts.insert(obj_metadata_ptr->count);
  this->counts.remove(prev_count);

  // update the priority queue
  int prio = priority_wrapper(cache, obj->obj_id, *obj_metadata_ptr);
  this->pq.insert_or_update(obj, prio);
}

void PQEvolveData::update_metadata_insert(const cache_t *cache, cache_obj_t *obj) {
  // per-object features
  int64_t obj_size = obj->obj_size;
  auto new_obj_metadata = std::make_shared<pq_cache_obj_info>(1, cache->n_req, obj_size);
  cache_obj_metadata[obj->obj_id] = new_obj_metadata;

  // update global-features.
  this->counts.insert(new_obj_metadata->count);
  this->addition_vtime_timestamps.insert(cache->n_req);
  this->sizes.insert(new_obj_metadata->size);

  // update priority queue
  int prio = priority_wrapper(cache, obj->obj_id, *new_obj_metadata);
  this->pq.insert_or_update(obj, prio);
}

void PQEvolveData::update_metadata_evict(const cache_t *cache, cache_obj_t *obj) {
  // Find the associated object metadata in the map.
  std::shared_ptr<pq_cache_obj_info> obj_metadata_ptr;
  auto it = cache_obj_metadata.find(obj->obj_id);
  if (it != cache_obj_metadata.end()) {
    obj_metadata_ptr = it->second;
  } else {
    // Object not found in the metadata map, nothing to update.
    VERBOSE("Object %lu not found in metadata map.\n", obj->obj_id);
    return;
  }

  // insert to history, delete from cache
  history.insert(obj->obj_id, obj->obj_size, obj_metadata_ptr->count, cache->n_req - obj_metadata_ptr->addition_to_cache_vtime);
  cache_obj_metadata.erase(obj->obj_id);

  // Remove the counts, ages, and sizes from the lists.
  this->counts.remove(obj_metadata_ptr->count);
  this->addition_vtime_timestamps.remove(obj_metadata_ptr->addition_to_cache_vtime);
  this->sizes.remove(obj_metadata_ptr->size);
}