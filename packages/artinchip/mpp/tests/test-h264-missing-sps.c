/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h264_decoder.h"
#include "h264_nal.h"

void *_mpp_alloc_(size_t size, const char *file, int line)
{
    (void)file;
    (void)line;
    return malloc(size);
}

int main(void)
{
    struct h264_dec_ctx ctx;
    /* PPS id=0, SPS id=0, entropy_coding=0 and valid bounded syntax.
     * No SPS was supplied, as happens after resetting between SPS and PPS. */
    static const unsigned char pps[] = {0xce, 0x38, 0x80, 0, 0, 0, 0, 0};
    memset(&ctx, 0, sizeof(ctx));
    assert(init_read_bits(&ctx.gb, pps, 24, 1) == 0);
    assert(h264_decode_pps(&ctx) == -1);
    assert(ctx.pps_buffers[0] != NULL);
    free(ctx.pps_buffers[0]);
    puts("actual H.264 PPS parser rejects missing SPS");
    return 0;
}
