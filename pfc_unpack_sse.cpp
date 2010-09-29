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

typedef const char* (*FUnpack) (
    const char* prev,
    char* cur,
    size_t sizeOf,
    const char *buf
);

class PfcSSETable
{
public:
    ui64 Table[256];
    PfcSSETable()
    {
        for ( ui16 mask = 0; mask < 256; mask++ )
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
                 *  |       Count of bytes to copy ( only first byte )
                 *  ^^^^^^^^^
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
            Table[ mask ] = shuffle | ( count << 3 );
        }
    }
};

static PfcSSETable pfc_sse_table;
const char* sse_unpack(
    const char* prev,
    char* cur,
    size_t sizeOf,
    const char* buf
) {
    const char *cur_end = cur + sizeOf;
    ui8 hdrMark = 0, maskHdr = 0;
    for (; cur < cur_end; cur += 8, prev += 8, hdrMark <<= 1) {
        if (!hdrMark) {
            maskHdr = *buf++;
            hdrMark = 1;
        }

        ui64 item = (cur + 8 <= cur_end)? *(ui64*)prev : *(ui32*)prev;
        if (maskHdr & hdrMark) {
            ui8 mask = *buf++;

            ui64 shuffle = pfc_sse_table.Table[ mask ];
            ui64 count = (( shuffle >> 3 ) & 0xf);
            asm (
                // if ( ( buf & 0xfff ) >= 0xff7 ) )
                "movq       %1, %%rax\n"
                "and        $0xfff, %%rax\n"
                "cmp        $0xff6, %%rax\n"
                "jbe        1f\n"
                // slow ( end of page )
                    // %rax = *(ui64*) (buf + count - 8)
                    "movq       -0x8(%1,%4,1), %%rax\n"
                    // %rax >>= (8 - count) * 8
                    "mov        $0x8, %%rcx\n"
                    "sub        %4, %%rcx\n"
                    "shl        $0x3, %%rcx\n"
                    "shr        %%cl, %%rax\n"
                    // %rax -> %xmm1
                    "movq       %%rax, %%xmm1\n"
                    "jmp        2f\n"
                // fast
                "1:\n"
                    "movq       (%1), %%xmm1\n"         // data -> xmm1
                // out
                "2:\n"

                // shuffle
                "andq       $0xffffffffffffff87, %2\n"  // cleanup mask
                "movq       %2, %%xmm0\n"               // smask    -> xmm0
                "pshufb     %%xmm0, %%xmm1\n"           // shuffle  -> xmm1

                // blend
                "movq       %3, %%xmm2\n"               // prev  -> xmm2
                "pblendvb   %%xmm2, %%xmm1\n"           // blned -> xmm2

                // return value
                "movq       %%xmm1, %0\n"               // xmm2 -> item

                : "=r"(item)
                : "r"(buf), "r"(shuffle), "r"(item), "r"(count)
                : "xmm0", "xmm1", "xmm2", "rcx", "rax"
            );

            buf += count;
        }

        if (cur + 8 <= cur_end)
            *(ui64*)cur = item;
        else
            *(ui32*)cur = item;
    }
    return buf;
}

const char *simple_unpack (
    const char *prev,
    char *cur,
    size_t sizeOf,
    const char *buf
) {
    const char *cur_end = cur + sizeOf;
    ui8 hdrMark = 0, maskHdr = 0;
    for (; cur < cur_end; cur += 8, prev += 8, hdrMark <<= 1) {
        if (!hdrMark) {
            maskHdr = *buf++;
            hdrMark = 1;
        }

        ui64 item = (cur + 8 <= cur_end)? *(ui64*)prev : *(ui32*)prev;
        if (maskHdr & hdrMark) {
            ui8 mask = *buf++;
            if (mask == 255) { // big-endian: fixme
                *(ui64*)cur = *(ui64*)buf; // use memcpy?
                buf += 8;
                continue;
            }
            ui64 item_flag = 0, item_mask = ~(ui64)0;
            ui32 shift = 0;
            while(mask) { // might use sse here
                item_flag |= ui64(mask & 1? (ui8)*buf++ : 0) << shift;
                item_mask ^= ui64(mask & 1? 255 : 0) << shift;
                mask >>= 1;
                shift += 8;
            }
            item = ( item & item_mask ) | item_flag;
        }
        if (cur + 8 <= cur_end)
            *(ui64*)cur = item;
        else
            *(ui32*)cur = item;
    }
    return buf;
}


#define BENCH_LCOUNT 10000UL
ui64 benchmark_run(
        FUnpack unpack,
        const char* from,
        char* to,
        size_t to_size,   // to_size % chunk_size == 0 !improtant
        size_t chunk_size // chunk_size & 3 == 0 !improtant
) {
    //{{{ start
    struct timeval start_t, end_t;
    gettimeofday(&start_t, 0);
    //}}}
    for( ui64 i = 0; i < BENCH_LCOUNT; i++ )
    {
        ui64 count = 0;
        const char* f_test = from;
        while ( (count + 2) * chunk_size < to_size )
        {
            f_test = unpack(
                to + count * chunk_size,        // prev
                to + (count + 1) * chunk_size,  // cur
                chunk_size,                     // size
                f_test                          // buf
            );
            count += 1;
        }
    }
    //{{{ end
    gettimeofday(&end_t, 0);
    return (end_t.tv_sec - start_t.tv_sec)*1000000L +
           (end_t.tv_usec - start_t.tv_usec);
    //}}}
}
int main()
{
    // Values
    const size_t size = 16384;

    // Generate source data
    ui32 in_data[size], out_sse[size], out_simple[size];
    for ( ui64 i = 0; i < size; i++ )
        in_data[i] = random();

    // Init destenation buffers
    memset(out_simple, 0, sizeof(out_simple));
    memset(out_sse, 0, sizeof(out_sse));

    // Benchmark
    printf(":: Benchmark ( Runs: %ld )\n", BENCH_LCOUNT);

    ui64 simple_time = benchmark_run(
                           simple_unpack,
                           (const char*) in_data,
                           (char*) out_simple,
                           size * sizeof(ui32),
                           sizeof(ui32) * 13
                       );
    printf("\tSimple:\t\t%ldu\n", simple_time);

    ui64 sse_time    = benchmark_run(
                           sse_unpack,
                           (const char*) in_data,
                           (char*) out_sse,
                           size * sizeof(ui32),
                           sizeof(ui32) * 13
                       );
    printf("\tSSE:\t\t%ldu\n", sse_time);

    // Ratio
    printf("\tSimple/SSE:\t%f\n",
            (double) (simple_time * 10000 / sse_time) / 10000);

    //{{{ Verify
    bool good = true;
    ui64 i = 0;
    for(; i < size; i++ )
    {
        if ( out_simple[i] != out_sse[i] )
        {
            good = false;
            break;
        }
    }
    printf(":: Verification: ");
    if ( !good )
        printf("FAILED (on %ld)\n", i);
    else
        printf("OK\n");
    //}}}
    return 0;
}
