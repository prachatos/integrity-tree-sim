
#include <cassert>
#include <cstring>
#include <cmath>
#include <iostream>
#include <vector>

#include "cachesim.hpp"

std::vector<uint64_t> lv_addr_offset;
uint64_t total_levels;
// TODO: Make this per block
bool single_owner = false;
bool hybrid_coh = false;
uint64_t write_thresh = 0;
static int64_t sim_verify_access(cache_t *cache, uint64_t node_id, uint32_t level, uint64_t pfn, sim_stats_t *stats, bool eager,bool rw);
/**
 * @brief Subroutine for initializing the cache simulator. You many add and initialize any global or heap
 * variables as needed.
 * 
 * @param config Simulation config
 */

void sim_setup(cache_t *cache_core, sim_config_t *config) {
    ULL lv_size = MAX_MEM_SIZE / (1ULL << CPU_CACHE_BLOCK_SIZE);
    for (int i=0; i<NUM_NODES; i++){
        cache_core[i].c = config->c;
        cache_core[i].b = 6;
        cache_core[i].s = config->s;
	cache_core[i].eager = config->eager;
		if(cache_core[i].eager){
			std::cout<<"eager"<<std::endl;
		}
		else{
			std::cout<<"lazy"<<std::endl;

		}
        cache_core[i].idx = config->c - config->s - cache_core[i].b;
        cache_core[i].cache = new std::map<uint64_t, cache_entry_t>[1 << cache_core[i].idx];
        cache_core[i].lruQ = new std::list<uint64_t>[1 << cache_core[i].idx];
        cache_core[i].set_entries = new uint64_t[1 << cache_core[i].idx]();
        cache_core[i].tag_compare_time = L1_TAG_COMPARE_TIME_CONST + L1_TAG_COMPARE_TIME_PER_S * (cache_core[i].s);
    }
    total_levels = ceil(log2(lv_size) * 1.0/log2(8));
    std::cout << log2(lv_size) << " " << total_levels << std::endl;
    lv_addr_offset.resize(total_levels);
    lv_addr_offset[0] = 0xfffffff000000000;
    lv_size >>= BLOCKS_PER_TOC_NODE;
    for (uint64_t i = 1; i < total_levels; ++i, lv_size >>= BLOCKS_PER_TOC_NODE) {
        lv_addr_offset[i] = lv_addr_offset[i - 1] + lv_size;
    }
    single_owner = config->single_owner;
    if (config->hybrid_coh) {
        hybrid_coh = true;
        write_thresh = config->write_thresh;
        std::cout << write_thresh << std::endl;
    }
#ifdef DEBUG
    for (uint64_t i = 0; i < total_levels; ++i) {
        std::cout << "INIT: Level[" << i << "] offset: " << std::hex << lv_addr_offset[i] << std::endl;
    }
#endif
}

//coh_state_t snoop_cache(cache_t *cache, uint64_t node_id, uint64_t pfn, uint64_t orig_pfn){
coh_state_t snoop_cache(cache_t *cache, uint64_t node_id, uint64_t idx, uint64_t tag){
    //uint64_t idx = pfn % (1 << cache[node_id].idx);
    //uint64_t tag = pfn >> cache[node_id].idx;
    if (cache[node_id].cache[idx][tag].valid) {
        return cache[node_id].cache[idx][tag].coh_state;
    }
    return COH_STATE_INVAL;
}

bool inval_block(cache_t *cache, uint64_t node_id, uint64_t idx, uint64_t tag){
    cache[node_id].cache[idx][tag].valid = false;
    cache[node_id].cache[idx][tag].dirty = false;
    cache[node_id].cache[idx][tag].single_owner = false;
    cache[node_id].cache[idx][tag].coh_state=COH_STATE_INVAL;
    cache[node_id].set_entries[idx]--;
    cache[node_id].cache[idx][tag].num_reads = 0;
    cache[node_id].cache[idx][tag].num_writes = 0;
    cache[node_id].cache[idx][tag].num_transfers = 0;
    cache[node_id].lruQ[idx].remove(tag);
    return true;
}

