/* Writes the application icon into an already-linked executable, so that
   Explorer, the Alt+Tab list and a desktop shortcut all show it.

       seticon <path-to-exe>

   TinyCC has no resource compiler, so the icon cannot be linked in at build
   time. Rather than performing surgery on the PE by hand -- appending a
   .rsrc section, patching the section table and the data directory --
   this uses BeginUpdateResource, which is the API Windows provides for
   exactly this and gets all of that right on its own.

   The images come from the same src/icon.c the running program draws with,
   so the file and the window can never drift apart.

   Run as the last step of build.cmd. Not part of the shipped executable. */

#include <stdio.h>
#include "../src/icon.c"

#ifndef RT_ICON
#define RT_ICON       MAKEINTRESOURCEW(3)
#define RT_GROUP_ICON MAKEINTRESOURCEW(14)
#endif

/* Which sizes to store, and why not more.

   An icon resource holds raw 32-bit pixels: no compression is available to
   us, since the PNG form the format also allows would mean writing a DEFLATE
   encoder. A 256 px entry is 262 KB on its own and 128 px is another 66 KB,
   which would take a 62 KB program to 444 KB. That is a poor trade for a view
   most people never open.

   What is here covers every size Windows asks for at 100, 125, 150 and 200
   percent scaling exactly, for 26 KB. Explorer's Large and Extra Large views
   scale up from 48 -- and 96 is a clean doubling of it -- which a flat shape
   with no fine detail survives well. */
static const int SIZES[] = {16, 20, 24, 32, 40, 48};
#define N_SIZES (int)(sizeof SIZES / sizeof SIZES[0])

/* An icon image inside a PE is a BITMAPINFOHEADER whose height counts the
   colour bitmap and the mask together, then the colour bits bottom-up, then
   a 1-bit mask padded to four bytes a row. */
