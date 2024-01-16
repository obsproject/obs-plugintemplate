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
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/app/app.h>

typedef struct {
	GstElement *pipe;
	GstElement *appsrc;
	GstElement *appsink;
	gint frame_size;
	GstAudioInfo audio_info;
	obs_source_t *source;
	obs_data_t *settings;
} data_t;

static gboolean bus_callback(GstBus *bus, GstMessage *message,
			     gpointer user_data)
{
	data_t *data = user_data;

	switch (GST_MESSAGE_TYPE(message)) {
	case GST_MESSAGE_ERROR: {
		GError *err;
		gst_message_parse_error(message, &err, NULL);
		const char *source_name = obs_source_get_name(data->source);
		blog(LOG_ERROR, "[obs-gstreamer] %s: %s", source_name,
		     err->message);
		g_error_free(err);
		break;
	}
	case GST_MESSAGE_WARNING: {
		GError *err;
		gst_message_parse_warning(message, &err, NULL);
		const char *source_name = obs_source_get_name(data->source);
		blog(LOG_WARNING, "[obs-gstreamer] %s: %s", source_name,
		     err->message);
		g_error_free(err);
	} break;
	default:
		break;
	}

	return TRUE;
}

const char *gstreamer_filter_get_name_video(void *type_data)
{
	return "GStreamer Filter (Video)";
}

const char *gstreamer_filter_get_name_audio(void *type_data)
{
	return "GStreamer Filter (Audio)";
}

void *gstreamer_filter_create(obs_data_t *settings, obs_source_t *source)
{
	data_t *data = g_new0(data_t, 1);

	data->source = source;
	data->settings = settings;

	return data;
}

void gstreamer_filter_destroy(void *p)
{
	data_t *data = (data_t *)p;

	if (data->pipe != NULL) {
		GstBus *bus = gst_element_get_bus(data->pipe);
		gst_bus_remove_watch(bus);
		gst_object_unref(bus);

		gst_element_set_state(data->pipe, GST_STATE_NULL);

		gst_object_unref(data->appsrc);
		gst_object_unref(data->appsink);
		gst_object_unref(data->pipe);
	}

	g_free(data);
}

void gstreamer_filter_get_defaults_video(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "pipeline",
				    "videoflip video-direction=horiz");
}

void gstreamer_filter_get_defaults_audio(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "pipeline",
				    "audioecho delay=200000000 intensity=0.3");
}

void gstreamer_filter_update(void *data, obs_data_t *settings);

static bool on_apply_clicked(obs_properties_t *props, obs_property_t *property,
			     void *data)
{
	gstreamer_filter_update(data, ((data_t *)data)->settings);

	return false;
}

obs_properties_t *gstreamer_filter_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_property_t *prop = obs_properties_add_text(
		props, "pipeline", "Pipeline", OBS_TEXT_MULTILINE);
	obs_property_set_long_description(prop,
					  "Use \"identity\" for passthru");
	obs_properties_add_button2(props, "apply", "Apply", on_apply_clicked,
				   data);

	return props;
}

void gstreamer_filter_update(void *p, obs_data_t *settings)
{
	data_t *data = (data_t *)p;

	if (data->pipe != NULL) {
		gst_element_set_state(data->pipe, GST_STATE_NULL);

		gst_object_unref(data->appsink);
		gst_object_unref(data->appsrc);
		gst_object_unref(data->pipe);

		data->appsink = NULL;
		data->appsrc = NULL;
		data->pipe = NULL;
	}
}

struct obs_source_frame *
gstreamer_filter_filter_video(void *p, struct obs_source_frame *frame)
{
	GstMapInfo info;
	data_t *data = (data_t *)p;

	if (data->pipe == NULL) {
		GError *err = NULL;
		gchar *format = "";

		switch (frame->format) {
		case VIDEO_FORMAT_I420:
			data->frame_size = frame->width * frame->height * 3 / 2;
			format = "I420";
			break;
		case VIDEO_FORMAT_NV12:
			data->frame_size = frame->width * frame->height * 3 / 2;
			format = "NV12";
			break;
		case VIDEO_FORMAT_I422:
			data->frame_size = frame->width * frame->height * 2;
			format = "Y42B";
			break;

		case VIDEO_FORMAT_YVYU:
			data->frame_size = frame->width * frame->height * 2;
			format = "YVYU";
			break;
		case VIDEO_FORMAT_YUY2:
			data->frame_size = frame->width * frame->height * 2;
			format = "YUY2";
			break;
		case VIDEO_FORMAT_UYVY:
			data->frame_size = frame->width * frame->height * 2;
			format = "UYVY";
			break;

		case VIDEO_FORMAT_RGBA:
			data->frame_size = frame->width * frame->height * 4;
			format = "RGBA";
			break;
		case VIDEO_FORMAT_BGRA:
			data->frame_size = frame->width * frame->height * 4;
			format = "BGRA";
			break;
		case VIDEO_FORMAT_BGRX:
			data->frame_size = frame->width * frame->height * 4;
			format = "BGRx";
			break;
		default: {
			const char *source_name =
				obs_source_get_name(data->source);
			blog(LOG_ERROR,
			     "[obs-gstreamer] %s: invalid video format: %d",
			     source_name, frame->format);
			break;
		}
		}

		gchar *str = g_strdup_printf(
			"appsrc name=appsrc format=time ! video/x-raw, width=%d, height=%d, format=%s, framerate=0/1 ! videoconvert ! "
			"%s ! videoconvert ! video/x-raw, width=%d, height=%d, format=%s, framerate=0/1 ! appsink name=appsink sync=false",
			frame->width, frame->height, format,
			obs_data_get_string(data->settings, "pipeline"),
			frame->width, frame->height, format);
		data->pipe = gst_parse_launch(str, &err);
		g_free(str);
		if (err != NULL) {
			const char *source_name =
				obs_source_get_name(data->source);
			blog(LOG_ERROR, "[obs-gstreamer] %s: %s", source_name,
			     err->message);
			g_error_free(err);

			gst_object_unref(data->pipe);
			data->pipe = NULL;

			return frame;
		}

		data->appsrc =
			gst_bin_get_by_name(GST_BIN(data->pipe), "appsrc");
		data->appsink =
			gst_bin_get_by_name(GST_BIN(data->pipe), "appsink");

		GstBus *bus = gst_element_get_bus(data->pipe);
		gst_bus_add_watch(bus, bus_callback, data);
		gst_object_unref(bus);

		gst_element_set_state(data->pipe, GST_STATE_PLAYING);
	}

	GstBuffer *buffer =
		gst_buffer_new_wrapped_full(0, frame->data[0], data->frame_size,
					    0, data->frame_size, NULL, NULL);

	GST_BUFFER_PTS(buffer) = frame->timestamp;

	gst_app_src_push_buffer(GST_APP_SRC(data->appsrc), buffer);

	GstSample *sample =
		gst_app_sink_pull_sample(GST_APP_SINK(data->appsink));
	if (sample == NULL)
		return frame;
	buffer = gst_sample_get_buffer(sample);

	gst_buffer_map(buffer, &info, GST_MAP_READ);

	if (info.size == data->frame_size)
		memcpy(frame->data[0], info.data, data->frame_size);

	gst_buffer_unmap(buffer, &info);
	gst_sample_unref(sample);

	return frame;
}