int maybe_mark_block_single_owner(cache_t *cache, uint64_t node_id, uint64_t idx, uint64_t tag, sim_stats_t* stats) {
    if (!hybrid_coh) {
        return 0;
    }
    if (!cache[node_id].cache[idx][tag].valid || cache[node_id].cache[idx][tag].coh_state == COH_STATE_INVAL) {
        return 0;
    }
    if (cache[node_id].cache[idx][tag].num_writes >= write_thresh) { //cache[node_id].cache[idx][tag].num_writes * 1.0/cache[node_id].cache[idx][tag].num_reads > 0.5) {
        if (!cache[node_id].cache[idx][tag].single_owner) {
            cache[node_id].cache[idx][tag].single_owner = true;
            for(uint64_t i=0; i<NUM_NODES;i++){
                if(i!=node_id){
                    if(snoop_cache( cache,i,idx,tag) != COH_STATE_INVAL){
                        inval_block(cache,i,idx,tag);
                        //increment for every block that is actually invalidated?
                        //  or broadcast to everyone if not in EX or MOD state?
                        //stats[node_id].num_inval_msgs++;
                    }
                }
            }
            return 1;
        }
    } /*else {
        if (cache[node_id].cache[idx][tag].single_owner) {
            cache[node_id].cache[idx][tag].single_owner = false;
            // it's in M state, so no one else can have it anyways
            // so extra invals
            for(uint64_t i=0; i<NUM_NODES;i++){
                if(i!=node_id){
                    cache[i].cache[idx][tag].single_owner = false;
                }
            }
            stats[node_id]->num_single_owner_unset++;
            return -1;
        }
    }*/
    return 0;
}

/**
 * @brief Access Metadata Cache
 * 
 * @param addr Address to access
 * @param rw 0 for Read or 1 for Write
 * @param stats Simulation stats
 */
