/*  LIMALLOC - lit malloc    */
/*  by Oleksandr Litus       */

#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

#include "limalloc.h"


/* ============================= GLOBALS =================================== */
#define ARENA_COUNT 8

static const size_t  CHUNK_SIZE       = sizeof(chunk);
static const size_t  BLOCK_SIZE       = sizeof(block);
static const size_t  OVERHEAD_SIZE    = sizeof(size_t);
static const size_t  MEM_PAGE_SIZE    = 1024 * 1024;
static const size_t  PAGE_SIZE        = 4096;
static const size_t  MAX_BUCKET_SIZE  = 8192;
static const int     BUCKET_COUNT     = 11;

static int FISRT_RUN = 1;

static __thread arena*   __arena    = NULL;
static __thread bucket*  __bucket   = NULL;

static arena arenas[ARENA_COUNT];


/* ============================= FUNCTIONS ================================= */
static size_t div_up(size_t aa, size_t bb);
static void init_malloc();

static int arena_trylock(arena* arena_ptr);

static void __unlock_arena();
static void __assign_arena();
static void __choose_bucket(size_t size);
static void __find_bucket(chunk* ptr);

static chunk* block_slice();

static chunk* pop_big_block(size_t size);
static chunk* allocate_big_block(size_t size);
static chunk* pop_chunk();
static chunk* allocate_page();

static chunk* get_chunk(size_t size);
void* limalloc(size_t size);

static void clean_bucket();
static void free_chunk(chunk* ptr);
void lifree(chunk* ptr);

void* lirealloc(chunk* prev_ptr, size_t new_size);



/* ============================= UTILS ===================================== */
/* Divide two longs and round up */
static
size_t
div_up(size_t aa, size_t bb)
{
    return ((aa - 1) / bb) + 1;
}


/* Initialize all structs in malloc */
static
void
init_malloc()
{
    for (int aa = 0; aa < ARENA_COUNT; ++aa) {
        
        // initialize mutex lock in each arena
        pthread_mutex_init(&(arenas[aa].lock), NULL);
        
        // initialize all buckets
        for (int bb = 0; bb < BUCKET_COUNT; ++bb) {
            arenas[aa].buckets[bb].chunk_head = NULL;
            arenas[aa].buckets[bb].block_head = NULL;
            arenas[aa].buckets[bb].page_head = NULL;
            arenas[aa].buckets[bb].chunk_size = (8 << bb);
        }
    }
    
    FISRT_RUN = 0;
}



/* ============================= ARENA ===================================== */
/* Try to lock the given arena */
static
int
arena_trylock(arena* arena_ptr)
{
    assert(arena_ptr != NULL);
    return pthread_mutex_trylock(&(arena_ptr->lock)) == 0;
}


/* Unlock the thread arena */
static
void
__unlock_arena()
{
    assert(__arena != NULL);
    pthread_mutex_unlock(&(__arena->lock));
}


/* Assign a new arena to the current thread in global __arena */
static
void
__assign_arena()
{
    // go through the arena array and find unlocked arena, assign thread to it
    for (int aa = 0; aa < ARENA_COUNT; ++aa) {
        
        arena* curr = &(arenas[aa]);
        if (arena_trylock(curr)) {
            __arena = curr;
            assert(__arena != NULL);
            break;
        }
    }
}



/* ============================= BUCKET ==================================== */
/* Chooses bucket for the allocation and assigns it to global __bucket */
static
void
__choose_bucket(size_t size)
{
    assert(size >= CHUNK_SIZE);
    assert(__arena != NULL);
    
    // traverse through buckets untill you find apropriate size
    for (int bb = 1; bb < 11; ++bb) {
        
        bucket* curr = &(__arena->buckets[bb]);
        if (size <= curr->chunk_size) {
            __bucket = curr;
            assert(__bucket != NULL);
            return;
        }
    }
    
    // otherwise, big bucket is chosen
    __bucket = &(__arena->buckets[0]);
    assert(__bucket != NULL);
}

/* Finds original bucket of the chunk, and assigns it to the __bucket */
static
void
__find_bucket(chunk* ptr)
{
    assert(__arena != NULL);
    
    char* char_ptr = (char*)ptr;
    
    for (int bb = 1; bb < BUCKET_COUNT; ++bb) {
        
        bucket* curr = &(__arena->buckets[bb]);
        
        page* curr_page = curr->page_head;
        while (curr_page != NULL) {
            
            chunk* start = (chunk*)curr_page;
            chunk* end = (chunk*)((char*)curr_page + MEM_PAGE_SIZE);
            
            if (ptr > start && ptr < end) {
                __bucket = curr;
                return;
            }
            
            curr_page = curr_page->next;
        }
    }
    
    // otherwise it is a big allocation
    __bucket = &(__arena->buckets[0]);
}


