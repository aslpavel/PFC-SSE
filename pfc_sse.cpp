// stdlib
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

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
FORCE_INLINE ui64 sse_unpack( ui64 prev, ui8* &buf, const ui64* table )
{
    ui8 mask = *buf++;
    ui64 shuffle = table[ mask ], item;
    ui64  count = ( shuffle >> 3 ) & 0xf;

    // pshufb   ~ _mm_shuffle_pi8
    // pblendvb ~ _mm_blendv_epi8
    asm (

#if SLOW_FETCH != 1
        // if ( ( buf & 0xfff ) >= 0xff7 ) )
        "movq       %1, %%rax\n"
        "and        $0xfff, %%rax\n"
        "cmp        $0xff6, %%rax\n"
        "jbe        1f\n"
        // slow
#endif
            // %rax = *(ui64*) (buf + count - 8)
            "movq       -0x8(%1,%4,1), %%rax\n"
            // %rax >>= (8 - count) * 8
            "mov        $0x8, %%rcx\n"
            "sub        %4, %%rcx\n"
            "shl        $0x3, %%rcx\n"
            "shr        %%cl, %%rax\n"
            // %rax -> %xmm1
            "movq       %%rax, %%xmm1\n"
#if SLOW_FETCH != 1
            "jmp        2f\n"
        "1:\n"
        // fast
            "movq       (%1), %%xmm1\n"         // data -> xmm1
        "2:\n"
        // out
#endif
        // shuffle
        "and       $0xffffffffffffff87, %2\n"   // remove count form mask
        "movq       %2, %%xmm0\n"               // smask    -> xmm0
        "pshufb     %%xmm0, %%xmm1\n"           // shuffle  -> xmm1

        // blend
        "movq       %3, %%xmm2\n"               // prev  -> xmm2
        "pblendvb   %%xmm2, %%xmm1\n"           // blned -> xmm1

        // return value
        "movq       %%xmm1, %0\n"               // xmm1 -> item

        : "=r"(item)
        : "r"(buf), "r"(shuffle), "r"(prev), "r"(count)
        : "xmm0", "xmm1", "xmm2", "rcx", "rax"
    );

    buf += count;
    return item;
}

// Plain C version
FORCE_INLINE ui64 simple_unpack( ui64 prev, ui8* &buf, const ui64* table )
{
    // mine
    ui64 item = prev;
    ui8 mask = *buf++;

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
    for ( ui32 mask = 0; mask < 256; mask++ )
    {
        ui64 shuffle = 0, count = 0;
        for ( int b = 0; b < 8; b++ )
        {
            /*
             *  Mask Format:
             *   0 1 2 3 4 5 6 7
             *  |X.X.X.O.O.O.O.X|
             *  |     |       |
             *  |     |       ^^^
             *  |     |       Shuffle|Blend bit
             *  |     |
             *  |     ^^^^^^^^^
             *  |     Count of bytes to copy ( only first byte )
             *  ^^^^^^^
             *  Shuffle shift bits
             */

            // copy needed
            if ( (mask >> b) & 0x1 )
            {
                // set suffle shift
                shuffle |= count << 8*b;
                // update count shift
                count += 1;
            } else {
                // set byte to zero | blend mask
                shuffle |= 0x80L << 8*b ;
            }
        }
        table[ mask ] = shuffle | ( count << 3 );
    }

    // test values
    ui8 mask = 234;            // 0b 1 1  1 0  1 0  1 0
    ui64 prev = ~(ui64) 0;              // 0x FFFF FFFF FFFF FFFF
    ui64 rbuf = 0xfff112233445500;     // -> 55 44 33 22 11 FF FF FF
    rbuf |= mask;
    ui8* buf = (ui8*) &rbuf;
    // () => 0x 11 22 33 ff 44 ff 55 ff

    cout << ":: Test\n";
    buf = (ui8*) &rbuf;
    printf("\tSSE:\t%lX => %lX\n",
           prev, sse_unpack( prev, buf, table ));
    buf = (ui8*) &rbuf;
    printf("\tSimple:\t%lX => %lX\n",
           prev, simple_unpack( prev, buf, table ));

#if BENCHMARK
    // random data
    size_t size = 16384;
    ui32 in_test[size], simple_out[size], sse_out[size];
    for ( int i = 0; i < size; i++ )
        in_test[i] = random();
    memset(simple_out, 0, sizeof(simple_out));
    memset(sse_out, 0, sizeof(sse_out));

    // Benchmark
    uint64_t count = 1000;
    ui64 out = 0;
    printf("\n:: Benchmark ( Runs: %ld )\n", count);
    struct timeval start, end;

    // SSE
    gettimeofday(&start, 0);
    for( uint64_t j = 0; j < count; j++ ) {
        prev = 0;
        buf = (ui8*) in_test;
        for( uint64_t i = 0; i < sizeof(in_test)/sizeof(out) - 1; i++ )
        {
            out = sse_unpack( prev, buf, table );
            ((ui64*)sse_out)[i] = out;
            prev = out;
        }
    }
    gettimeofday(&end, 0);
    ui64 sse_time =
        (end.tv_sec - start.tv_sec)*1000000L +
        (end.tv_usec - start.tv_usec);
    printf("\tSEE:\t\t%ldu\n", sse_time);

    // Simple
    gettimeofday(&start, 0);
    for( uint64_t j = 0; j < count; j++ ) {
        prev = 0;
        buf = (ui8*) in_test;
        for( uint64_t i = 0; i < sizeof(in_test)/sizeof(out) - 1; i++ )
        {
            out = simple_unpack( prev, buf, table );
            ((ui64*)simple_out)[i] = out;
            prev = out;
        }
    }
    gettimeofday(&end, 0);
    ui64 simple_time =
        (end.tv_sec - start.tv_sec)*1000000L +
        (end.tv_usec - start.tv_usec);
    printf("\tSimple:\t\t%ldu\n", simple_time);

    // Result
    printf("\tSimple/SSE:\t%f\n",
            (double) (simple_time * 10000 / sse_time) / 10000);

    // Verification
    bool good = true;
    int i = 0;
    for(; i < size; i++ )
    {
        if ( simple_out[i] != sse_out[i] )
        {
            good = false;
            break;
        }
    }
    printf(":: Verification: ");
    if ( !good )
        printf("FAILED (on %d)\n", i);
    else
        printf("OK\n");

#endif

    return 0;
}
