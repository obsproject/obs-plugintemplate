/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

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

#include <obs-module.h>
#include <plugin-support.h>
#include <gst/gst.h>

OBS_DECLARE_MODULE()

// gstreamer-source.c
extern const char *gstreamer_source_get_name(void *type_data);
extern void *gstreamer_source_create(obs_data_t *settings,
				     obs_source_t *source);
extern void gstreamer_source_destroy(void *data);
extern void gstreamer_source_get_defaults(obs_data_t *settings);
extern obs_properties_t *gstreamer_source_get_properties(void *data);
extern void gstreamer_source_update(void *data, obs_data_t *settings);
extern void gstreamer_source_show(void *data);
extern void gstreamer_source_hide(void *data);
extern enum obs_media_state gstreamer_source_get_state(void *data);
extern int64_t gstreamer_source_get_time(void *data);
extern int64_t gstreamer_source_get_duration(void *data);
extern void gstreamer_source_play_pause(void *data, bool pause);
extern void gstreamer_source_stop(void *data);
extern void gstreamer_source_restart(void *data);
extern void gstreamer_source_set_time(void *data, int64_t ms);

// gstreamer-encoder.c
extern const char *gstreamer_encoder_get_name_h264(void *type_data);
extern const char *gstreamer_encoder_get_name_h265(void *type_data);
extern void *gstreamer_encoder_create_h264(obs_data_t *settings,
					   obs_encoder_t *encoder);
extern void *gstreamer_encoder_create_h265(obs_data_t *settings,
					   obs_encoder_t *encoder);
extern void gstreamer_encoder_destroy(void *data);
extern bool gstreamer_encoder_encode(void *data, struct encoder_frame *frame,
				     struct encoder_packet *packet,
				     bool *received_packet);
extern void gstreamer_encoder_get_defaults_h264(obs_data_t *settings);
extern void gstreamer_encoder_get_defaults_h265(obs_data_t *settings);
extern obs_properties_t *gstreamer_encoder_get_properties_h264(void *data);
extern obs_properties_t *gstreamer_encoder_get_properties_h265(void *data);
extern bool gstreamer_encoder_get_extra_data(void *data, uint8_t **extra_data,
					     size_t *size);

// gstreamer-filter.c
extern const char *gstreamer_filter_get_name_video(void *type_data);
extern const char *gstreamer_filter_get_name_audio(void *type_data);
extern void *gstreamer_filter_create(obs_data_t *settings,
				     obs_source_t *source);
extern void gstreamer_filter_destroy(void *data);
extern void gstreamer_filter_get_defaults_video(obs_data_t *settings);
extern void gstreamer_filter_get_defaults_audio(obs_data_t *settings);
extern obs_properties_t *gstreamer_filter_get_properties(void *data);
extern void gstreamer_filter_update(void *data, obs_data_t *settings);
extern struct obs_source_frame *
gstreamer_filter_filter_video(void *data, struct obs_source_frame *frame);
//struct obs_audio_data *
//gstreamer_filter_filter_audio(void *p, struct obs_audio_data *audio_data);

// gstreamer-output.c
extern const char *gstreamer_output_get_name(void *type_data);
extern void *gstreamer_output_create(obs_data_t *settings,
				     obs_output_t *output);
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

//	guint major, minor, micro, nano;

//	gst_version(&major, &minor, &micro, &nano);