bool sim_access_cache(cache_t *cache, uint64_t node_id, uint64_t pfn, bool rw, sim_stats_t* stats, bool eager,
                      uint64_t orig_pfn, uint32_t level) {
    bool res = true;
    uint64_t idx = pfn % (1 << cache[node_id].idx);
    uint64_t tag = pfn >> cache[node_id].idx;
    stats[node_id].accesses_l1++;
    if (rw == READ) {
        stats[node_id].eff_reads++;
    } else {
        stats[node_id].eff_writes++;
    }
    if (cache[node_id].cache[idx][tag].valid) {
        // hit
        stats[node_id].hits_l1++;
        if (rw == WRITE){
            cache[node_id].cache[idx][tag].dirty = true;
            cache[node_id].cache[idx][tag].coh_state = COH_STATE_MODIFIED;
            cache[node_id].cache[idx][tag].num_writes++;
            cache[node_id].cache[idx][tag].orig_pfn=orig_pfn;
            //COHERENCE ACTION for HIT WRITE (invalidate everyone else)
            for(uint64_t i=0; i<NUM_NODES;i++){
                if(i!=node_id){
                    if(snoop_cache( cache,i,idx,tag) != COH_STATE_INVAL){
                        if (cache[node_id].cache[idx][tag].single_owner) {
                            std::cerr << "WARNING - invalid coherence state with single ownership" << "(" << i << ","  << idx << "," << tag << ")\n";
                            assert(false);
                        }
                        inval_block(cache,i,idx,tag);
                        //increment for every block that is actually invalidated?
                        //  or broadcast to everyone if not in EX or MOD state?
                        stats[node_id].num_inval_msgs++;
                    }
                }
            }
            
        }
        else{
            //COHERENCE ACTION for read hit
            //None of this should execute if it's a hit..?
            cache[node_id].cache[idx][tag].num_reads++;
            uint64_t sharers_tmp=0;
            for(uint64_t i=0; i<NUM_NODES;i++){
                if(i!=node_id){
                    coh_state_t cstate = snoop_cache( cache,i,idx,tag);
                    if (cache[node_id].cache[idx][tag].single_owner && cstate != COH_STATE_INVAL) {
                        std::cerr << "WARNING - invalid coherence state with single ownership" << "(" << i << ","  << idx << "," << tag << ")\n";
                        assert(false);
                    }
                    if(cstate!=COH_STATE_INVAL){
                        sharers_tmp++;
                    }
                    if(cstate==COH_STATE_EXCLUSIVE){
                        std::cerr<<"WARNING - cache hit but another node was in exclusive"<<std::endl;
                        cache[i].cache[idx][tag].coh_state=COH_STATE_SHARED;
                    }
                    if(cstate==COH_STATE_MODIFIED){
                        std::cerr<<"WARNING - cache hit but another node was in modified"<<std::endl;
                        cache[i].cache[idx][tag].coh_state=COH_STATE_SHARED;
                        cache[i].cache[idx][tag].dirty=false;
                        stats[i].num_wb_from_m2s++;
                        //update writeback stat for the other node
                        stats[i].num_dram_accesses++;
                        stats[i].num_dram_writes++;
                    }
                }
            }
            if(sharers_tmp==0) cache[node_id].cache[idx][tag].coh_state = COH_STATE_EXCLUSIVE;
            else cache[node_id].cache[idx][tag].coh_state = COH_STATE_SHARED;
        }
        auto it = std::find(cache[node_id].lruQ[idx].begin(), cache[node_id].lruQ[idx].end(), tag);
        if (it != cache[node_id].lruQ[idx].end())
            cache[node_id].lruQ[idx].remove(tag);
        cache[node_id].lruQ[idx].push_back(tag);
        int marked = maybe_mark_block_single_owner(cache, node_id, idx, tag, stats);
        if (marked > 0) {
            stats[node_id].num_single_owner_set++;
        } else if (marked < 0) {
            stats[node_id].num_single_owner_unset++;
        }
        return res;
    }
    // miss
    res = false;
    stats[node_id].misses_l1++;

    //TODO - find in other caches
    // if found, change res=true
    //  take appropriate coherence action and increment block_transfer count
    if(rw==WRITE){
        cache[node_id].cache[idx][tag].coh_state=COH_STATE_MODIFIED;
        uint64_t prev_writes = 0, prev_reads = 0, prev_transfers = 0;
        for(uint64_t i=0; i<NUM_NODES; i++){
            if(i!=node_id){
                coh_state_t cstate = snoop_cache(cache,i,idx,tag);
                //TODO FILL THIS OUT
                if(cstate!=COH_STATE_INVAL){
                    res=true;
                    if (cache[i].cache[idx][tag].single_owner) {
                        // only one in non-inval state
                        cache[node_id].cache[idx][tag].single_owner = true;
                    }
                    if (prev_writes == 0) {
                        prev_writes = cache[i].cache[idx][tag].num_writes;
                    }
                    if (prev_reads == 0) {
                        prev_reads = cache[i].cache[idx][tag].num_reads;
                    }
                    if (prev_transfers == 0) {
                        prev_transfers = cache[i].cache[idx][tag].num_transfers;
                    }
                    inval_block(cache,i,idx,tag);
                    stats[node_id].num_inval_msgs++;
                    cache[node_id].cache[idx][tag].coh_state=COH_STATE_MODIFIED;
                }
            }
        }
        cache[node_id].cache[idx][tag].num_writes = prev_writes + 1;
        cache[node_id].cache[idx][tag].num_reads = prev_reads + 1;
        if (res) cache[node_id].cache[idx][tag].num_transfers = prev_transfers + 1;
        //std::cout << prev_writes + 1 << "," << prev_reads + 1 << std::endl;
    }
    else{
        cache[node_id].cache[idx][tag].coh_state=COH_STATE_EXCLUSIVE;
        uint64_t prev_writes = 0, prev_reads = 0, prev_transfers = 0;
        for(uint64_t i=0; i<NUM_NODES; i++){
            if(i!=node_id){
                coh_state_t cstate = snoop_cache(cache,i,idx,tag);
                if(cstate==COH_STATE_EXCLUSIVE){
                    if (prev_writes == 0) {
                        prev_writes = cache[i].cache[idx][tag].num_writes;
                    }
                    if (prev_reads == 0) {
                        prev_reads = cache[i].cache[idx][tag].num_reads;
                    }
                    if (prev_transfers == 0) {
                        prev_transfers = cache[i].cache[idx][tag].num_transfers;
                    }
                    res=true;
                    ++cache[i].cache[idx][tag].num_reads;
                    if (!cache[i].cache[idx][tag].single_owner) {
                        cache[i].cache[idx][tag].coh_state=COH_STATE_SHARED;
                        cache[node_id].cache[idx][tag].coh_state=COH_STATE_SHARED;
                    }  else {
                        inval_block(cache,i,idx,tag);
                        //stats[node_id].num_inval_msgs++;
                        cache[node_id].cache[idx][tag].coh_state=COH_STATE_EXCLUSIVE;
                        cache[node_id].cache[idx][tag].single_owner = true;
                    }
                }
                else if(cstate==COH_STATE_SHARED){
                    if (prev_writes == 0) {
                        prev_writes = cache[i].cache[idx][tag].num_writes;
                    }
                    if (prev_reads == 0) {
                        prev_reads = cache[i].cache[idx][tag].num_reads;
                    }
                    if (prev_transfers == 0) {
                        prev_transfers = cache[i].cache[idx][tag].num_transfers;
                    }
                    res=true;
                    ++cache[i].cache[idx][tag].num_reads;
                    if (!cache[i].cache[idx][tag].single_owner) {
                        cache[i].cache[idx][tag].coh_state=COH_STATE_SHARED;
                        cache[node_id].cache[idx][tag].coh_state=COH_STATE_SHARED;
                    } else {
                        inval_block(cache,i,idx,tag);
                        //stats[node_id].num_inval_msgs++;
                        cache[node_id].cache[idx][tag].coh_state=COH_STATE_EXCLUSIVE;
                        cache[node_id].cache[idx][tag].single_owner = true;
                    }
                }
                else if(cstate==COH_STATE_MODIFIED) {
                    if (prev_writes == 0) {
                        prev_writes = cache[i].cache[idx][tag].num_writes;
                    }
                    if (prev_reads == 0) {
                        prev_reads = cache[i].cache[idx][tag].num_reads;
                    }
                    if (prev_transfers == 0) {
                        prev_transfers = cache[i].cache[idx][tag].num_transfers;
                    }
                    res=true;
                    stats[i].num_wb_from_m2s++;
                    //update writeback stat for the other node
                    stats[i].num_dram_accesses++;
                    stats[i].num_dram_writes++;
                    ++cache[i].cache[idx][tag].num_reads;
                    if (!cache[i].cache[idx][tag].single_owner) {
                        cache[i].cache[idx][tag].coh_state=COH_STATE_SHARED;
                        cache[node_id].cache[idx][tag].coh_state=COH_STATE_SHARED;
                    } else {
                        assert(cache[i].cache[idx][tag].single_owner);
                        inval_block(cache,i,idx,tag);
                        //stats[node_id].num_inval_msgs++;
                        cache[node_id].cache[idx][tag].coh_state=COH_STATE_EXCLUSIVE;
                        cache[node_id].cache[idx][tag].single_owner = true;
                    }
                }
            }
        }
        cache[node_id].cache[idx][tag].num_writes = prev_writes + 1;
        cache[node_id].cache[idx][tag].num_reads = prev_reads + 1;
        if (res) cache[node_id].cache[idx][tag].num_transfers = prev_transfers + 1;
        //std::cout << prev_writes + 1 << "," << prev_reads + 1 << std::endl;
    }
    if(res==true){ //found in another node
        stats[node_id].num_block_transfer++;
    }

    //found in other block or not, insertion would work the same

    if (cache[node_id].set_entries[idx] == (uint64_t)(1 << cache[node_id].s)) {
        // victim needed if set is full
        uint64_t victimTag = cache[node_id].lruQ[idx].front();
		uint64_t evicted_orig_pfn = cache[node_id].cache[idx][victimTag].orig_pfn;
		bool dirty_wb = cache[node_id].cache[idx][victimTag].dirty;
        // Remove the victim (moved eviction before handling parent update)
        inval_block(cache, node_id, idx, victimTag);
        if (dirty_wb) {
            ++stats[node_id].num_dram_accesses;
            ++stats[node_id].num_dram_writes;
            stats[node_id].writebacks_l1++;
            if (!eager && level != total_levels - 1) {
                // Find parent addr
                //uint64_t metadata_offset = orig_pfn >> ((level + 2) * BLOCKS_PER_TOC_NODE);
                //uint64_t metadata_pfn = lv_addr_offset[level + 1] + metadata_offset;
                //sim_access_cache(cache, node_id, metadata_pfn, rw, stats, eager, orig_pfn, level + 1);
				
				// Find Parent ADDR using idx and victimTag
        		sim_verify_access(cache, node_id, level + 1, evicted_orig_pfn, stats, eager, rw);
            }
        }
    }
    cache[node_id].set_entries[idx]++;
    cache[node_id].lruQ[idx].push_back(tag);
    cache[node_id].cache[idx][tag].valid = true;
    int marked = maybe_mark_block_single_owner(cache, node_id, idx, tag, stats);
    if (marked > 0) {
        stats[node_id].num_single_owner_set++;
    } else if (marked < 0) {
        stats[node_id].num_single_owner_unset++;
    }
    if (rw == WRITE) {
        cache[node_id].cache[idx][tag].dirty = true;
    }
    if (rw == READ && res==false) { // only go to dram if it wasn't in another cache
        ++stats[node_id].num_dram_accesses;
        ++stats[node_id].num_dram_reads;
        if (single_owner) {
            cache[node_id].cache[idx][tag].single_owner = true;
        } else {
            cache[node_id].cache[idx][tag].single_owner = false;
        }
    }
    return res;
}

