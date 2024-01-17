/*
 * obs-gstreamer. OBS Studio plugin.
 * Copyright (C) 2018-2021 Florian Zwoch <fzwoch@gmail.com>
 *
 * This file is part of obs-gstreamer.
 *
 * obs-gstreamer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * obs-gstreamer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with obs-gstreamer. If not, see <http://www.gnu.org/licenses/>.
 */

#include <obs-module.h>
#include <plugin-support.h>
#include <gst/gst.h>

OBS_DECLARE_MODULE()

// gstreamer-output.c
extern const char *gstreamer_output_get_name(void *type_data);
extern void *gstreamer_output_create(obs_data_t *settings, obs_output_t *output);
extern void gstreamer_output_destroy(void *data);
extern bool gstreamer_output_start(void *data);
extern void gstreamer_output_stop(void *data, uint64_t ts);
extern void gstreamer_output_encoded_packet(void *data, struct encoder_packet *packet);
extern void gstreamer_output_raw_video(void *data, struct video_data *frame);
extern void gstreamer_output_raw_audio(void *data, struct audio_data *frame);
extern void gstreamer_output_get_defaults(obs_data_t *settings);
extern obs_properties_t *gstreamer_output_get_properties(void *data);

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

	guint major, minor, micro, nano;
	gst_version(&major, &minor, &micro, &nano);
	obs_log(LOG_INFO, "gst-runtime: %u.%u.%u", major, minor, micro);

	struct obs_output_info output_info = {
		.id = "hjm-gstreamer-output",
		//.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED,
		.flags = OBS_OUTPUT_VIDEO,
		.get_name = gstreamer_output_get_name,
		.create = gstreamer_output_create,
		.destroy = gstreamer_output_destroy,
		.start = gstreamer_output_start,
		.stop = gstreamer_output_stop,

		.encoded_packet = gstreamer_output_encoded_packet,
		.raw_video = gstreamer_output_raw_video,
		.raw_audio = gstreamer_output_raw_audio,

		.get_defaults = gstreamer_output_get_defaults,
		.get_properties = gstreamer_output_get_properties,
	};

	obs_register_output(&output_info);

	gst_init(NULL, NULL);

	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
