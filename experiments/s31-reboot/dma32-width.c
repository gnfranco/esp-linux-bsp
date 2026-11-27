/* Compile-only proof of the unsigned-long calculation in riscv/mm/init.c. */
#define SZ_1G 0x40000000
#define S31_RAM_END 0x51000000UL
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#if __riscv_xlen == 32
_Static_assert(sizeof(unsigned long) == 4, "RV32 ABI");
_Static_assert(4UL * SZ_1G == 0, "unsigned long wraps at 4 GiB");
_Static_assert(MIN(4UL * SZ_1G, S31_RAM_END) == 0, "legacy limit");
#elif __riscv_xlen == 64
_Static_assert(sizeof(unsigned long) == 8, "RV64 ABI");
_Static_assert(4UL * SZ_1G == 0x100000000ULL, "4 GiB representable");
_Static_assert(MIN(4UL * SZ_1G, S31_RAM_END) == S31_RAM_END, "legacy limit");
#else
#error unknown ABI
#endif
unsigned long s31_legacy_dma32_limit(void)
{
    return MIN(4UL * SZ_1G, S31_RAM_END);
}
