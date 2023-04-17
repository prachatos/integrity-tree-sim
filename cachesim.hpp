#ifndef CACHESIM_HPP
#define CACHESIM_HPP

#include <map>
#include <list>
#include <algorithm>
#include <stdint.h>
#include <stdbool.h>

#define CPU_CACHE_BLOCK_SIZE 6
#define BLOCKS_PER_TOC_NODE 3
#define ULL unsigned long long

#define NUM_NODES 2

typedef enum {
    READ,
    WRITE,
} access_types_t;

typedef enum {
    COH_STATE_MODIFIED,
    COH_STATE_EXCLUSIVE,
    COH_STATE_SHARED,
    COH_STATE_INVAL,
} coh_state_t;

typedef struct cache_entry {
    bool valid;                 // valid bit
    bool dirty;                 // dirty bit
    coh_state_t coh_state;      // coherence state
    bool single_owner;          // single owner block, note owner_id is implied
    uint64_t num_reads;
    uint64_t num_writes;
    uint64_t num_transfers;
} cache_entry_t;

typedef struct cache {
    std::map<uint64_t, cache_entry_t> *cache;   // Cache is implemented as an array of hash tables (one per set)
    std::list<uint64_t> *lruQ;                  // Doubly linked list for LRU status (one per set)
    uint64_t *set_entries;                      // Utility array to check if a set is full
    uint64_t c;                                 // Size of cache
    uint64_t b;                                 // Block size of cache
    uint64_t s;                                 // Set size of cache
    uint64_t idx;                               // Index or way select value
    double tag_compare_time;
    bool eager;                                 // Whether to do eager or lazy updates
} cache_t;

typedef struct sim_config {
    uint64_t c;                     // Metadata cache size (log)
    uint64_t s;                     // Set associativity
    bool f;                         // R/W addr reversed or not
    bool v;                         // Stats every million instructions or not
    bool eager;                     // Whether to do eager or lazy updates
    bool single_owner;              // Single ownership for multinode case
    bool hybrid_coh;
    uint64_t write_thresh;
} sim_config_t;

typedef struct sim_stats {
    uint64_t reads;                 // read requests
    uint64_t writes;                // write requests
    uint64_t accesses_l1;           // total attempts to use L1
    uint64_t array_lookups_l1;      // times an array lookup was used to populate the set buffer
    uint64_t tag_compares_l1;       // times the L1 was used and TLB hit
    uint64_t hits_l1;               // times (TLB hit and) tag matched
    uint64_t misses_l1;             // times (TLB hit and) tag mismatch
    uint64_t writebacks_l1;         // total L1 evictions of dirty blocks
    double hit_ratio_l1;            // ratio of tag matches to TLB hits for L1 cache
    double miss_ratio_l1;           // ratio of tag mismatches to TLB hits for L1 cache
    uint64_t cache_flush_writebacks;
    double avg_access_time;     // average access time for the entire system
    double avg_level;
    uint64_t total_levels;
    uint64_t eff_reads;
    uint64_t eff_writes;
    uint64_t num_dram_writes;
    uint64_t num_dram_reads;
    uint64_t num_dram_accesses;
    uint64_t num_single_owner_set;
    uint64_t num_single_owner_unset;

    //coherence stats
    uint64_t num_inval_msgs;
    uint64_t num_wb_from_m2s; //writeback triggered by read request to a modified block
    uint64_t num_block_transfer;
} sim_stats_t;

extern void sim_setup(cache_t *cache_core0, sim_config_t *config);
extern void sim_access(cache_t *cache, uint64_t node_id, bool rw, uint64_t addr, sim_stats_t* p_stats);
extern void sim_finish(cache_t *cache, sim_stats_t *p_stats);
extern void compute_stats(cache_t *cache, sim_stats_t *stats);

static const double DRAM_ACCESS_PENALTY = 100;
static const unsigned long long MAX_MEM_SIZE = 8ULL * 1024 * 1024 * 1024;
// Hit time (HT) for an L1 Cache:
// is HIT_TIME_CONST + (HIT_TIME_PER_S * S)
static const double L1_ARRAY_LOOKUP_TIME_CONST = 1;
static const double L1_TAG_COMPARE_TIME_CONST = 1;
static const double L1_TAG_COMPARE_TIME_PER_S = 0.2;

#endif /* CACHESIM_HPP */
