/* SMAA's lookup textures as computed at start (src/gs/smaa_tables.h) are the bytes of SMAA's
   Textures/AreaTex.h (its first 80 rows) and Textures/SearchTex.h: FNV-1a hashes of those. */
#include "../src/gs/smaa_tables.h"
#include <stdio.h>

static unsigned fnv(const unsigned char* p, int n)
{
    unsigned h = 2166136261u;
    for (int i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

int main(void)
{
    static unsigned char area[AA_AREA_W * AA_AREA_H * 2], search[AA_SEARCH_W * AA_SEARCH_H];
    unsigned ha, hs;
    aa_area_table(area);
    aa_search_table(search);
    ha = fnv(area, sizeof(area)); hs = fnv(search, sizeof(search));
    printf("area %08x, search %08x\n", ha, hs);
    return ha == 0x42e82928u && hs == 0x0100c405u ? 0 : 1;
}
