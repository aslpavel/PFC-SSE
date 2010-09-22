// stdlib
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

// stl
#include <iostream>

// arcadia typedefs
typedef uint32_t ui32;
typedef uint64_t ui64;
typedef uint16_t ui16;
typedef uint8_t ui8;

using namespace std;

#define FORCE_INLINE __attribute__((always_inline))
#define BENCHMARK 1
#define SLOW_FETCH 0

// SSE version
FORCE_INLINE ui64 sse_unpack( ui8 mask, ui64 prev, ui8* buff, ui64* table )
{
    ui64 shuffle = table[ mask ], item;
    ui8  count = ( shuffle >> 3 ) & 7;
#if SLOW_FETCH
    ui64 data = 0;
    for ( int i = 0; i < count; i++ )
        data |= *(ui64*)(buff + i) << 8 * i;
    printf("data: %lx, buff: %lx, count: %d\n", data, *(ui64*)buff, count);
#endif

    // pshufb   ~ _mm_shuffle_pi8
    // pblendvb ~ _mm_blendv_epi8
    asm (
        // shuffle
        "movq       (%1), %%xmm1\n"     // data     -> xmm1
        "movq       %2, %%xmm0\n"       // smask    -> xmm0
        "pshufb     %%xmm0, %%xmm1\n"   // shuffle  -> xmm1
        // blend
        "shl        $1, %2\n"           // smask -> bmask
        "movq       %2, %%xmm0\n"       // bmask -> xmm0
        "movq       %3, %%xmm2\n"       // prev  -> xmm2
        "pblendvb   %%xmm1, %%xmm2\n"   // blned( mask, data, prev ) -> xmm2
        // return value
        "movq       %%xmm2, %0\n"       // xmm2 -> item
        : "=r"(item)
        : "r"(buff), "r"(shuffle), "r"(prev)
        : "xmm0", "xmm1", "xmm2"
    );

    // debug
    /*
    printf("shuffle: %lx, prev: %lx, item: %lx, count %d\n ",
            shuffle, prev, item, count);
    */

    return item;
}

// Plain C version
FORCE_INLINE ui64 simple_unpack( ui8 mask, ui64 prev, ui8* buf, ui64* table )
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

int main()
{
    // Generate mask table
    ui64 table[256];
    for ( ui8 mask = 0; mask < 255; mask++ )
    {
        ui64 shuffle = 0, blend = 0, count = 0;
        for ( int b = 0; b < 8; b++ )
        {
            /*
             *  Mask Format:
             *   0 1 2 3 4 5 6 7
             *  |X.X.X.O.O.O.X.X|
             *  |     |     | |
             *  |     |     | ^^^
             *  |     |     | Shuffle set 0 bit
             *  |     |     ^^^
             *  |     |     Mask bit
             *  |     ^^^^^^
             *  |     Count of bytes to copy ( only first byte )
             *  ^^^^^^^
             *  Shuffle shift bits
             */

            // copy needed
            if ( (mask >> b) & 0x1 )
            {
                // set blend bit
                blend |= 0x40L << 8*b;
                // set suffle shift
                shuffle |= count << 8*b;
                // update count shift
                count += 1;
            } else {
                // set byte to zero
                shuffle |= 0x80L << 8*b ;
            }
        }
        table[ mask ] = shuffle | blend | ( count << 3 );
    }

    // test values
    volatile ui8 mask = 234;            // 0b 1 1  1 0  1 0  1 0
    ui64 prev = ~(ui64) 0;              // 0x FFFF FFFF FFFF FFFF
    ui64 rbuf = 0xffffff1122334455;     // -> 55 44 33 22 11 FF FF FF
    ui8* buf = (ui8*) &rbuf;
    volatile ui64 out = 0;
    // () => 0x 11 22 33 ff 44 ff 55 ff

    cout << ":: Test\n";
    printf("\tSSE:\t%lX => %lX\n",
           prev, sse_unpack( mask, prev, buf, table ));
    printf("\tSimple:\t%lX => %lX\n",
           prev, simple_unpack( mask, prev, buf, table ));

#if BENCHMARK
    // Benchmark
    uint64_t count = 10000000;
    printf("\n:: Benchmark ( Runs: %ld )\n", count);
    struct timeval start, end;

    // SSE
    gettimeofday(&start, 0);
    for( uint64_t i = 0; i < count; i++ )
    {
        out = sse_unpack( mask, prev, buf, table );
    }
    gettimeofday(&end, 0);
    double sse_time =
        (end.tv_sec - start.tv_sec)*1000000 +
        (end.tv_usec - start.tv_usec);
    cout << "\tSSE:\t\t" << sse_time << "u\n";

    // Simple
    gettimeofday(&start, 0);
    for( uint64_t i = 0; i < count; i++ )
    {
        out = simple_unpack( mask, prev, buf, table );
    }
    gettimeofday(&end, 0);
    double simple_time =
        (end.tv_sec - start.tv_sec)*1000000 +
        (end.tv_usec - start.tv_usec);
    cout << "\tSimple:\t\t" << simple_time << "u\n";

    printf("\tSimple/SSE:\t%f\n", simple_time / sse_time);
#endif

    return 0;
}