static int build_image(unsigned char *out, int size)
{
    static unsigned char px[256 * 256 * 4];
    BITMAPINFOHEADER h;
    int mask_stride = ((size + 31) / 32) * 4;
    int colour_bytes = size * size * 4;
    int mask_bytes = mask_stride * size;
    int x, y, i;
    unsigned char *p;

    icon_render(px, size, ICON_BRAND, 1);

    for (i = 0; i < (int)sizeof h; i++) ((unsigned char *)&h)[i] = 0;
    h.biSize = sizeof h;
    h.biWidth = size;
    h.biHeight = size * 2;      /* colour plus mask, as the format demands */
    h.biPlanes = 1;
    h.biBitCount = 32;
    h.biCompression = BI_RGB;
    h.biSizeImage = (DWORD)colour_bytes;

    p = out;
    for (i = 0; i < (int)sizeof h; i++) *p++ = ((unsigned char *)&h)[i];

    /* bottom-up */
    for (y = size - 1; y >= 0; y--)
        for (x = 0; x < size; x++) {
            unsigned char *s = px + ((size_t)y * size + x) * 4;
            *p++ = s[0]; *p++ = s[1]; *p++ = s[2]; *p++ = s[3];
        }

    /* The alpha channel is what actually gets used, but a correct mask still
       matters to the older drawing paths, and a wrong one shows up as a black
       box behind the icon. */
    for (y = size - 1; y >= 0; y--) {
        unsigned char row[64];
        for (i = 0; i < mask_stride; i++) row[i] = 0;
        for (x = 0; x < size; x++) {
            unsigned char alpha = px[((size_t)y * size + x) * 4 + 3];
            if (alpha < 128) row[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
        }
        for (i = 0; i < mask_stride; i++) *p++ = row[i];
    }

    (void)mask_bytes;   /* the running total is the answer; this documents it */
    return (int)(p - out);
}

/* The group is a small directory telling Windows which sizes exist and what
   resource id each one lives under. It is written byte by byte: the on-disk
   entries are packed to 14 bytes and a compiler is free to pad a struct. */
static int build_group(unsigned char *out, const int *sizes, const int *bytes,
                       int n)
{
    unsigned char *p = out;
    int i;

    *p++ = 0; *p++ = 0;            /* reserved */
    *p++ = 1; *p++ = 0;            /* type 1 = icon */
    *p++ = (unsigned char)n; *p++ = (unsigned char)(n >> 8);

    for (i = 0; i < n; i++) {
        int s = sizes[i];
        *p++ = (unsigned char)(s >= 256 ? 0 : s);   /* 0 means 256 */
        *p++ = (unsigned char)(s >= 256 ? 0 : s);
        *p++ = 0;                  /* colours in palette: none, it is 32-bit */
        *p++ = 0;                  /* reserved */
        *p++ = 1; *p++ = 0;        /* planes */
        *p++ = 32; *p++ = 0;       /* bits per pixel */
        *p++ = (unsigned char)(bytes[i]);
        *p++ = (unsigned char)(bytes[i] >> 8);
        *p++ = (unsigned char)(bytes[i] >> 16);
        *p++ = (unsigned char)(bytes[i] >> 24);
        *p++ = (unsigned char)(i + 1);              /* resource id */
        *p++ = (unsigned char)((i + 1) >> 8);
    }

    return (int)(p - out);
}

static void widen(const char *in, WCHAR *out, int cap)
{
    int i = 0;
    while (in[i] && i < cap - 1) { out[i] = (WCHAR)(unsigned char)in[i]; i++; }
    out[i] = 0;
}

int main(int argc, char **argv)
{
    static unsigned char image[256 * 256 * 4 + 64 * 256 + 64];
    static unsigned char group[6 + 14 * N_SIZES];
    static unsigned char *kept[N_SIZES];
    int bytes[N_SIZES];
    WCHAR path[MAX_PATH];
    HANDLE upd;
    int i, glen;

    if (argc < 2) {
        printf("usage: seticon <path-to-exe>\n");
        return 2;
    }
    widen(argv[1], path, MAX_PATH);

    upd = BeginUpdateResourceW(path, FALSE);
    if (!upd) {
        printf("seticon: cannot open %s for update (error %u)\n",
               argv[1], (unsigned)GetLastError());
        return 1;
    }

    for (i = 0; i < N_SIZES; i++) {
        int n = build_image(image, SIZES[i]);
        bytes[i] = n;
        /* UpdateResource copies what it is given, so one buffer would do --
           but the group has to quote the exact byte count of each image, and
           those are only known once every image is built. */
        kept[i] = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n);
        if (!kept[i]) { printf("seticon: out of memory\n"); return 1; }
        {
            int k;
            for (k = 0; k < n; k++) kept[i][k] = image[k];
        }
    }

    for (i = 0; i < N_SIZES; i++) {
        if (!UpdateResourceW(upd, (LPCWSTR)RT_ICON,
                             (LPCWSTR)MAKEINTRESOURCEW(i + 1),
                             MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                             (LPVOID)kept[i], (DWORD)bytes[i])) {
            printf("seticon: UpdateResource failed for %d px (error %u)\n",
                   SIZES[i], (unsigned)GetLastError());
            EndUpdateResourceW(upd, TRUE);
            return 1;
        }
    }

    glen = build_group(group, SIZES, bytes, N_SIZES);

    /* Id 1: Explorer shows the group with the lowest id, so this is the one
       the file itself gets. */
    if (!UpdateResourceW(upd, (LPCWSTR)RT_GROUP_ICON,
                         (LPCWSTR)MAKEINTRESOURCEW(1),
                         MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                         (LPVOID)group, (DWORD)glen)) {
        printf("seticon: UpdateResource failed for the group (error %u)\n",
               (unsigned)GetLastError());
        EndUpdateResourceW(upd, TRUE);
        return 1;
    }

    if (!EndUpdateResourceW(upd, FALSE)) {
        printf("seticon: could not commit (error %u)\n",
               (unsigned)GetLastError());
        return 1;
    }

    printf("  icon written into %s (%d sizes)\n", argv[1], N_SIZES);
    return 0;
}
