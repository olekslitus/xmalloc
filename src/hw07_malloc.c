#include <stdio.h>
#include <string.h>

#include "xmalloc.h"
#include "hmalloc.h"

void*
xmalloc(size_t bytes)
{
    return hmalloc(bytes);
    return 0;
}

void
xfree(void* ptr)
{
    hfree(ptr);
}

void*
xrealloc(void* prev, size_t bytes)
{
    return hrealloc(prev, bytes);
    return 0;
}