//	blog(LOG_INFO, "[obs-gstreamer] build: %s, gst-runtime: %u.%u.%u", obs_gstreamer_version, major, minor, micro);

	struct obs_source_info source_info = {
		.id = "gstreamer-source",
		.type = OBS_SOURCE_TYPE_INPUT,
		.icon_type = OBS_ICON_TYPE_MEDIA,
		.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO |
				OBS_SOURCE_DO_NOT_DUPLICATE |
				OBS_SOURCE_CONTROLLABLE_MEDIA,
		.get_name = gstreamer_source_get_name,
		.create = gstreamer_source_create,
		.destroy = gstreamer_source_destroy,

		.get_defaults = gstreamer_source_get_defaults,
		.get_properties = gstreamer_source_get_properties,
		.update = gstreamer_source_update,
		.show = gstreamer_source_show,
		.hide = gstreamer_source_hide,

		.media_get_state = gstreamer_source_get_state,
		.media_get_time = gstreamer_source_get_time,
		.media_get_duration = gstreamer_source_get_duration,

		.media_play_pause = gstreamer_source_play_pause,
		.media_stop = gstreamer_source_stop,
		.media_restart = gstreamer_source_restart,
		.media_set_time = gstreamer_source_set_time,
	};

	obs_register_source(&source_info);

	struct obs_encoder_info encoder_info_h264 = {
		.id = "gstreamer-encoder-h264",
		.type = OBS_ENCODER_VIDEO,
		.codec = "h264",

		.get_name = gstreamer_encoder_get_name_h264,
		.create = gstreamer_encoder_create_h264,
		.destroy = gstreamer_encoder_destroy,

		.encode = gstreamer_encoder_encode,

		.get_defaults = gstreamer_encoder_get_defaults_h264,
		.get_properties = gstreamer_encoder_get_properties_h264,

		.get_extra_data = gstreamer_encoder_get_extra_data,
	};

	obs_register_encoder(&encoder_info_h264);

	struct obs_encoder_info encoder_info_h265 = {
		.id = "gstreamer-encoder-h265",
		.type = OBS_ENCODER_VIDEO,
		.codec = "hevc",

		.get_name = gstreamer_encoder_get_name_h265,
		.create = gstreamer_encoder_create_h265,
		.destroy = gstreamer_encoder_destroy,

		.encode = gstreamer_encoder_encode,

		.get_defaults = gstreamer_encoder_get_defaults_h265,
		.get_properties = gstreamer_encoder_get_properties_h265,

		.get_extra_data = gstreamer_encoder_get_extra_data,
	};

	obs_register_encoder(&encoder_info_h265);

	struct obs_source_info filter_info_video = {
		.id = "gstreamer-filter-video",
		.type = OBS_SOURCE_TYPE_FILTER,
		.output_flags = OBS_SOURCE_ASYNC_VIDEO,

		.get_name = gstreamer_filter_get_name_video,
		.create = gstreamer_filter_create,
		.destroy = gstreamer_filter_destroy,

		.get_defaults = gstreamer_filter_get_defaults_video,
		.get_properties = gstreamer_filter_get_properties,
		.update = gstreamer_filter_update,

		.filter_video = gstreamer_filter_filter_video,
	};

	obs_register_source(&filter_info_video);

	//struct obs_source_info filter_info_audio = {
	//	.id = "gstreamer-filter-audio",
	//	.type = OBS_SOURCE_TYPE_FILTER,
	//	.output_flags = OBS_SOURCE_AUDIO,

	//	.get_name = gstreamer_filter_get_name_audio,
	//	.create = gstreamer_filter_create,
	//	.destroy = gstreamer_filter_destroy,

	//	.get_defaults = gstreamer_filter_get_defaults_audio,
	//	.get_properties = gstreamer_filter_get_properties,
	//	.update = gstreamer_filter_update,

	//	.filter_audio = gstreamer_filter_filter_audio,
	//};

	//obs_register_source(&filter_info_audio);

	struct obs_output_info output_info = {
		.id = "gstreamer-output",
		//.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED ! OBS_OUTPUT_VIDEO,
		.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED,
		.get_name = gstreamer_output_get_name,
		.create = gstreamer_output_create,
		.destroy = gstreamer_output_destroy,
		.start = gstreamer_output_start,
		.stop = gstreamer_output_stop,

		.encoded_packet = gstreamer_output_encoded_packet,

		.get_defaults = gstreamer_output_get_defaults,
		.get_properties = gstreamer_output_get_properties,
		.raw_video = gstreamer_output_raw_video,
		.raw_audio = gstreamer_output_raw_audio,
	};

	obs_register_output(&output_info);

	//obs_output_t *output = obs_output_create("gstreamer-output", "output", NULL, NULL);
	//obs_output_set_media(output, obs_get_video(), obs_get_audio());

	gst_init(NULL, NULL);


	//obs_encoder_t *encoder_video = obs_video_encoder_create("gstreamer-encoder-h264", "encoder_video", NULL, NULL);
	//obs_encoder_t *encoder_audio = obs_audio_encoder_create("ffmpeg_aac", "encoder_audio", NULL, 0, NULL);
	//obs_output_set_video_encoder(output, encoder_video);
	//obs_output_set_audio_encoder(output, encoder_audio, 0);
	//obs_output_start(output);

	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
