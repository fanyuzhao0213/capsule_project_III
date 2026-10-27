#ifndef RECEIVER_CONTROL_STREAM_PARSER_H
#define RECEIVER_CONTROL_STREAM_PARSER_H

#include <stdint.h>
#include "legacy_protocol.h"

typedef struct
{
    uint8_t frame[LEGACY_CONTROL_FRAME_MAX_SIZE];
    uint16_t received;
    uint16_t expected;
} ReceiverControlStreamParser_t;

typedef enum
{
    RECEIVER_CONTROL_STREAM_MORE = 0,
    RECEIVER_CONTROL_STREAM_FRAME,
    RECEIVER_CONTROL_STREAM_INVALID_LENGTH
} ReceiverControlStreamResult_t;

static void ReceiverControlStream_Reset(ReceiverControlStreamParser_t *parser)
{
    parser->received = 0u;
    parser->expected = 0u;
}

/* UART静默数据块不是协议帧边界；逐字节保留跨块的ZAYS解析状态。 */
static ReceiverControlStreamResult_t ReceiverControlStream_Feed(
    ReceiverControlStreamParser_t *parser, uint8_t byte)
{
    static const uint8_t header[4] = {0x5Au, 0x41u, 0x59u, 0x53u};
    uint16_t payload_length;

    if (parser->received < sizeof(header))
    {
        if (byte == header[parser->received])
        {
            parser->frame[parser->received++] = byte;
        }
        else if (byte == header[0])
        {
            parser->frame[0] = byte;
            parser->received = 1u;
        }
        else
        {
            ReceiverControlStream_Reset(parser);
        }
        return RECEIVER_CONTROL_STREAM_MORE;
    }

    if (parser->received >= sizeof(parser->frame))
    {
        ReceiverControlStream_Reset(parser);
        return RECEIVER_CONTROL_STREAM_INVALID_LENGTH;
    }
    parser->frame[parser->received++] = byte;
    if (parser->received == 7u)
    {
        payload_length = (uint16_t)(((uint16_t)parser->frame[5] << 8) |
                                    parser->frame[6]);
        if (payload_length > (sizeof(parser->frame) - 8u))
        {
            ReceiverControlStream_Reset(parser);
            return RECEIVER_CONTROL_STREAM_INVALID_LENGTH;
        }
        parser->expected = (uint16_t)(payload_length + 8u);
    }
    return ((parser->expected != 0u) &&
            (parser->received == parser->expected)) ?
        RECEIVER_CONTROL_STREAM_FRAME : RECEIVER_CONTROL_STREAM_MORE;
}

#endif