/* ============================ SLICING ==================================== */
/* Slice of a chunk from the first block in the bucket,
 push the rest into either block list or chunk list */
static
chunk*
block_slice()
{
    assert(__bucket != NULL);
    assert(__arena != NULL);
    assert(__bucket->block_head != NULL);
    
    chunk* ptr          = (chunk*)__bucket->block_head;
    block* old_block    = __bucket->block_head;

    int old_block_size  = old_block->size;
    int chunk_size      = __bucket->chunk_size;
    int new_size        = old_block_size - chunk_size;
    
    
    // after slicing, new block is too small
    if (new_size < chunk_size) {
        __bucket->block_head = old_block->next;
    }
     
    // after slicing, new block is a chunk
    else if (new_size == chunk_size) {
        __bucket->block_head = old_block->next;
        __bucket->chunk_head = (chunk*)(((char*)old_block) + chunk_size);
    }
    
    // after slicing, new block is a smaller block
    else {
        __bucket->block_head = (block*)(((char*)old_block) + chunk_size);
        __bucket->block_head->size = new_size;
    }

    return ptr;
}



/* ============================ BIG ALLOCATION ============================= */
/* Try to pop big block from the bucket */
static
chunk*
pop_big_block(size_t size)
{
    assert(size >= BLOCK_SIZE);
    assert(__bucket != NULL);
    assert(__arena != NULL);
    
    chunk* user_ptr = NULL;
    block* ptr = NULL;
    block* head = __bucket->block_head;
    
    // bucket is empty, head is NULL
    if (head == NULL) {
        return NULL;
    }
    
    // head can be used
    if (size <= head->size) {
        ptr = head;
        __bucket->block_head = head->next;
        user_ptr = (chunk*)(((char*)ptr) + OVERHEAD_SIZE);
        return user_ptr;
    }
    
    // traverse through the list of blocks to find big enough block
    block* prev = NULL;
    block* curr = head;
    while (curr != NULL && size > curr->size) {
        prev = curr;
        curr = curr->next;
    }
    
    assert(prev != NULL);
    
    // chunk was found
    if (curr != NULL) {
        
        // cut out the found block
        prev->next = curr->next;
        ptr = curr;
    }
    
    // cast bucket into chunk for the user
    if (ptr != NULL) {
        user_ptr = (chunk*)(((char*)ptr) + OVERHEAD_SIZE);
    }
    
    return user_ptr;
}

/* Allocate big block of teh given size */
static
chunk*
allocate_big_block(size_t size)
{
    assert(size >= BLOCK_SIZE);
    
    // calc number of pages to allocate
    int page_count = div_up(size, PAGE_SIZE);
    
    // calc allocation size
    size_t alloc_size = page_count * PAGE_SIZE;
    
    // allocate block
    block* ptr = mmap(NULL, alloc_size,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1, 0);
    assert(ptr != MAP_FAILED);
    
    // add size info to start of the block
    ptr->size = size;
    
    // cast bucket into chunk for the user
    chunk* user_ptr = (chunk*)(((char*)ptr) + OVERHEAD_SIZE);
    
    return user_ptr;
}



/* ========================== STANDART ALLOCATION ========================== */
/* Pop chunk of standart bucket size */
static
chunk*
pop_chunk()
{
    assert(__bucket != NULL);
    assert(__arena != NULL);
    
    chunk* ptr = NULL;
    
    // there are free chunks in the bucket
    if (__bucket->chunk_head != NULL) {
        // get the first one in the list
        ptr = __bucket->chunk_head;
        __bucket->chunk_head = __bucket->chunk_head->next;
        return ptr;
    }
    
    // there are free blocks in the bucket
    if (__bucket->block_head != NULL) {
        // slice a chunk of the first block in the list
        ptr = block_slice();
        return ptr;
    }
    
    // nothing was found
    return ptr;
}

