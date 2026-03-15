/*
OBS UDP Output Plugin
Copyright (C) 2024 werner1447

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <obs-module.h>
#include <stdint.h>

/* Magic identifier for packet validation: "OBSU" */
#define UDP_OUTPUT_MAGIC 0x5553424F

/* Codec type identifiers */
#define UDP_CODEC_H264 0
#define UDP_CODEC_H265 1
#define UDP_CODEC_AV1  2
#define UDP_CODEC_AAC  3
#define UDP_CODEC_OPUS 4

/* Media type identifiers */
#define UDP_TYPE_VIDEO 0
#define UDP_TYPE_AUDIO 1

/* Default settings */
#define UDP_DEFAULT_HOST        "192.168.1.100"
#define UDP_DEFAULT_PORT        7777
#define UDP_DEFAULT_PACKET_SIZE 1400
#define UDP_MAX_PACKET_SIZE     65000
#define UDP_SEND_BUFFER_SIZE    (1024 * 1024) /* 1 MB */

/*
 * UDP frame header – 40 bytes, packed.
 * Sent before each payload fragment.
 */
#pragma pack(push, 1)
struct udp_frame_header {
	uint32_t magic;       /* UDP_OUTPUT_MAGIC                        */
	uint32_t seq;         /* global packet sequence counter          */
	int64_t dts;          /* decode timestamp in microseconds        */
	int64_t pts;          /* presentation timestamp in microseconds  */
	uint8_t type;         /* UDP_TYPE_VIDEO / UDP_TYPE_AUDIO         */
	uint8_t keyframe;     /* 1 = keyframe / sync frame               */
	uint8_t codec;        /* UDP_CODEC_* value                       */
	uint8_t frag_idx;     /* 0-based fragment index                  */
	uint8_t frag_count;   /* total number of fragments for this frame*/
	uint8_t reserved[3];  /* padding – must be zero                  */
	uint32_t frame_size;  /* total frame size before fragmentation   */
	uint32_t payload_len; /* bytes of payload in this packet         */
};
#pragma pack(pop)

/* Registers the udp_output type with OBS */
void register_udp_output(void);
