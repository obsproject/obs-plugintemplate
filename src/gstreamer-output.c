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

#pragma warning(disable : 4047)
#pragma warning(disable : 4244)

#include <obs-module.h>
#include <gst/gst.h>
#include <gst/app/app.h>
#include <plugin-support.h>

typedef struct {
	GstElement *pipe;
	GstElement *video;
	GstElement *audio;
	gsize buffer_size;
	obs_output_t *output;
	obs_data_t *settings;
	struct obs_video_info ovi;
	struct obs_audio_info oai;
} data_t;

static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer user_data)
{
	obs_log(LOG_INFO, "bus_callback = called");
	data_t *data = user_data;

	switch (GST_MESSAGE_TYPE(message)) 
	{
		case GST_MESSAGE_ERROR: 
		{
			GError *err;
			gst_message_parse_error(message, &err, NULL);
			obs_log(LOG_ERROR, "[GST_MESSAGE_ERROR] : %s", err->message);
			g_error_free(err);
			break;
		}
		case GST_MESSAGE_WARNING: 
		{
			GError *err;
			gst_message_parse_warning(message, &err, NULL);
			obs_log(LOG_WARNING, "[GST_MESSAGE_WARNING] : %s", err->message);
			g_error_free(err);
		    break;
		}
		case GST_MESSAGE_INFO: 
		{
			GError *err;
			gst_message_parse_info(message, &err, NULL);
			obs_log(LOG_INFO, "[GST_MESSAGE_INFO] : %s", err->message);
			g_error_free(err);
		    break;
		}
		default:
			break;
	}

	return TRUE;
}

const char *gstreamer_output_get_name(void *type_data)
{
	return "HJM GStreamer Output";
}

void *gstreamer_output_create(obs_data_t *settings, obs_output_t *output)
{
	data_t *data = g_new0(data_t, 1);

	data->output = output;
	data->settings = settings;
	obs_log(LOG_INFO, "gstreamer_output_create = called");
	return data;
}

void gstreamer_output_destroy(void *p)
{
	data_t *data = (data_t *)p;

	if (data->pipe != NULL) {
		
		GstBus *bus = gst_element_get_bus(data->pipe);
		gst_bus_remove_watch(bus);
		gst_object_unref(bus);

		gst_element_set_state(data->pipe, GST_STATE_NULL);

		gst_object_unref(data->video);
		//gst_object_unref(data->audio);
		gst_object_unref(data->pipe);

		data->video = NULL;
		//data->audio = NULL;
		data->pipe = NULL;

		obs_log(LOG_INFO, "gstreamer_output_destroy = unreferenced");
	}

	g_free(data);
	obs_log(LOG_INFO, "gstreamer_output_destroy = end");
}

bool gstreamer_output_start(void *p)
{
	obs_log(LOG_INFO, "gstreamer_output_start = called");
	data_t *data = (data_t *)p;

	obs_get_video_info(&data->ovi);
	obs_get_audio_info(&data->oai);

	GError *err = NULL;
	char *format;

	switch (data->ovi.output_format) 
	{
		case VIDEO_FORMAT_I420:
			format = "I420";
			data->buffer_size = data->ovi.output_width * data->ovi.output_height * 3 / 2;
			break;
		case VIDEO_FORMAT_NV12:
			format = "NV12";
			data->buffer_size = data->ovi.output_width * data->ovi.output_height * 3 / 2;
			break;
		case VIDEO_FORMAT_YVYU:
			format = "YVYU";
			data->buffer_size = data->ovi.output_width * data->ovi.output_height * 2;
			break;
		case VIDEO_FORMAT_YUY2:
			format = "YUY2";
			data->buffer_size = data->ovi.output_width * data->ovi.output_height * 2;
			break;
		case VIDEO_FORMAT_UYVY:
			format = "UYVY";
			data->buffer_size = data->ovi.output_width * data->ovi.output_height * 2;
			break;
		case VIDEO_FORMAT_I422:
			format = "Y42B";
			data->buffer_size = data->ovi.output_width * data->ovi.output_height * 2;
			break;
		case VIDEO_FORMAT_RGBA:
			format = "RGBA";
			data->buffer_size = data->ovi.output_width * data->ovi.output_height * 3;
			break;
		case VIDEO_FORMAT_BGRA:
			format = "BGRA";
			data->buffer_size = data->ovi.output_width * data->ovi.output_height * 3;
			break;
		case VIDEO_FORMAT_BGRX:
			format = "BGRX";
			data->buffer_size = data->ovi.output_width * data->ovi.output_height * 3;
			break;
		case VIDEO_FORMAT_I444:
			format = "Y444";
			data->buffer_size = data->ovi.output_width * data->ovi.output_height * 3;
			break;
		default:
			blog(LOG_ERROR, "unhandled output format: %d\n", data->ovi.output_format);
			format = NULL;
			break;
	}

	//gchar *pipe = g_strdup_printf(
	//	"appsrc name=appsrc_video ! video/x-h264, width=%d, height=%d, stream-format=byte-stream ! h264parse ! udpsink host=localhost port=5000 "
	//	"appsrc name=appsrc_audio ! audio/mpeg, mpegversion=4, stream-format=raw, rate=%d, channels=%d, codec_data=(buffer)1190 ! aacparse name=audio "
	//	"%s",
	//	ovi.output_width, ovi.output_height, oai.samples_per_sec,
	//	oai.speakers, obs_data_get_string(data->settings, "pipeline"));

	//char *pipe = g_strdup_printf("appsrc name=appsrc_video ! x264enc ! udpsink host=192.168.123.18 port=5000");
	char *pipe_string = g_strdup_printf("%s ! video/x-raw, format=%s, width=%d, height=%d, framerate=%d/%d ! %s", obs_data_get_string(data->settings, "appsrc_video"), format, data->ovi.output_width, data->ovi.output_height, data->ovi.fps_num, data->ovi.fps_den, obs_data_get_string(data->settings, "pipeline"));

	data->pipe = gst_parse_launch(pipe_string, &err);

	if (err) 
	{	
		obs_log(LOG_ERROR, "gstreamer_output_start = gst_parse_launch error %s = %s", pipe_string, err->message);
		g_error_free(err);
		g_free(data);
		return NULL;
	}
	else
	{
		obs_log(LOG_INFO, "gstreamer_output_start = gst_parse_launch = %s", pipe_string);
	}

	g_free(pipe_string);

	data->video = gst_bin_get_by_name(GST_BIN(data->pipe), "appsrc_video");
	//data->audio = gst_bin_get_by_name(GST_BIN(data->pipe), "appsrc_audio");

	GstBus *bus = gst_element_get_bus(data->pipe);
	gst_bus_add_watch(bus, bus_callback, data);
	gst_object_unref(bus);

	gst_element_set_state(data->pipe, GST_STATE_PLAYING);

	if (!obs_output_can_begin_data_capture(data->output, 0)) 
	{
		obs_log(LOG_INFO, "output obs_output_can_begin_data_capture = false");
		return false;
	}
	//else if (!obs_output_initialize_encoders(data->output, 0)) 
	//{
	//	obs_log(LOG_INFO, "output obs_output_initialize_encoders = true");
	//	return false;
	//} 
	else 
	{
		obs_log(LOG_INFO, "output obs_output_can_begin_data_capture = true");
	}
	obs_output_begin_data_capture(data->output, 0);
	obs_log(LOG_INFO, "obs_output_begin_data_capture = end");

	return true;
}

