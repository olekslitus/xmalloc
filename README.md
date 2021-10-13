# xmalloc
high-performance thread-safe allocator


In this assignment you will build a free-list based memory allocator, Husky Malloc.

Your allocator must provide three functions:

-   void\* hmalloc(size\_t size); // Allocate "size" bytes of memory.
-   void hfree(void\* item); // Free the memory pointed to by "item".
-   void hprintstats(); // Print allocator stats to stderr in a specific format.

Your allocator should maintain a free list of available blocks of memory. This should be a singly linked list sorted by block address.

## hmalloc

To allocate memory B bytes of memory, you should first add sizeof(size\_t) to B to make space to track the size of the block, and then:

For requests with (B < 1 page = 4096 bytes):

-   See if there's a big enough block on the free list. If so, select the first one and remove it from the list.
-   If you don't have a block, mmap a new block (1 page = 4096).
-   If the block is bigger than the request, and the leftover is big enough to store a free list cell, return the extra to the free list.
-   Use the start of the block to store its size.
-   Return a pointer to the block _after_ the size field.

For requests with (B >= 1 page = 4096 bytes):

-   Calculate the number of pages needed for this block.
-   Allocate that many pages with mmap.
-   Fill in the size of the block as (# of pages \* 4096).
-   Return a pointer to the block _after_ the size field.

## hfree

To free a block of memory, first find the beginning of the block by subtracting sizeof(size\_t) from the provided pointer.

If the block is < 1 page then stick it on the free list.

If the block is >= 1 page, then munmap it.

## Tracking stats

Your memory allocator should track 5 stats:

-   pages\_mapped: How many pages total has your allocator requested with mmap?
-   pages\_unmapped: How many pages total has your allocator given back with munmap?
-   chunks\_allocated: How many hmalloc calls have happened?
-   chunks\_freed: How many hfree calls have happend?
-   free\_length: How long is your free list currently? (this can be calculated when stats are requested)

## Coalescing your free list

When inserting items into your free list, you should maintain two invariants:

-   The free list is sorted by memory address of the blocks.
-   Any two adjacent blocks on the free list get coalesced (joined together) into one bigger block.

Since you insert into the free list and need to handle this in two different places, a helper function is a good idea.

## Test Programs
Provided with the assignment download are two test programs:

-   ivec\_main - Collatz Conjecture simulation with dynamic array
-   list\_main - Collatz Conjecture simulation with linked list

A Makefile is included that links these two programs with each of three different memory allocators: the system allocator and two allocators you will need to provide.


- [x] Make it thread safe by adding a single mutex guarding your free list. There should be no data races when calling hmalloc/hfree functions concurrently from multiple threads.
- [x] Implement realloc.
- [x] Edit hw07\_malloc.c in the CH02 starter code to either be or call your updated hmalloc. Yes, it was really hw08, but the file should be “hw07\_malloc.c”.

## Task #2 - Implement an optimized memory allocator

Edit par\_malloc.c to either implement or call an optimized concurrent memory allocator. This allocator should run the test cases as quickly as possible - definitely faster than hw07\_malloc, and optimally faster than sys\_malloc.

The two things that slow down hw07\_malloc are 1.) lock contention and 2.) algorithmic complexity. Your optimized allocator should try to reduce these issues as much as possible.

## Task #3 - Graph & Report

Time all six versions of the program ({sys, hw7, par} X {ivec, list}). Select input values so that you can distinguish the speed differences between the versions.

Do your timing on your local development machine; make sure you have multiple cores available.

Include a graph.png showing your results.

Include a report.txt with:

-   ASCII art table of results
-   Information on your test hardware / OS.
-   Discussion of your strategy for creating a fast allocator.
-   Discussion of your results



## Report
==========================================================
                        SYSTEM INFO
==========================================================
OS: Debian
CPU: 2.7 GHz Intel Core i7
Cores: 2
Memory: 4 GB
----------------------------------------------------------



==========================================================
                       TIME RESULTS
==========================================================
        |   hw07(2000)  |   par(10000)   |    sys(10000)
----------------------------------------------------------
ivec    |      0.54     |      0.06      |      0.07
list    |      6.06     |      0.12      |      0.29
----------------------------------------------------------
        
        

==========================================================
                STRATEGY FOR FAST ALLOCATOR
==========================================================
My strategy for the fast allocator included couple levels
of structural abstraction. The top level struture is
an arena. Each thread gets its own arena, and you can
imagine it being totally sepaerate malloc for each thread.
Each arena locks with a mutex, so other threads find arena
for themself.

Next step was buckets. Instead of one single-list with
all the stored free chunks, I decided to add 11 buckets.
Each bucket has size (power of two) from 16 to 8192.
The last bucket is used for big allocations.

Each of the buckets has three types of lists. First list
is for chunks. The bigest struct in bucket is a page.
Pages aare bigger then system page and are allocated rarely
and store their starting and nding points. Next are blocks,
blocks have overhead to store their size, however can only
be multiples of the chunk size in the bucket. Chunks don't
have overhead, and when freed find their bucket using pages
pointers. Also coelesing combines chunks into blocks,
and blocks into larger blocks.

When user is requesting a space, first I am choosing arena
for thread, if it is not already assigned. Next, I am
finding apropriate bucket for the allocation in that arena.
If bucket has chunks, I return a chunk. Otherwise, if
it has blocks, I slice up a chunk fromn a block. If nothing
is available I allocate one more page. Where new block is
created and chunk is sliced up.
----------------------------------------------------------



==========================================================
                FAST ALLOCATOR RESULTS
==========================================================
My fast allocator is many times faster then single list
allocator from hw07 for both list and ivec. This can be
explained by the absence of locks and increased speed
in getting oe freeing a chunk. In my approach all comamnds
are constant time, as I don't need to traverse through
a list, instead I get chunk from the correct bucket.
My fast allocator consistently beats the system allocator
for the list alocations. In case of the vector, it beats
system 2/3 of the time. This can be expalined in different
ways. System allocator is huge and uses lots of aproaches
similar to mine. However, I believe that my allocator is
slisghtly faster due to increased simplicity of code
and structure. I consider such results impressive,
as I did not try to write allocator specificsally optimized
for the given tests. The aproach I took should be able to
handle any serious allocations of real world programs.
I tested the code, and it seems robust, as nofails
were seen. I learned a lot on this assignment, aspecially
about memory managment and its structure.
----------------------------------------------------------
