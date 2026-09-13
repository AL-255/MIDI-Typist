#include "updater_protocol.h"

#include <string.h>

static const uint8_t s_serial[22] = "OPENHUNTSMAN0001";

uint8_t updater_frame_checksum(const uint8_t frame[UPDATER_FRAME_SIZE])
{
    uint8_t checksum = 0u;
    for (size_t i = 2u; i < UPDATER_CHECKSUM_OFFSET; ++i)
    {
        checksum ^= frame[i];
    }
    return checksum;
}

bool updater_frame_valid(const uint8_t *frame, size_t length)
{
    if ((frame == NULL) || (length != UPDATER_FRAME_SIZE) ||
        (frame[UPDATER_PAYLOAD_COUNT_OFFSET] > UPDATER_PAYLOAD_SIZE))
    {
        return false;
    }

    /* The factory DLL emits SET_MODE with checksum zero; preserve that quirk. */
    const bool factory_set_mode = (frame[UPDATER_CHANNEL_OFFSET] == 0u) &&
                                 (frame[UPDATER_OPCODE_OFFSET] == 0x04u) &&
                                 (frame[UPDATER_CHECKSUM_OFFSET] == 0u);
    return factory_set_mode || (frame[UPDATER_CHECKSUM_OFFSET] == updater_frame_checksum(frame));
}

static void response_begin(const uint8_t *request, uint8_t *response)
{
    memset(response, 0, UPDATER_FRAME_SIZE);
    memcpy(&response[2], &request[2], 6u);
    response[UPDATER_STATUS_OFFSET] = UPDATER_STATUS_SUCCESS;
    response[UPDATER_EXTENSION_OFFSET] = request[UPDATER_EXTENSION_OFFSET];
}

updater_action_t updater_protocol_handle(const uint8_t *request, size_t request_length,
                                         uint8_t response[UPDATER_FRAME_SIZE])
{
    if ((response == NULL) || !updater_frame_valid(request, request_length))
    {
        return kUpdaterNoAction;
    }

    response_begin(request, response);
    if (request[UPDATER_CHANNEL_OFFSET] != 0u)
    {
        response[UPDATER_STATUS_OFFSET] = UPDATER_STATUS_FAILURE;
        response[UPDATER_CHECKSUM_OFFSET] = updater_frame_checksum(response);
        return kUpdaterResponseReady;
    }

    uint8_t count = 0u;
    switch (request[UPDATER_OPCODE_OFFSET])
    {
        case 0x04u: /* SET_MODE */
            if ((request[UPDATER_PAYLOAD_COUNT_OFFSET] >= 1u) &&
                (request[UPDATER_PAYLOAD_OFFSET] == 1u))
            {
                response[UPDATER_CHECKSUM_OFFSET] = updater_frame_checksum(response);
                return kUpdaterEnterBootloader;
            }
            response[UPDATER_STATUS_OFFSET] = UPDATER_STATUS_FAILURE;
            break;
        case 0x81u: /* QUERY_VERSION */
            response[UPDATER_PAYLOAD_OFFSET + 0u] = 2u;
            response[UPDATER_PAYLOAD_OFFSET + 1u] = 1u;
            count = 2u;
            break;
        case 0x82u: /* QUERY_IDENTIFIER */
            memcpy(&response[UPDATER_PAYLOAD_OFFSET], s_serial, sizeof(s_serial));
            count = (uint8_t)sizeof(s_serial);
            break;
        case 0x83u: /* QUERY_CAPABILITY */
            response[UPDATER_PAYLOAD_OFFSET + 0u] = 1u;
            response[UPDATER_PAYLOAD_OFFSET + 1u] = 0x34u;
            count = 2u;
            break;
        case 0x84u: /* QUERY_MODE */
            response[UPDATER_PAYLOAD_OFFSET] = 0u;
            count = 1u;
            break;
        case 0x87u: /* QUERY_EXTENDED_VERSION */
            response[UPDATER_PAYLOAD_OFFSET + 0u] = 2u;
            response[UPDATER_PAYLOAD_OFFSET + 1u] = 1u;
            response[UPDATER_PAYLOAD_OFFSET + 2u] = 0u;
            response[UPDATER_PAYLOAD_OFFSET + 3u] = 0u;
            count = 4u;
            break;
        case 0x9fu: /* QUERY_BUILD */
            response[UPDATER_PAYLOAD_OFFSET + 0u] = 0u;
            response[UPDATER_PAYLOAD_OFFSET + 1u] = 0u;
            response[UPDATER_PAYLOAD_OFFSET + 2u] = 1u;
            response[UPDATER_PAYLOAD_OFFSET + 3u] = 0u;
            count = 4u;
            break;
        case 0x86u: /* QUERY_PAIR */
            response[UPDATER_PAYLOAD_OFFSET + 0u] = 0u;
            response[UPDATER_PAYLOAD_OFFSET + 1u] = 0u;
            count = 2u;
            break;
        case 0xc0u: /* QUERY_TIMER */
            response[UPDATER_PAYLOAD_OFFSET + 0u] = request[UPDATER_PAYLOAD_OFFSET];
            response[UPDATER_PAYLOAD_OFFSET + 1u] = 0u;
            count = 2u;
            break;
        default:
            response[UPDATER_STATUS_OFFSET] = UPDATER_STATUS_FAILURE;
            break;
    }

    response[UPDATER_PAYLOAD_COUNT_OFFSET] = count;
    response[UPDATER_CHECKSUM_OFFSET] = updater_frame_checksum(response);
    return kUpdaterResponseReady;
}
