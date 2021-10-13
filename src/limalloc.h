/*  LIMALLOC - lit malloc    */
/*  by Oleksandr Litus       */

#ifndef limalloc_h
#define limalloc_h

/* Chunk of memory with specific size */
typedef struct chunk {
    struct chunk*   next;
} chunk;

/* Block of memory with variable size */
typedef struct block {
    size_t          size;
    struct block*   next;
} block;

/* Page represents big part of memory */
typedef struct page {
    struct page*    next;
} page;

/* Bucket to store memory of same size */
typedef struct bucket {
    chunk*  chunk_head;
    block*  block_head;
    page*   page_head;
    size_t  chunk_size;
} bucket;

/* Allocation arena for each thread */
typedef struct arena {
    pthread_mutex_t lock;
    bucket buckets[11];
} arena;

void* limalloc(size_t size);
void  lifree(chunk* ptr);
void* lirealloc(chunk* prev_ptr, size_t new_size);

#endif /* limalloc_h */