void gstreamer_output_stop(void *p, uint64_t ts)
{
	obs_log(LOG_INFO, "gstreamer_output_stop = called");
	data_t *data = (data_t *)p;

	obs_output_end_data_capture(data->output);
	obs_log(LOG_INFO, "gstreamer_output_stop = obs_output_end_data_capture stopped");

	if (data->pipe) 
	{
		gst_app_src_end_of_stream(GST_APP_SRC(data->video));
		//gst_app_src_end_of_stream(GST_APP_SRC(data->audio));
		obs_log(LOG_INFO, "gstreamer_output_stop = gst_app_src_end_of_stream");
		GstBus *bus = gst_element_get_bus(data->pipe);	
		gst_bus_remove_watch(bus);
		gst_object_unref(bus);

		gst_object_unref(data->video);
		//gst_object_unref(data->audio);

		gst_element_set_state(data->pipe, GST_STATE_NULL);
		gst_object_unref(data->pipe);
		data->pipe = NULL;
		obs_log(LOG_INFO, "gstreamer_output_stop = unref complete");
	}
}

void gstreamer_output_encoded_packet(void *p, struct encoder_packet *packet)
{
	data_t *data = (data_t *)p;

	GstBuffer *buffer = gst_buffer_new_allocate(NULL, packet->size, NULL);
	gst_buffer_fill(buffer, 0, packet->data, packet->size);

	GST_BUFFER_PTS(buffer) = packet->pts * GST_SECOND / (packet->timebase_den / packet->timebase_num);
	GST_BUFFER_DTS(buffer) = packet->dts * GST_SECOND / (packet->timebase_den / packet->timebase_num);

	gst_buffer_set_flags(buffer, packet->keyframe ? 0 : GST_BUFFER_FLAG_DELTA_UNIT);

	GstElement *appsrc = data->video;

	gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);

	//obs_log(LOG_INFO, "gstreamer_output_encoded_packet = complete");
}

void gstreamer_output_raw_video(void *p, struct video_data *frame)
{
	data_t *data = (data_t *)p;

	GstBuffer *buffer = gst_buffer_new_wrapped_full(0, frame->data[0], data->buffer_size, 0, data->buffer_size, NULL, NULL);
	gst_buffer_fill(buffer, 0, frame->data[0], data->buffer_size);

	//GST_BUFFER_PTS(buffer) = frame->timestamp;

	gst_app_src_push_buffer(GST_APP_SRC(data->video), buffer);
	//obs_log(LOG_INFO, "gstreamer_output_raw_video");
}

void gstreamer_output_raw_audio(void *p, struct audio_data *frame)
{
	data_t *data = (data_t *)p;

	GstBuffer *buffer = gst_buffer_new_allocate(NULL, data->buffer_size, NULL);
	gst_buffer_fill(buffer, 0, frame->data[0], data->buffer_size);

	GST_BUFFER_PTS(buffer) = frame->timestamp;
	GST_BUFFER_OFFSET(buffer) = 0;
	//gst_buffer_set_flags(buffer, packet->keyframe ? 0 : GST_BUFFER_FLAG_DELTA_UNIT);

	GstElement *appsrc = data->video;

	gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
}

void gstreamer_output_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "pipeline", "autovideosink sync=false");
	obs_data_set_default_string(settings, "appsrc_video", "appsrc name=appsrc_video is-live=false format=GST_FORMAT_TIME do-timestamp=true");
}

obs_properties_t *gstreamer_output_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *prop = obs_properties_add_text(props, "appsrc_video", "Video Appsrc", OBS_TEXT_MULTILINE);
	obs_property_set_long_description(prop, "Appsrc gstreamer-output.");
	obs_property_set_description(prop, "Appsrc");

	prop = obs_properties_add_text(props, "pipeline", "Pipeline", OBS_TEXT_MULTILINE);
	obs_property_set_long_description(prop, "pipeline for gstreamer-output.");
	obs_property_set_description(prop, "Pipeline");

	return props;
}
#pragma warning(default : 4047)
#pragma warning(default : 4244)