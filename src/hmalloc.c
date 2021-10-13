// ============================================================================
// hmalloc - husky malloc
// by Oleksandr Litus
// ============================================================================

#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

#include "hmalloc.h"

/* ============================ FUNCTIONS ================================== */
void* hmalloc(size_t bytes);
void  hfree(void* item);
void* hrealloc(void* prev, size_t bytes);

static size_t   div_up(size_t aa, size_t bb);
static long     chunk_list_length();
static void     chunk_list_coalesce();

static void     push_chunk(chunk* chunk_addr);
static chunk*   pop_chunk(size_t chunk_size);
static chunk*   allocate_chunk(int page_count);



/* ============================ GLOBAL VARS ================================ */
const size_t    PAGE_SIZE = 4096;
const size_t    BIG_ALLOC_SIZE = 4096;
const size_t    OVERHEAD_SIZE = sizeof(size_t);
const size_t    CHUNK_SIZE = sizeof(chunk);

static chunk*   head = NULL;            // Initializes chunk list to NULL.

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;



/* ============================= UTILS ===================================== */
/* Divide two longs and round up */
static
size_t
div_up(size_t aa, size_t bb)
{
    return ((aa - 1) / bb) + 1;
}

/* Calculate the length of the chunk list*/
static
long
chunk_list_length()
{
    int len = 0;
    
    chunk* curr = head;
    while(curr != NULL) {
        len += 1;
        curr = curr->next;
    }
    
    assert(len >= 0);
    return len;
}



/* ============================== COALESCE ================================= */
/* Traverse through the list and coalesce chunks */
static
void
chunk_list_coalesce()
{
    // head does not exist, or only head exists
    if (head == 0 || head->next == NULL) {
        return;
    }
    
    // loop to coalesce
    chunk* curr = head;
    chunk* next = head->next;
    while(curr != NULL && next != NULL) {
        chunk* curr_end = (chunk*)(((char*)curr) + curr->size);
        
        if  (curr_end == next) {
            curr->size += next->size;
            curr->next = next->next;
            
            next = curr->next;
        }
        
        else {
            curr = next;
            next = next->next;
        }
    }
    
    return;
}



/* ============================== CHUNK LIST =============================== */
/* Push chunk to the list of chunks (from lowest to greatest address) */
static
void
push_chunk(chunk* ptr)
{
    assert(ptr != NULL);
    
    // chunk address is smaller than head address, or head is NULL
    if (head == NULL || ptr < head) {
        ptr->next = head;
        head = ptr;
        return;
    }
    
    // traverse through the chunk list to find insert position
    chunk* prev = NULL;
    chunk* curr = head;
    while (curr != NULL && ptr > curr) {
        prev = curr;
        curr = curr->next;
    }
    
    // insert chunk at found position
    if (prev != NULL) {
        prev->next = ptr;
        ptr->next = curr;
        return;
    }
}

/* Pop chunk from the list of chunks */
static
chunk*
pop_chunk(size_t size)
{
    assert(size >= CHUNK_SIZE);
    
    chunk* ptr = NULL;
    
    // chunk list is empty, head is NULL
    if (head == NULL) {
        return ptr;
    }
    
    // head can be used as a chunk
    if (size <= head->size) {
        ptr = head;
        head = head->next;
        return ptr;
    }
    
    // traverse through the list of chunks to find big enough chunk
    chunk* prev = NULL;
    chunk* curr = head;
    while (curr != NULL && size > curr->size) {
        prev = curr;
        curr = curr->next;
    }
    
    assert(prev != NULL);
    
    // chunk was found
    if (curr != NULL) {
        
        // cut out the found chunk
        prev->next = curr->next;
        ptr = curr;
    }

    return ptr;
}