/* Allocates new block of free space, returns the first chunk */
static
chunk*
allocate_page()
{
    assert(__bucket != NULL);
    assert(__arena != NULL);
    assert(__bucket->block_head == NULL);
    
    // allocate block
    page* ptr = mmap(NULL, MEM_PAGE_SIZE,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1, 0);
    assert(ptr != MAP_FAILED);
    
    // add page to the list of pages
    if (__bucket->page_head == NULL) {
        __bucket->page_head = ptr;
    }
    else {
        __bucket->page_head->next = ptr;
    }
    
    block* block_ptr = (block*)(((char*)ptr) + sizeof(page));
    
    // add size and next to start of the block
    block_ptr->size = MEM_PAGE_SIZE - sizeof(page);
    block_ptr->next = NULL;
    
    // add block to the list of blocks
    if (__bucket->block_head == NULL) {
        __bucket->block_head = block_ptr;
    }
    else {
        block_ptr->next = block_ptr->next;
        __bucket->block_head = block_ptr;
    }
    
    // slice a chunk of the first block in the list
    chunk* user_ptr = block_slice();
    
    return user_ptr;
}




/* ============================= MALLOC ==================================== */
/* Pop chunk from the bucket or allocate new page */
static
chunk*
get_chunk(size_t size)
{
    assert(__arena != NULL);
    assert(__bucket != NULL);
    assert(size >= CHUNK_SIZE);
    
    chunk* ptr = NULL;
    
    // big allocation
    if (__bucket->chunk_size == 0) {
        ptr = pop_big_block(size);
        
        if (ptr == NULL) {
            ptr = allocate_big_block(size);
        }
    }
    
    // standart allocation
    else {
        ptr = pop_chunk();

        if (ptr == NULL) {
            ptr = allocate_page();
        }
    }
    
    return ptr;
}


/* Allocate requested number of bytes on heap */
void*
limalloc(size_t size)
{
    assert(size > 0);
        
    // initialize malloc structures at first run
    if (FISRT_RUN) init_malloc();
    
    // make sure arena is assigned to the current thread
    if (__arena == NULL) __assign_arena();
    assert(__arena != NULL);
    
    // make sure size at least CHUNK_SIZE
    size = (size < CHUNK_SIZE) ? CHUNK_SIZE : size;
    
    // choose apropriate bucket for the allocation
    __choose_bucket(size);
    assert(__bucket != NULL);
    
    // get a pointer to the chunk
    chunk* ptr = get_chunk(size);
    
    return ptr;
}



/* ============================= FREE ====================================== */
/* Free given chunk */
static
void
free_chunk(chunk* ptr)
{
    assert(ptr != NULL);
    assert(__arena != NULL);
    assert(__bucket != NULL);
    
    // big allocation
    if (__bucket->chunk_size == 0) {
        
        block* block_ptr = (block*)(((char*)ptr) - OVERHEAD_SIZE);
        block_ptr->next = __bucket->block_head;
        __bucket->block_head = block_ptr;
    }
    
    // standart allocation
    else {
        // add chunk to the chunk list of the bucket
        ptr->next = __bucket->chunk_head;
        __bucket->chunk_head = ptr;
    }
}

/* Free the givsen item from memory */
void
lifree(chunk* ptr)
{
    assert(ptr != NULL);
    assert(__arena != NULL);
    assert(__bucket != NULL);
    
    // find the original bucket of the chunk
    __find_bucket(ptr);
    
    // free chunk
    free_chunk(ptr);
}



/* ============================= REALLOC =================================== */
/* Reallocate the prev with new size */
void*
lirealloc(chunk* prev_ptr, size_t new_size)
{
    assert(prev_ptr != NULL);
    assert(new_size > 0);
    
    // find the original bucket of the chunk
    __find_bucket(prev_ptr);
    
    // prev size of the allocation
    size_t prev_size;
    
    // big allocation
    if (__bucket->chunk_size == 0) {
        block* block_ptr = (block*)(((char*)prev_ptr) - OVERHEAD_SIZE);
        prev_size = block_ptr->size;
    }
    
    // standart allocation
    else {
        prev_size = __bucket->chunk_size;
    }
    
    // check if there is enough space in the curr chunk
    // if there is, return it back
    if (prev_size >= new_size) {
        return prev_ptr;
    }
    
    // NOTE: assuming realloc is used on vectors a lot,
    // it makes sense to give more space on first reallocation,
    // so that in future, malloc won't need to allocate new space
    new_size = (new_size < PAGE_SIZE) ? PAGE_SIZE : new_size;
    
    // if there isn't enough space allocate new space
    chunk* new_ptr = limalloc(new_size);
    
    // copy memory from old ptr to new_ptr
    memcpy(prev_ptr, new_ptr, prev_size);
    
    // free the old chunk
    lifree(prev_ptr);
    
    return new_ptr;
}
