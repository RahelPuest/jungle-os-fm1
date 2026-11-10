/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* Versioned, checksummed records for presets, patterns and scenes. fm1-base's store already keeps each object as
 * a torn-write-safe A/B sector pair with its own CRC; this header adds what the app needs on top: a magic per
 * record kind, a format version with a migration hook, the payload length, and a CRC of the payload, so a record
 * from another firmware, an older format or a corrupted copy is recognised and replaced by defaults. */
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t magic;    /* the record kind, 4 ASCII bytes */
    uint16_t version;  /* the payload's format */
    uint16_t len;      /* payload bytes */
    uint32_t crc;      /* CRC-32 of the payload */
} jo_rec_hdr_t;

enum { JO_REC_OK = 0, JO_REC_SHORT = -1, JO_REC_MAGIC = -2, JO_REC_VERSION = -3, JO_REC_LEN = -4, JO_REC_CRC = -5 };

uint32_t jo_crc32(const void *p, uint32_t n);
/* fills the header for payload (len bytes); the record is hdr followed by the payload */
void jo_rec_seal(jo_rec_hdr_t *h, uint32_t magic, uint16_t version, const void *payload, uint32_t len);
/* checks a record as loaded (got bytes at buf): JO_REC_OK, or why not. min_version..version are accepted
 * (the caller migrates older payloads); *payload, *len point into buf */
int jo_rec_check(const void *buf, int got, uint32_t magic, uint16_t min_version, uint16_t version,
                 const uint8_t **payload, uint32_t *len, uint16_t *found_version);