//struct obs_audio_data *
//gstreamer_filter_filter_audio(void *p, struct obs_audio_data *audio_data)
//{
//	GstMapInfo info;
//	data_t *data = (data_t *)p;
//
//	if (data->pipe == NULL) {
//		GError *err = NULL;
//		struct obs_audio_info audio_info;
//
//		obs_get_audio_info(&audio_info);
//
//		gst_audio_info_init(&data->audio_info);
//		gst_audio_info_set_format(&data->audio_info,
//					  GST_AUDIO_FORMAT_F32LE,
//					  audio_info.samples_per_sec,
//					  audio_info.speakers, NULL);
//		data->audio_info.layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;
//
//		gchar *str = g_strdup_printf(
//			"appsrc name=appsrc format=time ! audio/x-raw, rate=%d, channels=%d, format=F32LE, layout=non-interleaved ! audioconvert ! "
//			"%s ! audioconvert ! audio/x-raw, rate=%d, channels=%d, format=F32LE, layout=non-interleaved ! appsink name=appsink sync=false",
//			data->audio_info.rate, data->audio_info.channels,
//			obs_data_get_string(data->settings, "pipeline"),
//			data->audio_info.rate, data->audio_info.channels);
//		data->pipe = gst_parse_launch(str, &err);
//		g_free(str);
//		if (err != NULL) {
//			const char *source_name =
//				obs_source_get_name(data->source);
//			blog(LOG_ERROR, "[obs-gstreamer] %s: %s", source_name,
//			     err->message);
//			g_error_free(err);
//
//			gst_object_unref(data->pipe);
//			data->pipe = NULL;
//
//			return audio_data;
//		}
//
//		data->appsrc =
//			gst_bin_get_by_name(GST_BIN(data->pipe), "appsrc");
//		data->appsink =
//			gst_bin_get_by_name(GST_BIN(data->pipe), "appsink");
//
//		gst_element_set_state(data->pipe, GST_STATE_PLAYING);
//	}
//
//	gint channel_size = data->audio_info.bpf * audio_data->frames /
//			    data->audio_info.channels;
//
//	GstBuffer *buffer = gst_buffer_new_allocate(
//		NULL, channel_size * data->audio_info.channels, NULL);
//
//	gst_buffer_map(buffer, &info, GST_MAP_WRITE);
//
//	for (int i = 0; i < data->audio_info.channels; i++)
//		memcpy(info.data + i * channel_size, audio_data->data[i],
//		       channel_size);
//
//	gst_buffer_unmap(buffer, &info);
//
//	gst_buffer_add_audio_meta(buffer, &data->audio_info, audio_data->frames,
//				  NULL);
//	GST_BUFFER_PTS(buffer) = audio_data->timestamp;
//
//	gst_app_src_push_buffer(GST_APP_SRC(data->appsrc), buffer);
//
//	GstSample *sample =
//		gst_app_sink_pull_sample(GST_APP_SINK(data->appsink));
//	if (sample == NULL)
//		return audio_data;
//
//	buffer = gst_sample_get_buffer(sample);
//
//	gst_buffer_map(buffer, &info, GST_MAP_READ);
//
//	if (info.size == channel_size * data->audio_info.channels)
//		for (int i = 0; i < data->audio_info.channels; i++)
//			memcpy(audio_data->data[i],
//			       info.data + i * channel_size, channel_size);
//
//	gst_buffer_unmap(buffer, &info);
//	gst_sample_unref(sample);
//
//	return audio_data;
//}
