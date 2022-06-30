#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static void cache_touch(cache *c, cache_item *item);
static void cache_remove(cache *c, cache_item *item);
static char *cache_object_copy(const char *in, int obj_size);

void cache_init(cache *c) {
    cache_item *item = (cache_item *)Malloc(sizeof(cache_item));
    item->uri = NULL;
    item->obj = NULL;
    item->prev = item;
    item->next = item;
    item->size = 0;

    c->root = item;
    c->size = 0;
    c->read_cnt = 0;
    Sem_init(&c->mutex, 0, 1);
    Sem_init(&c->write, 0, 1);
}

int cache_add(cache *c, const char *uri, int uri_size, const char *obj, int obj_size) {
    if (obj_size > MAX_OBJECT_SIZE) {
        return -1;
    }
    P(&c->write);

    cache_item *item = (cache_item *)Malloc(sizeof(cache_item));
    item->uri = cache_object_copy(uri, uri_size + 1);
    item->obj = cache_object_copy(obj, obj_size);
    item->prev = c->root;
    item->next = c->root->next;
    item->size = obj_size;
    item->prev->next = item;
    item->next->prev = item;

    c->size += obj_size;

    cache_item *prev;
    for (cache_item *item = c->root->prev; c->size > MAX_CACHE_SIZE; item = prev) {
        c->size -= item->size;
        prev = item->prev;
        cache_remove(c, item);
    }
    V(&c->write);
    return 0;
}

int cache_get(cache *c, const char *uri, char **obj) {
    if (!uri) return -1;

    P(&c->mutex);
    c->read_cnt++;
    if (c->read_cnt == 1) {
        P(&c->write);
    }
    V(&c->mutex);

    for (cache_item *item = c->root->next; item != c->root; item = item->next) {
        if (!strcasecmp(item->uri, uri)) {
            cache_touch(c, item);
            *obj = item->obj;
            return item->size;
        }
    }

    cache_read_done(c);
    return -1;
}

void cache_read_done(cache *c) {
    P(&c->mutex);
    c->read_cnt--;
    if (c->read_cnt == 0) {
        V(&c->write);
    }
    V(&c->mutex);
}

static void cache_touch(cache *c, cache_item *item) {
    P(&c->mutex);
    if (!item || c->root->next == item) {
        V(&c->mutex);
        return;
    }
    item->prev->next = item->next;
    item->next->prev = item->prev;
    item->prev = c->root;
    item->next = c->root->next;
    item->prev->next = item;
    item->next->prev = item;
    V(&c->mutex);
}

static void cache_remove(cache *c, cache_item *item) {
    item->prev->next = item->next;
    item->next->prev = item->prev;
    Free(item->uri);
    Free(item->obj);
    Free(item);
}

static char *cache_object_copy(const char *in, int obj_size) {
    char *out = (char *)Malloc(obj_size * sizeof(char));
    memcpy(out, in, obj_size * sizeof(char));
    return out;
}