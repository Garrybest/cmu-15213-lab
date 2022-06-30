#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct cache_item {
    char *uri;
    char *obj;
    struct cache_item *prev;
    struct cache_item *next;
    int size;
} cache_item;

typedef struct {
    cache_item *root;
    int size;
    int read_cnt;
    sem_t mutex;
    sem_t write;
} cache;

void cache_init(cache *c);
int cache_add(cache *c, const char *uri, int uri_size, const char *obj, int obj_size);
int cache_get(cache *c, const char *uri, char **obj);
void cache_read_done(cache *c);

