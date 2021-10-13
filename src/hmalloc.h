// ============================================================================
// hmalloc - husky malloc
// by Oleksandr Litus
// ============================================================================

#ifndef hmalloc_h
#define hmalloc_h

#include <stdio.h>

/* Singly-linked list of free memory chunks */
typedef struct chunk {
	size_t          size;
	struct chunk*   next;
} chunk;

void* hmalloc(size_t alloc_size);
void  hfree(void* item);
void* hrealloc(void* prev, size_t alloc_size);

#endif /* hmalloc_h */
