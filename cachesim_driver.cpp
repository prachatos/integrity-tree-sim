#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <cassert>
#include "cachesim.hpp"

static void print_help(void);
static void print_sim_config(sim_config_t *sim_config);
static void print_statistics(sim_stats_t* stats, sim_config_t *sim_config);
static void print_statistics_all_nodes(sim_stats_t* stats, sim_config_t *config);

int main(int argc, char **argv) {
    sim_config_t config = {18, 2, 0, 0, 1, 0, 0, 0};
    FILE *trace[NUM_NODES] = {NULL};
    int opt;
    //cache_t cache_core0;
    cache_t cache_core[NUM_NODES];

    /* Read arguments */
    while(-1 != (opt = getopt(argc, argv, "i:I:2:3:4:c:C:s:S:t:T:fFvVlLoOhH"))) {
        switch(opt) {
        case 'i':
        case 'I':
                trace[0] = fopen(optarg, "r");
                if (trace[0] == NULL) {
                    perror("fopen");
                    printf("Could not open the input trace file\n");
                    return 1;
                }
                //trace[1] = fopen(optarg, "r");
                break;
        case '2':
            trace[1] = fopen(optarg, "r");
            if (trace[1] == NULL) {
                perror("fopen");
                printf("Could not open the input trace file\n");
                return 1;
            }
            break;
        case '3':
            trace[2] = fopen(optarg, "r");
            if (trace[2] == NULL) {
                perror("fopen");
                printf("Could not open the input trace file\n");
                return 1;
            }
            break;
        case '4':
            trace[3] = fopen(optarg, "r");
            if (trace[3] == NULL) {
                perror("fopen");
                printf("Could not open the input trace file\n");
                return 1;
            }
            break;
        case 'c': // c
        case 'C':
            config.c = atoi(optarg);
            break;
        case 's':
        case 'S':
            config.s = atoi(optarg);
            break;
        case 'f':
        case 'F':
            config.f = true;
            break;
        case 'v':
        case 'V':
            config.v = true;
            break;
        case 'l':
        case 'L':
            config.eager = false;
            break;
        case 'o':
        case 'O':
            config.single_owner = true;
            break;
        case 'h':
        case 'H':
            config.hybrid_coh = true;
            break;
        case 't':
        case 'T':
            config.write_thresh = atoi(optarg);
            break;
        default:
            print_help();
            return 0;
        }
    }
    if (trace[0] == NULL) {
        perror("fopen");
        printf("Could not open the input trace file");
        return 1;
    }
    /// FIXME!!!! lazy hardcode for now.. will fix this later
    if(NUM_NODES==2 && !trace[1]){
        //trace[1]=fopen("/home/albert/its_traces/rand_access_t1.out", "r");
        trace[1]=fopen("/home/albert/its_traces/rand_access_shorter.out", "r");
        if (trace[1] == NULL) {
            perror("fopen");
            printf("Could not open the input trace1 file");
            return 1;
        }
    }
    

    /* Setup the cache */

    sim_setup(cache_core, &config);

    /* Setup statistics */
    sim_stats_t stats[NUM_NODES];
    for(int i=0; i<NUM_NODES;i++){
        memset(&(stats[i]), 0, sizeof stats[i]);
    }
    print_sim_config(&config);
    /* Begin reading the file */
    uint64_t address;
    int rw;
    int count[NUM_NODES] = {0};
    bool any_trace_done=false;
    while(!any_trace_done){
    //while (!feof(trace[0])) {
        for(int i=0; i<NUM_NODES;i++){
            int ret = 0;
            if (config.f)
                ret = fscanf(trace[i], "%d 0x%" PRIx64 "\n", &rw, &address);
            else
                ret = fscanf(trace[i], "0x%" PRIx64 " %d\n", &address, &rw);
            if(ret == 2) {
                sim_access(cache_core, i, (bool)rw, address, stats);
                ++count[i];
            } else {
                char *line = NULL;
                size_t len = 0;
                int ret = getline(&line, &len, trace[i]);
                assert(ret != 2);
                // Skip line
            }
            if (config.v && count[i] % (unsigned long long)10e5 == 0 && count[i]) {
                printf("Node %d:\n",i);
                any_trace_done = true;
                compute_stats(&cache_core[i], &stats[i]);
                print_statistics(&stats[i], &config);
                break;
            }
        }
        if (!any_trace_done) {
            for(int i=0; i<NUM_NODES;i++){
                if(feof(trace[i])){
                    any_trace_done=true;
                }
            }
        }
    }

    sim_finish(cache_core, stats);

    print_statistics_all_nodes(stats, &config);

    return 0;
}

static void print_help(void) {
    printf("cachesim [OPTIONS] -I traces/file.trace\n");
    printf("-h\t\tThis helpful output\n");
    printf("Metadata Cache parameters:\n");
    printf("  -c C\t\tTotal size for Metadata Cache in bytes is 2^C\n");
    printf("  -s S\t\tNumber of blocks (ways) per set for Metadata Cache is 2^S\n");
    printf("  -f F\t\tIf the trace has format (rw, addr)\n");
    printf("  -v V\t\tPrint statistics every million accesses\n");
    printf("  -l L\t\tEnable lazy update\n");
}

static void print_sim_config(sim_config_t *sim_config) {
    printf("(C,S): (%" PRIu64 " KiB,%" PRIu64 " way)\n",
        (1UL << sim_config->c)/1024, (1UL << sim_config->s)
    );
}

static void print_statistics(sim_stats_t* stats, sim_config_t *config) {
    printf("Cache Statistics\n");
    printf("----------------\n");
    printf("Reads: %" PRIu64 "\n", stats->reads);
    printf("Writes: %" PRIu64 "\n", stats->writes);
    printf("\n");
    printf("Metadata Cache accesses: %" PRIu64 "\n", stats->accesses_l1);
    printf("Metadata Cache reads: %" PRIu64 "\n", stats->eff_reads);
    printf("Metadata Cache writes: %" PRIu64 "\n", stats->eff_writes);
    printf("Metadata Cache hits: %" PRIu64 "\n", stats->hits_l1);
    printf("Metadata Cache misses: %" PRIu64 "\n", stats->misses_l1);
    printf("Metadata Cache hit ratio: %.3f\n", stats->hit_ratio_l1);
    printf("Metadata Cache miss ratio: %.3f\n", stats->miss_ratio_l1);
    printf("Metadata Cache writebacks due user level conflicts: %" PRIu64 "\n", stats->writebacks_l1);
    //printf("Metadata Cache average access time (AAT): %.3f\n", stats->avg_access_time);
    printf("Average level for verification hit: %.2f\n", stats->avg_level);
    printf("\n");
    printf("Metadata inval messages: %" PRIu64 "\n", stats->num_inval_msgs);
    printf("Metadata block transfers: %" PRIu64 "\n", stats->num_block_transfer);
    printf("Metadata writebacks from modify to shared: %" PRIu64 "\n", stats->num_wb_from_m2s);
    printf("Total DRAM accesses: %" PRIu64 "\n", stats->num_dram_accesses);
    printf("Total transitions to Single Owner: %" PRIu64 "\n", stats->num_single_owner_set);
    printf("Total transitions from Single Owner: %" PRIu64 "\n", stats->num_single_owner_unset);
    printf("\n");
}
static void print_statistics_all_nodes(sim_stats_t* stats, sim_config_t *config) {
    for(int i=0;i<NUM_NODES;i++){
        printf("Node %d:\n",i);
        print_statistics(&(stats[i]),config);
    }
}
