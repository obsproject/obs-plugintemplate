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

#include "udp-output.h"
#include <plugin-support.h>
#include <obs-module.h>
#include <string.h>

/* ── Platform-specific socket includes ─────────────────────────────────── */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET udp_socket_t;
#define UDP_INVALID_SOCKET INVALID_SOCKET
#define UDP_SOCKET_ERROR   SOCKET_ERROR
#define udp_close_socket(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
typedef int udp_socket_t;
#define UDP_INVALID_SOCKET (-1)
#define UDP_SOCKET_ERROR   (-1)
#define udp_close_socket(s) close(s)
#endif

/* ── Internal state ─────────────────────────────────────────────────────── */
struct udp_output_ctx {
	obs_output_t *output;
	udp_socket_t sock;
	struct sockaddr_in dest;
	uint32_t seq;
	int max_payload; /* max bytes of encoded data per UDP packet */
	bool active;
};

/* ── Helpers ────────────────────────────────────────────────────────────── */
static uint8_t codec_from_encoder(const char *id)
{
	if (!id)
		return UDP_CODEC_H264;
	if (strstr(id, "h265") || strstr(id, "hevc"))
		return UDP_CODEC_H265;
	if (strstr(id, "av1"))
		return UDP_CODEC_AV1;
	if (strstr(id, "aac"))
		return UDP_CODEC_AAC;
	if (strstr(id, "opus"))
		return UDP_CODEC_OPUS;
	return UDP_CODEC_H264;
}

/* ── OBS output callbacks ───────────────────────────────────────────────── */

static const char *udp_output_get_name(void *unused)
{
	(void)unused;
	return obs_module_text("UDPOutput.Name");
}

static void *udp_output_create(obs_data_t *settings, obs_output_t *output)
{
	struct udp_output_ctx *ctx = bzalloc(sizeof(*ctx));
	ctx->output = output;
	ctx->sock = UDP_INVALID_SOCKET;
	ctx->seq = 0;
	ctx->max_payload = (int)obs_data_get_int(settings, "packet_size");
	if (ctx->max_payload <= 0 || ctx->max_payload > UDP_MAX_PACKET_SIZE)
		ctx->max_payload = UDP_DEFAULT_PACKET_SIZE;
	return ctx;
}

static void udp_output_destroy(void *data)
{
	struct udp_output_ctx *ctx = data;
	if (!ctx)
		return;
	if (ctx->sock != UDP_INVALID_SOCKET)
		udp_close_socket(ctx->sock);
	bfree(ctx);
}

static bool udp_output_start(void *data)
{
	struct udp_output_ctx *ctx = data;

#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		obs_log(LOG_ERROR, "WSAStartup failed");
		return false;
	}
#endif

	obs_data_t *settings = obs_output_get_settings(ctx->output);
	const char *host = obs_data_get_string(settings, "remote_host");
	int port = (int)obs_data_get_int(settings, "remote_port");
	ctx->max_payload = (int)obs_data_get_int(settings, "packet_size");
	obs_data_release(settings);

	if (!host || host[0] == '\0')
		host = UDP_DEFAULT_HOST;
	if (port <= 0 || port > 65535)
		port = UDP_DEFAULT_PORT;
	if (ctx->max_payload <= 0 || ctx->max_payload > UDP_MAX_PACKET_SIZE)
		ctx->max_payload = UDP_DEFAULT_PACKET_SIZE;

	ctx->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (ctx->sock == UDP_INVALID_SOCKET) {
		obs_log(LOG_ERROR, "Failed to create UDP socket");
		return false;
	}

	/* Increase send buffer to handle keyframe bursts */
	int sndbuf = UDP_SEND_BUFFER_SIZE;
	setsockopt(ctx->sock, SOL_SOCKET, SO_SNDBUF, (const char *)&sndbuf,
		   sizeof(sndbuf));

	/* Set DSCP CS4 (0x80) for QoS prioritisation on LAN switches */
	int tos = 0x80;
	setsockopt(ctx->sock, IPPROTO_IP, IP_TOS, (const char *)&tos,
		   sizeof(tos));

	/* Resolve destination */
	memset(&ctx->dest, 0, sizeof(ctx->dest));
	ctx->dest.sin_family = AF_INET;
	ctx->dest.sin_port = htons((uint16_t)port);

	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) {
		obs_log(LOG_ERROR, "Cannot resolve host: %s", host);
		udp_close_socket(ctx->sock);
		ctx->sock = UDP_INVALID_SOCKET;
		return false;
	}
	ctx->dest.sin_addr =
		((struct sockaddr_in *)res->ai_addr)->sin_addr;
	freeaddrinfo(res);

	ctx->active = true;
	obs_log(LOG_INFO, "UDP output started → %s:%d (payload %d B)", host,
		port, ctx->max_payload);

	if (!obs_output_begin_data_capture(ctx->output, 0)) {
		obs_log(LOG_ERROR, "obs_output_begin_data_capture failed");
		ctx->active = false;
		udp_close_socket(ctx->sock);
		ctx->sock = UDP_INVALID_SOCKET;
		return false;
	}
	return true;
}

