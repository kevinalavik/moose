#include <fs/cpio.h>
#include <sys/klog.h>
#include <term/ansi.h>
#include <lib/string.h>
#include <stdint.h>

static inline unsigned int hex8(const char *s)
{
    unsigned int v = 0;

    for (int i = 0; i < 8; i++)
    {
        v <<= 4;
        char c = s[i];

        if (c >= '0' && c <= '9')
            v |= (c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= (c - 'A' + 10);
    }

    return v;
}

#define ALIGN4(x) ((uint8_t *)(((uintptr_t)(x) + 3) & ~3))

void cpio_parse(void *buf, size_t len)
{
    if (!buf || len < sizeof(cpio_header_t))
    {
        klog("cpio", COL_RED "invalid archive" COL_RESET);
        return;
    }

    uint8_t *p = (uint8_t *)buf;
    uint8_t *end = p + len;

    while (p + sizeof(cpio_header_t) < end)
    {
        cpio_header_t *h = (cpio_header_t *)p;

        if (memcmp(h->c_magic, "070701", 6) != 0)
        {
            klog("cpio", COL_AMBER "bad magic" COL_RESET);
            return;
        }

        unsigned int namesize = hex8(h->c_namesize);
        unsigned int filesize = hex8(h->c_filesize);

        p += sizeof(cpio_header_t);

        char *name = (char *)p;
        p += namesize;
        p = ALIGN4(p);

        if (strcmp(name, "TRAILER!!!") == 0)
        {
            klog("cpio", "cpio end");
            return;
        }

        void *data = p;
        p += filesize;
        p = ALIGN4(p);

        if (name[0] == '.')
            continue; /* skip the . entryu */
        klog("cpio", "found initrd entry: %s", name);
    }
}