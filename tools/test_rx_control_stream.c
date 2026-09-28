/* Host-side regression test for RX UART control frames split across blocks. */
#include <assert.h>
#include <stdint.h>
#include "control_stream_parser.h"

static unsigned feed(ReceiverControlStreamParser_t *parser,
                     const uint8_t *bytes, unsigned length)
{
    unsigned i;
    unsigned complete = 0u;
    for (i = 0u; i < length; ++i)
    {
        ReceiverControlStreamResult_t result =
            ReceiverControlStream_Feed(parser, bytes[i]);
        assert(result != RECEIVER_CONTROL_STREAM_INVALID_LENGTH);
        if (result == RECEIVER_CONTROL_STREAM_FRAME)
        {
            assert(parser->expected == 9u);
            assert(parser->frame[4] == LEGACY_CMD_ANTENNA_TEST_REQUEST);
            assert(parser->frame[7] == 1u);
            assert(parser->frame[8] == 1u);
            ++complete;
            ReceiverControlStream_Reset(parser);
        }
    }
    return complete;
}

int main(void)
{
    static const uint8_t select_ant1[9] =
        {0x5Au, 0x41u, 0x59u, 0x53u, 0x2Cu, 0u, 1u, 1u, 1u};
    static const uint8_t noisy_prefix[3] = {0x00u, 0x5Au, 0x5Au};
    static const uint8_t oversized_header[7] =
        {0x5Au, 0x41u, 0x59u, 0x53u, 0x2Cu, 0u, 25u};
    ReceiverControlStreamParser_t parser = {{0}, 0u, 0u};
    unsigned split;

    for (split = 1u; split < sizeof(select_ant1); ++split)
    {
        ReceiverControlStream_Reset(&parser);
        assert(feed(&parser, select_ant1, split) == 0u);
        assert(feed(&parser, select_ant1 + split,
                    (unsigned)sizeof(select_ant1) - split) == 1u);
    }

    assert(feed(&parser, noisy_prefix, sizeof(noisy_prefix)) == 0u);
    assert(feed(&parser, select_ant1 + 1u,
                (unsigned)sizeof(select_ant1) - 1u) == 1u);
    assert(feed(&parser, select_ant1, sizeof(select_ant1)) == 1u);
    for (split = 0u; split < sizeof(oversized_header) - 1u; ++split)
    {
        assert(ReceiverControlStream_Feed(&parser, oversized_header[split]) ==
               RECEIVER_CONTROL_STREAM_MORE);
    }
    assert(ReceiverControlStream_Feed(&parser, oversized_header[6]) ==
           RECEIVER_CONTROL_STREAM_INVALID_LENGTH);
    assert(feed(&parser, select_ant1, sizeof(select_ant1)) == 1u);
    return 0;
}