static void udp_output_stop(void *data, uint64_t ts)
{
	(void)ts;
	struct udp_output_ctx *ctx = data;
	if (!ctx->active)
		return;
	ctx->active = false;
	obs_output_end_data_capture(ctx->output);
	if (ctx->sock != UDP_INVALID_SOCKET) {
		udp_close_socket(ctx->sock);
		ctx->sock = UDP_INVALID_SOCKET;
	}
#ifdef _WIN32
	WSACleanup();
#endif
	obs_log(LOG_INFO, "UDP output stopped");
}

static void udp_output_data(void *data, struct encoder_packet *packet)
{
	struct udp_output_ctx *ctx = data;
	if (!ctx->active || ctx->sock == UDP_INVALID_SOCKET || !packet ||
	    !packet->data || packet->size == 0)
		return;

	const uint8_t *src = packet->data;
	size_t remaining = (size_t)packet->size;
	int max_p = ctx->max_payload;

	/* Determine total fragment count (capped at 255) */
	int frag_total = (int)((remaining + (size_t)max_p - 1) / (size_t)max_p);
	if (frag_total > 255)
		frag_total = 255;

	/* Derive codec from encoder id */
	obs_encoder_t *enc = (packet->type == OBS_ENCODER_VIDEO)
				     ? obs_output_get_video_encoder(ctx->output)
				     : obs_output_get_audio_encoder(ctx->output, 0);
	const char *enc_id = enc ? obs_encoder_get_id(enc) : NULL;
	uint8_t codec = codec_from_encoder(enc_id);

	/* Build send buffer once (header + payload) */
	size_t hdr_size = sizeof(struct udp_frame_header);
	size_t buf_size = hdr_size + (size_t)max_p;
	uint8_t *buf = bmalloc(buf_size);

	struct udp_frame_header *hdr = (struct udp_frame_header *)buf;
	hdr->magic = UDP_OUTPUT_MAGIC;
	hdr->dts = packet->dts / 1000; /* ns → µs */
	hdr->pts = packet->pts / 1000;
	hdr->type = (packet->type == OBS_ENCODER_VIDEO) ? UDP_TYPE_VIDEO
							 : UDP_TYPE_AUDIO;
	hdr->keyframe = packet->keyframe ? 1 : 0;
	hdr->codec = codec;
	hdr->frag_count = (uint8_t)frag_total;
	hdr->frame_size = (uint32_t)packet->size;
	memset(hdr->reserved, 0, sizeof(hdr->reserved));

	int frag_idx = 0;
	while (remaining > 0 && frag_idx < 255) {
		size_t chunk =
			(remaining > (size_t)max_p) ? (size_t)max_p : remaining;

		hdr->seq = ctx->seq++;
		hdr->frag_idx = (uint8_t)frag_idx;
		hdr->payload_len = (uint32_t)chunk;

		memcpy(buf + hdr_size, src, chunk);

		sendto(ctx->sock, (const char *)buf,
		       (int)(hdr_size + chunk), 0,
		       (struct sockaddr *)&ctx->dest,
		       sizeof(ctx->dest));

		src += chunk;
		remaining -= chunk;
		frag_idx++;
	}

	bfree(buf);
}

/* ── Properties (settings UI) ──────────────────────────────────────────── */
static obs_properties_t *udp_output_properties(void *unused)
{
	(void)unused;
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(props, "remote_host",
				obs_module_text("UDPOutput.RemoteHost"),
				OBS_TEXT_DEFAULT);

	obs_property_t *port =
		obs_properties_add_int(props, "remote_port",
				       obs_module_text("UDPOutput.RemotePort"),
				       1, 65535, 1);
	(void)port;

	obs_property_t *psize = obs_properties_add_int(
		props, "packet_size",
		obs_module_text("UDPOutput.PacketSize"), 512,
		UDP_MAX_PACKET_SIZE, 64);
	obs_property_int_set_suffix(psize, " B");

	return props;
}

static void udp_output_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "remote_host",
				    UDP_DEFAULT_HOST);
	obs_data_set_default_int(settings, "remote_port", UDP_DEFAULT_PORT);
	obs_data_set_default_int(settings, "packet_size",
				 UDP_DEFAULT_PACKET_SIZE);
}

/* ── Output info registration ───────────────────────────────────────────── */
static struct obs_output_info udp_output_info = {
	.id = "udp_output",
	.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED,
	.get_name = udp_output_get_name,
	.create = udp_output_create,
	.destroy = udp_output_destroy,
	.start = udp_output_start,
	.stop = udp_output_stop,
	.encoded_packet = udp_output_data,
	.get_properties = udp_output_properties,
	.get_defaults = udp_output_defaults,
};

void register_udp_output(void)
{
	obs_register_output(&udp_output_info);
	obs_log(LOG_INFO, "UDP output type registered");
}
