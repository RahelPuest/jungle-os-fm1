/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
#include "jo_store.h"

uint32_t jo_crc32(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *b++;
        for (k = 0; k < 8u; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

void jo_rec_seal(jo_rec_hdr_t *h, uint32_t magic, uint16_t version, const void *payload, uint32_t len)
{
    h->magic = magic;
    h->version = version;
    h->len = (uint16_t)len;
    h->crc = jo_crc32(payload, len);
}

int jo_rec_check(const void *buf, int got, uint32_t magic, uint16_t min_version, uint16_t version,
                 const uint8_t **payload, uint32_t *len, uint16_t *found_version)
{
    const jo_rec_hdr_t *h = (const jo_rec_hdr_t *)buf;
    if (got < (int)sizeof *h) return JO_REC_SHORT;
    if (h->magic != magic) return JO_REC_MAGIC;
    if (h->version < min_version || h->version > version) return JO_REC_VERSION;
    if ((uint32_t)got < sizeof *h + h->len) return JO_REC_LEN;
    if (jo_crc32((const uint8_t *)buf + sizeof *h, h->len) != h->crc) return JO_REC_CRC;
    *payload = (const uint8_t *)buf + sizeof *h;
    *len = h->len;
    if (found_version) *found_version = h->version;
    return JO_REC_OK;
}