static int64_t sim_verify_access(cache_t *cache, uint64_t node_id, uint32_t level, uint64_t pfn, sim_stats_t *stats, bool eager,
                                 bool rw) {
    if (level == total_levels - 1) {
    #ifdef DEBUG
        std::cout << "VERIFY: Received hit at root" << std::endl;
    #endif
        return level;
    }
    pfn = pfn % (MAX_MEM_SIZE / (1ULL << CPU_CACHE_BLOCK_SIZE));
    uint64_t metadata_offset = pfn >> ((level + 1) * BLOCKS_PER_TOC_NODE);
    uint64_t metadata_pfn = lv_addr_offset[level] + metadata_offset;
#ifdef DEBUG
    std::cout << "VERIFY: Generated address " << std::hex << metadata_pfn << " for level " << std::dec << level
              << ", pfn " << std::hex << pfn << std::endl;
#endif
    bool hit = sim_access_cache(cache, node_id, metadata_pfn, READ, stats, eager, pfn, level);
    //if (rw == WRITE || !hit) {
    if (((rw == WRITE) && eager ) || !hit) { // no need to go to root if lazy update?
    #ifdef DEBUG
        std::cout << "VERIFY: Received miss at level " << level << std::endl;
    #endif
        return sim_verify_access(cache, node_id, level + 1, pfn, stats, eager, rw);
    }
#ifdef DEBUG
    std::cout << "VERIFY: Received hit at level " << level << std::endl;
#endif
    return level;
}

