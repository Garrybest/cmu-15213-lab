/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Best",
    /* First member's full name */
    "Garry Best",
    /* First member's email address */
    "garrybest@foxmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    "",
};

typedef void* ptr;

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
#define NUM_LISTS 10
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define PTR_SIZE (ALIGN(sizeof(ptr)))
#define SIZE(p) (*(size_t*)(p) & -2)
#define ALLOCATED(p) (*(size_t*)(p)&1)
#define HEADER(p) ((size_t*)(p))
#define FOOTER(p) ((size_t*)((char*)(p) + SIZE(p) - SIZE_T_SIZE))
#define NEXT(p) ((ptr*)((char*)(p) + SIZE_T_SIZE))
#define PREV(p) ((ptr*)((char*)(p) + SIZE_T_SIZE + PTR_SIZE))

static void* mm_malloc_new(size_t size);
static void* mm_malloc_old(size_t size);
static void* find_list(size_t size);
static int find_list_index(size_t size);
static void* list_init();
static void* list_push_front(void* p);
static void* list_remove(void* p);

static ptr root[NUM_LISTS];

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    for (size_t i = 0; i < NUM_LISTS; i++)
        root[i] = list_init();
    return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void* mm_malloc(size_t size) {
    void* p;
    if ((p = mm_malloc_old(size)) == NULL)
        return mm_malloc_new(size);
    return p;
}

/*
 * mm_free - Freeing a block.
 */
void mm_free(void* p) {
    p = (char*)p - SIZE_T_SIZE;
    if (!ALLOCATED(p))
        return;

    size_t block_size = SIZE(p);
    if (!ALLOCATED((char*)p - SIZE_T_SIZE)) {
        // merge front
        size_t front_block_size = SIZE((char*)p - SIZE_T_SIZE);
        p = (char*)p - front_block_size;
        list_remove(p);
        block_size += front_block_size;
    }
    if (((char*)p + block_size < (char*)mem_heap_hi()) &&
        !ALLOCATED((char*)p + block_size)) {
        // merge back
        size_t back_block_size = SIZE((char*)p + block_size);
        list_remove((char*)p + block_size);
        block_size += back_block_size;
    }
    *HEADER(p) = block_size;
    *FOOTER(p) = block_size;
    list_push_front(p);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void* mm_realloc(void* p, size_t size) {
    void* oldptr = p;
    void* newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = SIZE((char*)oldptr - SIZE_T_SIZE);
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

static void* mm_malloc_new(size_t size) {
    int newsize = ALIGN(size + 2 * SIZE_T_SIZE);
    void* p = mem_sbrk(newsize);
    if (p == (void*)-1)
        return NULL;
    else {
        *HEADER(p) = newsize | 1;
        *FOOTER(p) = newsize | 1;
        return (void*)((char*)p + SIZE_T_SIZE);
    }
}

static void* mm_malloc_old(size_t size) {
    ptr p, head;
    size_t block_size, left_size;
    size_t newsize = ALIGN(size + 2 * SIZE_T_SIZE);

    for (int idx = find_list_index(newsize); idx < NUM_LISTS; idx++) {
        head = root[idx];
        for (p = *NEXT(head); p != head; p = *NEXT(p)) {
            block_size = SIZE(p);
            if (!ALLOCATED(p) && (block_size >= newsize)) {
                list_remove(p);
                left_size = block_size - newsize;
                if (left_size > ALIGN(2 * SIZE_T_SIZE + 2 * PTR_SIZE)) {
                    *HEADER(p) = newsize | 1;
                    *FOOTER(p) = newsize | 1;
                    // p points to the left space
                    ptr left = (char*)p + newsize;
                    *HEADER(left) = left_size;
                    *FOOTER(left) = left_size;
                    list_push_front(left);
                } else {
                    *HEADER(p) = block_size | 1;
                    *FOOTER(p) = block_size | 1;
                }
                return (void*)((char*)p + SIZE_T_SIZE);
            }
        }
    }

    return NULL;
}

static void* find_list(size_t size) {
    int index = find_list_index(size);
    if (index < 0 || index >= NUM_LISTS)
        return NULL;
    return root[index];
}

static int find_list_index(size_t size) {
    if (size <= 0)
        return NUM_LISTS;
    else if (size <= 16)
        return 0;
    else if (size <= 32)
        return 1;
    else if (size <= 64)
        return 2;
    else if (size <= 128)
        return 3;
    else if (size <= 256)
        return 4;
    else if (size <= 512)
        return 5;
    else if (size <= 1024)
        return 6;
    else if (size <= 2048)
        return 7;
    else if (size <= 4096)
        return 8;
    else
        return 9;
}

static void* list_init() {
    void* p = (char*)mm_malloc_new(2 * PTR_SIZE) - SIZE_T_SIZE;
    *PREV(p) = p;
    *NEXT(p) = p;
    return p;
}

static void* list_push_front(void* p) {
    void* head = find_list(SIZE(p));
    *PREV(p) = head;
    *NEXT(p) = *NEXT(head);
    *NEXT(*PREV(p)) = p;
    *PREV(*NEXT(p)) = p;
    return p;
}

static void* list_remove(void* p) {
    *NEXT(*PREV(p)) = *NEXT(p);
    *PREV(*NEXT(p)) = *PREV(p);
    *NEXT(p) = NULL;
    *PREV(p) = NULL;
    return p;
}