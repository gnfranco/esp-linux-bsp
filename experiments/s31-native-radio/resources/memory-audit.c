/* SPDX-License-Identifier: Apache-2.0 */
/* Read-only inventory of a specific S31 Lab DTB and boot FIT. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

#define HP_START UINT64_C(0x2f000000)
#define HP_END UINT64_C(0x2f080000)
#define ROM_START UINT64_C(0x2f07afb0)
#define PSRAM_START UINT64_C(0x50000000)
#define PSRAM_END UINT64_C(0x51000000)

struct span { uint64_t base, end; };
static void fail(const char *msg)
{
    fprintf(stderr, "AUDIT_REJECT: %s\n", msg);
    exit(EXIT_FAILURE);
}
static void *load_tree(const char *path, int fit, size_t *file_size)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *p;
    if (!f || fseek(f, 0, SEEK_END) || (size = ftell(f)) < 40 ||
        size > 16 * 1024 * 1024 || fseek(f, 0, SEEK_SET))
        fail("cannot read input");
    p = malloc((size_t)size);
    if (!p || fread(p, 1, (size_t)size, f) != (size_t)size || fclose(f))
        fail("read failed");
    /* The kernel's bundled host libfdt omits fdt_check_full(). This
     * tool is for trusted, hash-verified build artifacts, not arbitrary
     * untrusted DTBs. Bound totalsize before following any offsets. */
    if (fdt_version(p) != 17 || fdt_totalsize(p) > (uint32_t)size ||
        fdt_check_header(p))
        fail("invalid flattened tree header");
    *file_size = (size_t)size;
    if (!fit && fdt_totalsize(p) != (uint32_t)size)
        fail("unexpected trailing DTB data");
    return p;
}
static int present(const void *f, int n, const char *name)
{
    int len;
    const void *p = fdt_getprop(f, n, name, &len);
    if (!p && len != -FDT_ERR_NOTFOUND)
        fail("property lookup failed");
    return p != NULL;
}
static int flag(const void *f, int n, const char *name)
{
    int len;
    const void *p = fdt_getprop(f, n, name, &len);
    if (!p && len == -FDT_ERR_NOTFOUND)
        return 0;
    if (!p || len)
        fail("invalid flag property");
    return 1;
}
static uint32_t u32(const void *f, int n, const char *name)
{
    int len;
    const fdt32_t *p = fdt_getprop(f, n, name, &len);
    if (!p || len != 4)
        fail("expected one 32-bit cell");
    return fdt32_to_cpu(*p);
}
static int string_is(const void *f, int n, const char *name, const char *value)
{
    int len;
    const char *p = fdt_getprop(f, n, name, &len);
    return p && len == (int)strlen(value) + 1 && !memcmp(p, value, (size_t)len);
}
static int enabled(const void *f, int n)
{
    if (!present(f, n, "status"))
        return 1;
    return string_is(f, n, "status", "okay") || string_is(f, n, "status", "ok");
}
static struct span range(uint64_t base, uint64_t size)
{
    if (!size || base >= UINT64_C(0x100000000) ||
        size > UINT64_C(0x100000000) - base)
        fail("empty or overflowing RV32 range");
    return (struct span){base, base + size};
}
static struct span reg_pair(const void *f, int n)
{
    int len;
    const fdt32_t *p = fdt_getprop(f, n, "reg", &len);
    if (!p || len != 8)
        fail("expected one address/size pair");
    return range(fdt32_to_cpu(p[0]), fdt32_to_cpu(p[1]));
}
static int overlap(struct span a, struct span b)
{
    return a.base < b.end && b.base < a.end;
}
static void report(const char *name, struct span s)
{
    printf("%s=[0x%08" PRIx64 ",0x%08" PRIx64 ") bytes=%" PRIu64 "\n",
           name, s.base, s.end, s.end - s.base);
}
int main(int argc, char **argv)
{
    void *dtb, *fit;
    int r, n, count = 0, mem_count = 0, fw, len, reservations;
    size_t dtb_size, fit_size;
    uint64_t payload_offset, payload_size;
    struct span pool = {0}, psram = {0}, image;
    const struct span rom = {ROM_START, HP_END};
    if (argc != 3)
        fail("usage: memory-audit BOARD.dtb BOOT.itb");
    dtb = load_tree(argv[1], 0, &dtb_size);
    fit = load_tree(argv[2], 1, &fit_size);
    if (fdt_node_check_compatible(dtb, 0, "esp,esp32s31") ||
        u32(dtb, 0, "#address-cells") != 1 || u32(dtb, 0, "#size-cells") != 1)
        fail("unsupported board root");
    fdt_for_each_subnode(n, dtb, 0) {
        if (enabled(dtb, n) && string_is(dtb, n, "device_type", "memory")) {
            psram = reg_pair(dtb, n);
            ++mem_count;
        }
    }
    if (n != -FDT_ERR_NOTFOUND)
        fail("invalid root children");
    if (mem_count != 1 || psram.base != PSRAM_START || psram.end != PSRAM_END)
        fail("unexpected Linux memory layout");
    r = fdt_path_offset(dtb, "/reserved-memory");
    if (r < 0 || !enabled(dtb, r) || u32(dtb, r, "#address-cells") != 1 ||
        u32(dtb, r, "#size-cells") != 1)
        fail("unsupported reserved-memory layout");
    if (!fdt_getprop(dtb, r, "ranges", &len) || len)
        fail("expected identity reserved-memory ranges");
    fdt_for_each_subnode(n, dtb, r) {
        if (!enabled(dtb, n) || !flag(dtb, n, "linux,dma-default"))
            continue;
        if (fdt_node_check_compatible(dtb, n, "shared-dma-pool") ||
            !flag(dtb, n, "no-map") || present(dtb, n, "reusable"))
            fail("unsupported default DMA pool");
        pool = reg_pair(dtb, n);
        ++count;
    }
    if (n != -FDT_ERR_NOTFOUND)
        fail("invalid reserved-memory children");
    if (count != 1 || pool.base < HP_START || pool.end > HP_END ||
        (pool.base | pool.end) % 4096 || overlap(pool, rom))
        fail("missing, duplicate, misaligned or conflicting DMA pool");
    /* This is a deliberately strict inventory, not a generic DT validator.
     * Refuse unknown or overlapping enabled static/dynamic reservations. */
    fdt_for_each_subnode(n, dtb, r) {
        if (!enabled(dtb, n) || flag(dtb, n, "linux,dma-default"))
            continue;
        if (!present(dtb, n, "reg"))
            fail("unresolved dynamic reservation");
        if (overlap(pool, reg_pair(dtb, n)))
            fail("default DMA pool overlaps another reserved region");
    }
    if (n != -FDT_ERR_NOTFOUND)
        fail("invalid reserved-memory children");
    reservations = fdt_num_mem_rsv(dtb);
    if (reservations < 0)
        fail("invalid reservation map");
    for (int i = 0; i < reservations; ++i) {
        uint64_t base, size;
        if (fdt_get_mem_rsv(dtb, i, &base, &size))
            fail("invalid reservation map");
        if (overlap(pool, range(base, size)))
            fail("default DMA pool overlaps reserve map");
    }
    r = fdt_path_offset(fit, "/configurations");
    if (r < 0 || !string_is(fit, r, "default", "conf-1"))
        fail("unexpected FIT default configuration");
    n = fdt_subnode_offset(fit, r, "conf-1");
    if (n < 0 || !string_is(fit, n, "firmware", "opensbi"))
        fail("unexpected FIT selected firmware");
    fw = fdt_path_offset(fit, "/images/opensbi");
    if (fw < 0 || !string_is(fit, fw, "compression", "none") ||
        u32(fit, fw, "load") != HP_START || u32(fit, fw, "entry") != HP_START)
        fail("unsupported OpenSBI FIT component");
    if (present(fit, fw, "data") || present(fit, fw, "data-position"))
        fail("unsupported FIT payload format");
    payload_offset = ((uint64_t)fdt_totalsize(fit) + 3) & ~UINT64_C(3);
    payload_offset += u32(fit, fw, "data-offset");
    payload_size = u32(fit, fw, "data-size");
    if (payload_offset > fit_size || payload_size > fit_size - payload_offset)
        fail("truncated OpenSBI payload");
    image = range(u32(fit, fw, "load"), payload_size);
    if (image.end > HP_END || overlap(pool, image) || overlap(image, rom))
        fail("OpenSBI payload overlaps DMA or ROM");
    if (pool.base != UINT64_C(0x2f030000) ||
        pool.end != UINT64_C(0x2f070000))
        fail("unexpected Lab DMA pool extent");
    report("HP_SRAM_WINDOW", (struct span){HP_START, HP_END});
    report("ROM_RESERVED_IDF", rom);
    report("OPENSBI_FILE_EXTENT_ONLY", image);
    report("LINUX_DEFAULT_DMA_POOL", pool);
    report("LINUX_PSRAM", psram);
    puts("RADIO_ALLOCATABLE_BYTES=UNKNOWN");
    puts("S31_MEMORY_STATIC_AUDIT_PASS");
    free(fit);
    free(dtb);
    return 0;
}