static void sim_write_access(cache_t *cache, uint64_t node_id, uint32_t level, uint64_t pfn, sim_stats_t *stats, bool eager) {
    // TODO: Lazy update

    if (level == total_levels - 1) {
        return;     // Stop recursion at root
    }
    // Somehow force to <16GB??
    pfn = pfn % (MAX_MEM_SIZE / (1ULL << CPU_CACHE_BLOCK_SIZE));
    uint64_t metadata_offset = pfn >> ((level + 1) * BLOCKS_PER_TOC_NODE);
    uint64_t metadata_pfn = lv_addr_offset[level] + metadata_offset;
#ifdef DEBUG
    std::cout << "WRITE: Writing to address " << std::hex << metadata_pfn << " for level " << std::dec << level
              << ", pfn " << std::hex << pfn << std::endl;
#endif
    bool hit = sim_access_cache(cache, node_id, metadata_pfn, WRITE, stats, eager, pfn, level);   // Need to stop somewhere for lazy
    ++stats[node_id].num_dram_accesses;
    ++stats[node_id].num_dram_writes;
    if (!eager && hit) {
        return;
    }
    sim_write_access(cache, node_id, level + 1, pfn, stats, eager);
}

/**
 * @brief Subroutine that simulates the cache one trace event at a time.
 * 
 *  @param rw 0 for Read or 1 for Write
 *  @param addr Address being accessed
 *  @param stats Simulation stats
 */