/* ============================== SPLITTING ================================ */
/* Split the memory by the size, and push leftover to the chunk list */
static
void
split_chunk(chunk* ptr, size_t size)
{
    assert(size > 0);
    assert(ptr != NULL);
    
    // get the full size
    size_t full_size = ptr->size;
    assert(full_size > 0);
    
    // calc leftover size
    size_t leftover_size = full_size - size;
    assert(full_size >= 0);

    if (leftover_size >= CHUNK_SIZE) {
        
        // get pointer to the leftover memory
        chunk* leftover_ptr = (chunk*)(((char*)ptr) + size);
        
        // add size info to start of the chunk
        leftover_ptr->size = leftover_size;
        
        // push leftover to the chunk list
        push_chunk(leftover_ptr);
    }
    
    // leftover is smaller than CHUNK_SIZE
    else {
        
        // increase allocated size by leftover size
        size += leftover_size;
    }
    
    // add size info to start of the chunk
    ptr->size = size;
}



/* ============================== ALLOCATION =============================== */
/* Allocate chunk with given number of pages */
static
chunk*
allocate_chunk(int page_count)
{
    assert(page_count > 0);
    
    // calc allocation size
    size_t size = page_count * PAGE_SIZE;
    
    // allocate chunk
    chunk* ptr = mmap(NULL, size,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1, 0);
    assert(ptr != MAP_FAILED);
    
    // add size info to start of the chunk
    ptr->size = size;
    
    return ptr;
}



/* ============================== HMALLOC ================================== */
/* Allocate requested amount of memory and return its address */
void*
hmalloc(size_t size)
{
    assert(size > 0);
    
    chunk* ptr = NULL;
    
    size += OVERHEAD_SIZE;
    
    // make sure size is at least CHUNK_SIZE
    size = (size < CHUNK_SIZE) ? CHUNK_SIZE : size;
    
    if (size < BIG_ALLOC_SIZE) {
        pthread_mutex_lock(&mutex);
        
        // try to pop chunk from the list
        ptr = pop_chunk(size);
        
        // in case nothing was found, allocate a new page
        if (ptr == NULL) {
            int page_count = 1;
            ptr = allocate_chunk(page_count);
            assert(ptr != NULL);
        }
        
        // split chunk and push leftover to the list of chunks
        split_chunk(ptr, size);
        assert(ptr != NULL);
        
        pthread_mutex_unlock(&mutex);
    }
    
    // allocation is bigger than BIG_ALLOC
    else {
        
        // allocate multiple pages
        int page_count = div_up(size, PAGE_SIZE);
        ptr = allocate_chunk(page_count);
        assert(ptr != NULL);
    }
    
    // offset pointer by the size of the overhead
    char* user_ptr = ((char*)ptr) + OVERHEAD_SIZE;
    assert(user_ptr != NULL);

    return user_ptr;
}

/* Free the memory of the item at a given address */
void
hfree(void* user_ptr)
{
    assert(user_ptr != NULL);

    // move back user address by the size of allocation info
    chunk* ptr = (chunk*)(((char*)user_ptr) - OVERHEAD_SIZE);
    assert(ptr != NULL);

    // allocated size is a multiple of pages
    if (ptr->size >= PAGE_SIZE) {
        
        // calc number of pages
        int pages_count = div_up(ptr->size, PAGE_SIZE);
        
        // unmap all of the pages
        munmap(ptr, ptr->size);
    } 

    // allocated size is smaller than a page
    else {
        pthread_mutex_lock(&mutex);
        // add chunk to the list
        push_chunk(ptr);
        
        // coalesce chunks in the list
        chunk_list_coalesce();
        pthread_mutex_unlock(&mutex);
    }
}

/* Reallocate the given memory with new size */
void*
hrealloc(void* user_ptr, size_t new_size)
{
    assert(new_size != 0);
    
    // move back user address by the size of allocation info
    chunk* ptr = (chunk*)(((char*)user_ptr) - OVERHEAD_SIZE);
    
    // return new allocation, if alloc_addr is NULL
    if (ptr == NULL) {
        return hmalloc(new_size);
    }
    
    // return the same chunk, if requested size the same or smaller
    if (new_size <= ptr->size) {
        return ptr;
    }
    
    // otherwise make new allocation
    chunk* new_user_ptr = hmalloc(new_size);
    assert(new_user_ptr != NULL);
    
    // copy all data from old allocation to new
    int user_size = ptr->size - OVERHEAD_SIZE;
    memcpy(user_ptr, new_user_ptr, user_size);
    
    // free the old allocation
    hfree(user_ptr);
    
    return new_user_ptr;
}
