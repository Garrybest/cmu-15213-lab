#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cachelab.h"

const char HIT[] = "hit";
const char MISS[] = "miss";
const char MISS_EVICTION[] = "miss eviction";

typedef struct {
    uint64_t tag;
    uint64_t idx;
} cache_line;

typedef struct {
    int S;
    int E;
    int b;

    int hits;      /* number of  hits */
    int misses;    /* number of misses */
    int evictions; /* number of evictions */

    uint64_t block_mask;
    uint64_t set_mask;
    uint64_t tag_mask;

    cache_line** sets;
} cache;

void init_cache(cache* c, int s, int E, int b) {
    c->S = 1 << s;
    c->E = E;
    c->b = b;

    c->block_mask = (1 << b) - 1;
    c->set_mask = ((1 << (s + b)) - 1) & ~c->block_mask;
    c->tag_mask = ~0 & ~c->set_mask & ~c->block_mask;

    c->sets = (cache_line**)malloc(c->S * sizeof(cache_line*));
    for (int i = 0; i < c->S; i++) {
        c->sets[i] = (cache_line*)calloc(E, sizeof(cache_line));
    }
}

void destroy_cache(cache* c) {
    for (int i = 0; i < c->S; i++) {
        free(c->sets[i]);
    }
    free(c->sets);
    free(c);
}

const char* update_cache(cache* c, uint64_t addr, uint64_t idx) {
    uint64_t set_index = (addr & c->set_mask) >> c->b;
    cache_line* associativities = c->sets[set_index];
    uint64_t tag = addr & c->tag_mask;

    int update_idx = 0;
    uint8_t vacant = 0;
    for (int i = 0; i < c->E; i++) {
        uint64_t cache_tag = associativities[i].tag & c->tag_mask;
        uint8_t cache_valid = associativities[i].tag & 1;
        // Hit
        if (cache_valid && cache_tag == tag) {
            associativities[i].idx = idx;
            c->hits++;
            return HIT;
        }
        if (vacant) {
            continue;
        }
        if (!cache_valid) {
            vacant = 1;
            update_idx = i;
        } else if (associativities[i].idx < associativities[update_idx].idx) {
            update_idx = i;
        }
    }
    associativities[update_idx].tag = tag | 1;
    associativities[update_idx].idx = idx;
    c->misses++;
    if (vacant) {
        return MISS;
    }
    c->evictions++;
    return MISS_EVICTION;
}

static void print_usage() {
    printf("Usage: ./csim [-hv] -s <num> -E <num> -b <num> -t <file>\n");
    printf("Options:\n");
    printf("  -h         Print this help message.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -s <num>   Number of set index bits.\n");
    printf("  -E <num>   Number of lines per set.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -t <file>  Trace file.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  linux>  ./csim -s 4 -E 1 -b 4 -t traces/yi.trace\n");
    printf("  linux>  ./csim -v -s 8 -E 2 -b 4 -t traces/yi.trace\n");
}

static char* join(char* dest, const char* src) {
    strcat(dest, " ");
    strcat(dest, src);
    return dest;
}

int main(int argc, char** argv) {
    int opt, s = 0, E = 0, b = 0, v = 0;
    char t[100] = "";
    while (-1 != (opt = getopt(argc, argv, "hvs:E:b:t:"))) {
        /* determine which argument it’s processing */
        switch (opt) {
            case 'h':
                print_usage();
                break;
            case 'v':
                v = 1;
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 't':
                strcpy(t, optarg);
                break;

            default:
                printf("wrong argument\n");
                break;
        }
    }
    if (s < 0 || E <= 0 || b <= 0 || strlen(t) == 0) {
        print_usage();
        return -1;
    }

    FILE* pFile = fopen(t, "r");  // pointer to FILE object
    if (pFile == NULL) {
        fprintf(stderr, "failed to open file %s", t);
        return -1;
    }

    cache* c = (cache*)calloc(1, sizeof(cache));
    init_cache(c, s, E, b);

    char identifier;
    uint64_t address, idx = 0;
    int size;
    // Reading lines like " M 20,1" or "L 19,3"
    while (fscanf(pFile, " %c %lx,%d", &identifier, &address, &size) > 0) {
        char str[80];
        sprintf(str, "%c %lx,%d", identifier, address, size);
        ++idx;
        switch (identifier) {
            case 'L':
                join(str, update_cache(c, address, idx));
                break;
            case 'M':
                join(str, update_cache(c, address, idx));
            case 'S':
                join(str, update_cache(c, address, idx));
                break;
            default:
                continue;
        }
        if (v) {
            printf("%s\n", str);
        }
    }

    fclose(pFile);
    printSummary(c->hits, c->misses, c->evictions);
    destroy_cache(c);
    return 0;
}
