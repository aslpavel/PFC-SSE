// stdlib
#include <stdint.h>
#include <stdio.h>

// sse
#include <smmintrin.h>

// stl
#include <iostream>

typedef uint32_t ui32;
typedef uint64_t ui64;
typedef uint8_t ui8;

typedef ui64 (* f_unpack) ( ui8 mask, ui64 prev, ui8* buf, ui64* table );

using namespace std;

ui64 simple_unpack( ui8 mask, ui64 prev, ui8* buf, ui64* table )
{
    // mine
    ui64 item = prev;

    // unedited
    ui64 item_flag = 0, item_mask = ~(ui64)0;
    ui32 shift = 0;
    while(mask) { // might use sse here
        item_flag |= ui64(mask & 1? (ui8)*buf++ : 0) << shift;
        item_mask ^= ui64(mask & 1? 255 : 0) << shift;
        mask >>= 1;
        shift += 8;
    }
    item = item & item_mask | item_flag;

    return item;
}

ui64 sse_unpack( ui8 mask, ui64 prev, ui8* buff, ui64* table )
{
    // init
    ui64 shuffle = table[ mask*2 ];
    ui64 blend = table[ mask*2 + 1 ];
    ui64 data = * (ui64*) buff;
    ui64 item;

    // debug
    printf("shuffle: %lx, blend: %lx, data: %lx\n ", shuffle, blend, data);

    // body
    /*
        // ssse3
        pshufb -> _mm_shuffle_pi8
        // sse4.1
        pblendvb -> _mm_blendv_epi8
    */
    asm (
        // set registers to zero
        "pxor       %%xmm0, %%xmm0\n"
        "pxor       %%xmm1, %%xmm1\n"
        "pxor       %%xmm2, %%xmm2\n"

        // shuffle
        "movq       %1, %%xmm1\n"       // data -> xmm1
        "movq       %2, %%xmm0\n"       // shuffle -> xmm0
        "pshufb     %%xmm0, %%xmm1\n"   // shuffle bytes -> xmm1
        // blend
        "pxor       %%xmm0, %%xmm0\n"   // xmm0 = 0
        "movq       %3, %%xmm0\n"       // mask -> xmm0
        "pxor       %%xmm2, %%xmm2\n"   // xmm2 = 0
        "movq       %4, %%xmm2\n"       // prev -> xmm2
        "pblendvb   %%xmm1, %%xmm2\n"   // update previos with changed data
        // return value
        "movq       %%xmm2, %0\n"
        : "=r"(item)
        : "r"(data), "r"(shuffle), "r"(blend), "r"(prev)
        : "xmm0", "xmm1", "xmm2"
   );

    return item;
}

int main()
{
    // Generate lookup table ( shuffle, blend )
    ui64 table[512];
    for ( ui8 mask = 0; mask < 255; mask++ )
    {

        ui64 shuffle = 0, blend = 0, copy = 0;

        for ( int b = 0; b < 8; b++ )
        {
            // 0x80 - highest bit in byte
            // pblendvb -> menas copy
            // pshufb   -> set to zero

            // copy needed
            if ( (mask >> b) & 0x1 )
            {
                // update byte
                blend |= 0xffL << 8*b;
                // copy byte from copy postition
                shuffle |= copy << 8*b;

                copy += 1;

            } else {
                // set byte to zero
                shuffle |= 0x80L << 8*b ;
            }
        }

        table[ mask*2 ] = shuffle;
        table[ mask*2 + 1 ] = blend;
    }

    // test values
    ui8 mask = 234;                     // 0b 1 1  1 0  1 0  1 0
    ui64 prev = ~(ui64) 0;              // 0x FFFF FFFF FFFF FFFF
    ui64 rbuf = 0xffffff1122334455;     // -> 55 44 33 22 11 FF FF FF
    ui8* buf = (ui8*) &rbuf;
    // () => 0x 11 22 33 ff 44 ff 55 ff

    f_unpack simple = simple_unpack;
    f_unpack sse = sse_unpack;

    cout << "Simple Unpack" << endl;
    printf("%lX => %lX\n", prev, simple( mask, prev, buf, table ));

    cout << "SSE Unpack" << endl;
    printf("%lX => %lX\n", prev, sse( mask, prev, buf, table ));

    return 0;
}