void sim_access(cache_t *cache, uint64_t node_id, bool rw, uint64_t addr, sim_stats_t* stats) {
    // 64 bytes --> 1 CPU block
    // 8 blocks --> 1 entry
    uint64_t addr_pfn = addr >> CPU_CACHE_BLOCK_SIZE;
    int lv_hit = 0;
    if (rw == READ) {
    #ifdef DEBUG
        std::cout << "ACCESS: Sending pfn " << std::hex << addr_pfn << " for addr " << addr << " to verify\n";
    #endif
        stats[node_id].reads++;
        lv_hit = sim_verify_access(cache, node_id, 0, addr_pfn, stats, cache[node_id].eager,  READ);
        stats[node_id].total_levels += lv_hit;
    #ifdef DEBUG
        std::cout << "ACCESS: Verified pfn " << std::hex << addr_pfn << std::dec << " at level " << lv_hit << std::endl;
    #endif
    } else {
#ifdef DEBUG
        std::cout << "ACCESS: Sending pfn " << std::hex << addr_pfn << " for addr " << addr << " to verify\n";
#endif
        stats[node_id].writes++;
        // Go till root
        lv_hit = sim_verify_access(cache, node_id, 0, addr_pfn, stats, cache[node_id].eager, WRITE);
        stats[node_id].total_levels += lv_hit;
    #ifdef DEBUG
        std::cout << "Verified pfn " << std::hex << addr_pfn << std::dec << " at level " << lv_hit << std::endl;
    #endif
        // Set dirty bits
        sim_write_access(cache, node_id, 0, addr_pfn, stats, cache[node_id].eager);
    }
    // Generate eq metadata cache address
    // Issue a cache access and see if hit
    // If not go to next level, place it in cache
    // Keep going till hit
}

void compute_stats(cache_t *cache, sim_stats_t *stats) {
    //double tag_compare_time = L1_TAG_COMPARE_TIME_CONST + L1_TAG_COMPARE_TIME_PER_S * (cache->s);
    double tag_compare_time = cache->tag_compare_time;
    stats->hit_ratio_l1 = (stats->hits_l1 * 1.0/stats->accesses_l1);
    stats->miss_ratio_l1 = (stats->misses_l1 * 1.0/stats->accesses_l1);
    stats->avg_access_time = ((L1_ARRAY_LOOKUP_TIME_CONST + tag_compare_time) *
                                stats->accesses_l1 + DRAM_ACCESS_PENALTY * stats->misses_l1 * 1.0)/
                                stats->accesses_l1;
    stats->avg_level = stats->total_levels * 1.0/(stats->reads + stats->writes);
    /*for (unsigned j = 0; j < cache->idx; ++j) {
        if (cache->cache[j].size() == 0)
            continue;
        for (auto k: cache->cache[j]) {
            if (k.second.num_writes) std::cout << k.second.num_reads << "," << k.second.num_writes << "," << k.second.num_transfers << std::endl;
        }
    }*/
}

/**
 * @brief Subroutine for cleaning up any outstanding memory operations and calculating overall statistics
 * such as miss rate or average access time.
 * 
 * @param stats Simulation stats
 */
void sim_finish(cache_t *cache, sim_stats_t *stats) {
    for(int i=0;i<NUM_NODES;i++){
    compute_stats(&(cache[i]), &(stats[i]));
    delete[] cache[i].cache;
    delete[] cache[i].lruQ;
    delete[] cache[i].set_entries;
    }
}
