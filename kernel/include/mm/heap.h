#ifndef MM_HEAP_H
#define MM_HEAP_H

/* kevins stupid heap*/
/* alogrithm: my head */
/* O(null) */

#include <stdint.h>
#include <stddef.h>

/* basic idea, when we begin we allocate one single block with capacity of PAGE_SIZE (which is minimum) then when we goto alloc we check if there is a big enough block, if not then we make a new block*/

typedef struct block
{
    size_t capacity;
    size_t used;
} block_t;

#endif /* MM_HEAP_H */