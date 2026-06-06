#include <lib/heap.h>
#include <stdint.h>
#include <stddef.h>

#define HEAP_POOL_SIZE 0x400000  /* 4 MB */

typedef struct hdr
{
    size_t size;
    int free;
    struct hdr *next;
} hdr_t;

#define HDR_SZ sizeof(hdr_t)
#define MIN_BLOCK 64

static char pool[HEAP_POOL_SIZE];
static int inited;

void *malloc(size_t size)
{
    if (!size)
        return NULL;

    if (!inited)
    {
        inited = 1;
        hdr_t *h = (hdr_t *)pool;
        h->size = HEAP_POOL_SIZE - HDR_SZ;
        h->free = 1;
        h->next = NULL;
    }

    size = (size + 7) & ~(size_t)7;

    hdr_t *h = (hdr_t *)pool;
    while (h)
    {
        if (h->free && h->size >= size)
        {
            if (h->size >= size + HDR_SZ + MIN_BLOCK)
            {
                hdr_t *n = (hdr_t *)((uint8_t *)(h + 1) + size);
                n->size = h->size - size - HDR_SZ;
                n->free = 1;
                n->next = h->next;
                h->size = size;
                h->next = n;
            }
            h->free = 0;
            return (void *)(h + 1);
        }
        h = h->next;
    }

    return NULL;
}

void free(void *ptr)
{
    if (!ptr)
        return;

    hdr_t *h = (hdr_t *)ptr - 1;
    h->free = 1;

    /* coalesce forward */
    if (h->next && h->next->free)
    {
        h->size += HDR_SZ + h->next->size;
        h->next = h->next->next;
    }

    /* coalesce backward — scan from head */
    hdr_t *p = (hdr_t *)pool;
    while (p && p->next != h)
        p = p->next;
    if (p && p->free)
    {
        p->size += HDR_SZ + h->size;
        p->next = h->next;
    }
}
