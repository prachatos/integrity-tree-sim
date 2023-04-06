#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cassert>
#include "cachesim.hpp"

static void print_help(void);
static void print_sim_config(sim_config_t *sim_config);
static void print_statistics(sim_stats_t* stats, sim_config_t *sim_config);

int main(int argc, char **argv) {
    sim_config_t config = {18, 2};
    FILE *trace = NULL;
    int opt;
    cache_t cache_core0;

    /* Read arguments */
    while(-1 != (opt = getopt(argc, argv, "i:I:c:C:s:S:f:F"))) {
        switch(opt) {
        case 'i':
        case 'I':
                trace = fopen(optarg, "r");
                if (trace == NULL) {
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
        default:
            print_help();
            return 0;
        }
    }

    if (trace == NULL) {
        perror("fopen");
        printf("Could not open the input trace file");
        return 1;
    }
    /* Setup the cache */

    sim_setup(&cache_core0, &config);

    /* Setup statistics */
    sim_stats_t stats;
    memset(&stats, 0, sizeof stats);
    print_sim_config(&config);
    /* Begin reading the file */
    uint64_t address;
    int rw;
    int count = 0;
    while (!feof(trace)) {
        int ret = 0;
        if (config.f)
            ret = fscanf(trace, "%d 0x%" PRIx64 "\n", &rw, &address);
        else
            ret = fscanf(trace, "0x%" PRIx64 " %d\n", &address, &rw);
        if(ret == 2) {
            sim_access(&cache_core0, (bool)rw, address, &stats);
            ++count;
        } else {
            char *line = NULL;
            size_t len = 0;
            int ret = getline(&line, &len, trace);
            assert(ret != 2);
            // Skip line
        }
        if (count % (unsigned long long)10e6 == 0 && count) {
            compute_stats(&cache_core0, &stats);
            print_statistics(&stats, &config);
        }
    }

    sim_finish(&cache_core0, &stats);

    print_statistics(&stats, &config);

    return 0;
}

static void print_help(void) {
    printf("cachesim [OPTIONS] -I traces/file.trace\n");
    printf("-h\t\tThis helpful output\n");
    printf("Metadata Cache parameters:\n");
    printf("  -c C\t\tTotal size for Metadata Cache in bytes is 2^C\n");
    printf("  -b B\t\tSize of each block for Metadata Cache in bytes is 2^B\n");
    printf("  -s S\t\tNumber of blocks (ways) per set for Metadata Cache is 2^S\n");
}

static void print_sim_config(sim_config_t *sim_config) {
    printf("(C,S): (%" PRIu64 ",%" PRIu64 ")\n",
        sim_config->c, sim_config->s
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
    printf("Metadata Cache average access time (AAT): %.3f\n", stats->avg_access_time);
    printf("Average level for verification hit: %.2f\n", stats->avg_level);
    printf("Total DRAM accesses: %" PRIu64 "\n", stats->num_dram_accesses);
    printf("\n");
}
