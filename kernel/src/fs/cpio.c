#include <fs/cpio.h>
#include <sys/klog.h>
#include <term/ansi.h>
#include <lib/string.h>
#include <stdint.h>

static unsigned int cpio_parse_hex(const char *field, int width)
{
    unsigned int v = 0;

    for (int i = 0; i < width; i++)
    {
        char c = field[i];

        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= c - '0';
        else if (c >= 'a' && c <= 'f')
            v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            v |= c - 'A' + 10;
    }

    return v;
}

static inline void *cpio_align(void *p)
{
    return (void *)(((uintptr_t)p + 3) & ~3);
}

static void cpio_strip_prefix(char *name)
{
    if (name[0] == '.' && name[1] == '/')
    {
        size_t len = strlen(name + 2) + 1;
        memmove(name, name + 2, len);
    }
}

static int cpio_do_file(inode_t *root, const char *path,
                        const void *data, unsigned int filesize)
{
    char parent[256], name[256];
    inode_t *dir, *file;
    const char *slash;
    int err;

    slash = strrchr(path, '/');
    if (slash)
    {
        size_t plen = slash - path;
        size_t nlen;

        if (plen >= sizeof(parent))
            plen = sizeof(parent) - 1;
        memcpy(parent, path, plen);
        parent[plen] = '\0';

        nlen = strlen(slash + 1);
        if (nlen >= sizeof(name))
            nlen = sizeof(name) - 1;
        memcpy(name, slash + 1, nlen);
        name[nlen] = '\0';

        err = vfs_mkdir_p(root, parent, S_IFDIR | 0755);
        if (err)
            return err;

        dir = vfs_resolve(root, parent);
    }
    else
    {
        size_t nlen = strlen(path);

        if (nlen >= sizeof(name))
            nlen = sizeof(name) - 1;
        memcpy(name, path, nlen);
        name[nlen] = '\0';
        dir = root;
    }

    if (!dir)
        return -1;

    err = vfs_create(dir, name, 0644, &file);
    if (err)
        return err;

    if (vfs_write(file, data, filesize, 0) < 0)
        return -1;

    return 0;
}

int cpio_archive_extract(inode_t *root, void *buf, size_t len)
{
    uint8_t *p = buf;
    uint8_t *end = p + len;
    size_t nr_dir = 0, nr_file = 0, nr_skip = 0;

    if (!buf || len < sizeof(cpio_header_t))
    {
        klog("cpio", COL_RED "invalid archive" COL_RESET);
        return -1;
    }

    klog("cpio", "extracting ...");

    while (p + sizeof(cpio_header_t) <= end)
    {
        cpio_header_t *hdr = (cpio_header_t *)p;
        unsigned int mode, namesize, filesize;
        char name[256];
        size_t nlen;
        int err;

        if (memcmp(hdr->c_magic, CPIO_NEWC_MAGIC, 6) != 0)
        {
            klog("cpio", COL_AMBER "bad magic" COL_RESET);
            return -1;
        }

        mode = cpio_parse_hex(hdr->c_mode, 8);
        namesize = cpio_parse_hex(hdr->c_namesize, 8);
        filesize = cpio_parse_hex(hdr->c_filesize, 8);

        nlen = namesize;
        if (nlen >= sizeof(name))
            nlen = sizeof(name) - 1;
        memcpy(name, (uint8_t *)hdr + sizeof(*hdr), nlen);
        name[nlen] = '\0';

        void *data = cpio_align((uint8_t *)hdr + sizeof(*hdr) + namesize);

        if (strcmp(name, "TRAILER!!!") == 0)
            break;

        if (name[0] == '.' &&
            (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
        {
            nr_skip++;
            p = cpio_align((uint8_t *)data + filesize);
            continue;
        }

        cpio_strip_prefix(name);
        klog("cpio", "  %s", name);

        if (S_ISDIR(mode))
        {
            err = vfs_mkdir_p(root, name, mode);
            if (err)
                klog("cpio", COL_AMBER "mkdir %s" COL_RESET, name);
            else
                nr_dir++;
        }
        else if (S_ISREG(mode))
        {
            err = cpio_do_file(root, name, data, filesize);
            if (err)
                klog("cpio", COL_AMBER "file %s" COL_RESET, name);
            else
                nr_file++;
        }
        else
        {
            nr_skip++;
        }

        p = cpio_align((uint8_t *)data + filesize);
    }

    klog("cpio", "extracted %zu dirs, %zu files (%zu skipped)",
         nr_dir, nr_file, nr_skip);
    return 0;
}
