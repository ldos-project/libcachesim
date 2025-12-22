#include <bits/stdc++.h>
typedef uint64_t obj_id_t;

class CacheManager {
private:
    std::list<obj_id_t> lru_list;
    std::unordered_map<obj_id_t, std::list<obj_id_t>::iterator> map;

public:
    bool find(obj_id_t obj_id) {
        auto it = map.find(obj_id);
        if (it == map.end()) return false;
        lru_list.erase(it->second);
        lru_list.push_front(obj_id);
        map[obj_id] = lru_list.begin();
        return true;
    }

    void insert(obj_id_t obj_id) {
        if (map.count(obj_id)) {
            lru_list.erase(map[obj_id]);
        }
        lru_list.push_front(obj_id);
        map[obj_id] = lru_list.begin();
    }

    obj_id_t evict() {
        obj_id_t victim = lru_list.back();
        lru_list.pop_back();
        map.erase(victim);
        return victim;
    }
};
