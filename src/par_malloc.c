#include <stdlib.h>
#include <unistd.h>

#include "xmalloc.h"
#include "limalloc.h"

void*
xmalloc(size_t bytes)
{
    return limalloc(bytes);
    return 0;
}

void
xfree(void* ptr)
{
    lifree(ptr);
}

void*
xrealloc(void* prev, size_t bytes)
{
    return lirealloc(prev, bytes);
    return 0;
}
