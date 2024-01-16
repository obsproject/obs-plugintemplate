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

#pragma warning(disable : 4244)

#include <obs-module.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/app/app.h>
#include <gst/net/gstnet.h>

typedef struct {
	GstElement *pipe;
	GstClock *clock;
	obs_source_t *source;
	obs_data_t *settings;
	gint64 frame_count;
	gint64 audio_count;
	enum obs_media_state obs_media_state;
	gint64 seek_pos_pending;
	bool buffering;
	GSource *timeout;
	GThread *thread;
	GMainLoop *loop;
	GMutex mutex;
	GCond cond;
} data_t;

static void create_pipeline(data_t *data);

static void timeout_destroy(gpointer user_data)
{
	data_t *data = user_data;

	g_source_destroy(data->timeout);
	g_source_unref(data->timeout);
	data->timeout = NULL;
}

static gboolean pipeline_destroy(gpointer user_data)
{
	data_t *data = user_data;

	if (!data->pipe)
		return G_SOURCE_REMOVE;

	// reset OBS media flags
	data->obs_media_state = OBS_MEDIA_STATE_STOPPED;
	data->seek_pos_pending = -1;
	data->buffering = false;

	// stop the bus_callback
	GstBus *bus = gst_element_get_bus(data->pipe);
	gst_bus_remove_watch(bus);
	gst_object_unref(bus);

	// set state to GST_STATE_NULL here and _only_ here, just before
	// unreferencing data->pipe
	gst_element_set_state(data->pipe, GST_STATE_NULL);

	gst_object_unref(data->pipe);
	if (data->clock != NULL)
		gst_object_unref(data->clock);
	data->pipe = NULL;
	data->clock = NULL;

	return G_SOURCE_REMOVE;
}

static gboolean pipeline_restart(gpointer user_data)
{
	data_t *data = user_data;

	if (data->pipe)
		pipeline_destroy(data);

	create_pipeline(data);

	if (data->pipe)
		gst_element_set_state(data->pipe, GST_STATE_PLAYING);

	return G_SOURCE_REMOVE;
}

static void update_obs_media_state(GstMessage *message, data_t *data)
{
	switch (GST_MESSAGE_TYPE(message)) {
	case GST_MESSAGE_BUFFERING: {
		gint percent;
		gst_message_parse_buffering(message, &percent);
		data->buffering = (percent < 100);
	} break;
	case GST_MESSAGE_STATE_CHANGED: {
		GstState newstate;
		gst_message_parse_state_changed(message, NULL, &newstate, NULL);
		switch (newstate) {
		default:
		case GST_STATE_NULL:
			blog(LOG_WARNING,
			     "[obs-gstreamer] state is GST_STATE_NULL, unexpected.");
			data->obs_media_state = OBS_MEDIA_STATE_NONE;
			break;
		case GST_STATE_READY:
			data->obs_media_state = OBS_MEDIA_STATE_STOPPED;
			break;
		case GST_STATE_PAUSED:
			data->obs_media_state = OBS_MEDIA_STATE_PAUSED;
			break;
		case GST_STATE_PLAYING:
			data->obs_media_state = OBS_MEDIA_STATE_PLAYING;
			break;
		}
	} break;
	case GST_MESSAGE_ERROR: {
		data->obs_media_state = OBS_MEDIA_STATE_ERROR;
	} break;
	case GST_MESSAGE_EOS: {
		data->obs_media_state = OBS_MEDIA_STATE_ENDED;
	} break;
	default:
		break;
	}
}

static gboolean bus_callback(GstBus *bus, GstMessage *message,
			     gpointer user_data)
{
	data_t *data = user_data;

	update_obs_media_state(message, data);

	switch (GST_MESSAGE_TYPE(message)) {
	case GST_MESSAGE_ERROR: {
		GError *err;
		gst_message_parse_error(message, &err, NULL);
		const char *source_name = obs_source_get_name(data->source);
		blog(LOG_ERROR, "[obs-gstreamer] %s: %s", source_name,
		     err->message);
		g_error_free(err);
	} // fallthrough
	/*case GST_MESSAGE_EOS:
		if (obs_data_get_bool(data->settings, "clear_on_end"))
			obs_source_output_video(data->source, NULL);
		if (obs_data_get_bool(data->settings,
				      GST_MESSAGE_TYPE(message) ==
						      GST_MESSAGE_ERROR
					      ? "restart_on_error"
					      : "restart_on_eos") &&
		    data->timeout == NULL) {
			data->timeout = g_timeout_source_new(obs_data_get_int(data->settings, "restart_timeout"));
			g_source_set_callback(data->timeout, pipeline_restart,
					      data, timeout_destroy);
			g_source_attach(data->timeout,
					g_main_context_get_thread_default());
		}
		break;*/
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

static GstFlowReturn video_new_sample(GstAppSink *appsink, gpointer user_data)
{
	data_t *data = user_data;
	GstSample *sample = gst_app_sink_pull_sample(appsink);
	GstBuffer *buffer = gst_sample_get_buffer(sample);
	GstCaps *caps = gst_sample_get_caps(sample);
	GstMapInfo info;
	GstVideoInfo video_info;

	gst_video_info_from_caps(&video_info, caps);
	gst_buffer_map(buffer, &info, GST_MAP_READ);

	struct obs_source_frame frame;

	frame.timestamp =
		obs_data_get_bool(data->settings, "use_timestamps_video")
			? GST_BUFFER_PTS(buffer)
			: data->frame_count++;

	frame.width = video_info.width;
	frame.height = video_info.height;
	frame.linesize[0] = video_info.stride[0];
	frame.linesize[1] = video_info.stride[1];
	frame.linesize[2] = video_info.stride[2];
	frame.data[0] = info.data + video_info.offset[0];
	frame.data[1] = info.data + video_info.offset[1];
	frame.data[2] = info.data + video_info.offset[2];

	enum video_range_type range = VIDEO_RANGE_DEFAULT;
	switch (video_info.colorimetry.range) {
	case GST_VIDEO_COLOR_RANGE_0_255:
		range = VIDEO_RANGE_FULL;
		frame.full_range = 1;
		break;
	case GST_VIDEO_COLOR_RANGE_16_235:
		range = VIDEO_RANGE_PARTIAL;
		break;
	default:
		break;
	}

	enum video_colorspace cs = VIDEO_CS_DEFAULT;
	switch (video_info.colorimetry.matrix) {
	case GST_VIDEO_COLOR_MATRIX_BT709:
		cs = VIDEO_CS_709;
		break;
	case GST_VIDEO_COLOR_MATRIX_BT601:
		cs = VIDEO_CS_601;
		break;
	default:
		break;
	}

	video_format_get_parameters(cs, range, frame.color_matrix,
				    frame.color_range_min,
				    frame.color_range_max);

	switch (video_info.finfo->format) {
	case GST_VIDEO_FORMAT_I420:
		frame.format = VIDEO_FORMAT_I420;
		break;
	case GST_VIDEO_FORMAT_NV12:
		frame.format = VIDEO_FORMAT_NV12;
		break;
	case GST_VIDEO_FORMAT_BGRA:
		frame.format = VIDEO_FORMAT_BGRA;
		break;
	case GST_VIDEO_FORMAT_BGRx:
		frame.format = VIDEO_FORMAT_BGRX;
		break;
	case GST_VIDEO_FORMAT_RGBx:
	case GST_VIDEO_FORMAT_RGBA:
		frame.format = VIDEO_FORMAT_RGBA;
		break;
	case GST_VIDEO_FORMAT_UYVY:
		frame.format = VIDEO_FORMAT_UYVY;
		break;
	case GST_VIDEO_FORMAT_YUY2:
		frame.format = VIDEO_FORMAT_YUY2;
		break;
	case GST_VIDEO_FORMAT_YVYU:
		frame.format = VIDEO_FORMAT_YVYU;
		break;
#ifdef GST_VIDEO_FORMAT_I420_10LE
	case GST_VIDEO_FORMAT_I420_10LE:
		frame.format = VIDEO_FORMAT_I010;
		break;
	case GST_VIDEO_FORMAT_P010_10LE:
		frame.format = VIDEO_FORMAT_P010;
		break;
	case GST_VIDEO_FORMAT_I422_10LE:
		frame.format = VIDEO_FORMAT_I210;
		break;
	case GST_VIDEO_FORMAT_Y444_12LE:
		frame.format = VIDEO_FORMAT_I412;
		break;
#endif
	default:
		frame.format = VIDEO_FORMAT_NONE;
		const char *source_name = obs_source_get_name(data->source);
		blog(LOG_ERROR, "[obs-gstreamer] %s: Unknown video format: %s",
		     source_name, video_info.finfo->name);
		break;
	}

	obs_source_output_video(data->source, &frame);

	gst_buffer_unmap(buffer, &info);
	gst_sample_unref(sample);

	return GST_FLOW_OK;
}

//static GstFlowReturn audio_new_sample(GstAppSink *appsink, gpointer user_data)
//{
//	data_t *data = user_data;
//	GstSample *sample = gst_app_sink_pull_sample(appsink);
//	GstBuffer *buffer = gst_sample_get_buffer(sample);
//	GstCaps *caps = gst_sample_get_caps(sample);
//	GstMapInfo info;
//	//GstAudioInfo audio_info;
//
//	gst_audio_info_from_caps(&audio_info, caps);
//	gst_buffer_map(buffer, &info, GST_MAP_READ);
//
//	struct obs_source_audio audio;
//
//	audio.frames = info.size / audio_info.bpf;
//	audio.samples_per_sec = audio_info.rate;
//	audio.data[0] = info.data;
//
//	audio.timestamp =
//		obs_data_get_bool(data->settings, "use_timestamps_audio")
//			? GST_BUFFER_PTS(buffer)
//			: data->audio_count++ * GST_SECOND *
//				  (audio.frames / (double)audio_info.rate);
//
//	switch (audio_info.channels) {
//	case 1:
//		audio.speakers = SPEAKERS_MONO;
//		break;
//	case 2:
//		audio.speakers = SPEAKERS_STEREO;
//		break;
//	case 3:
//		audio.speakers = SPEAKERS_2POINT1;
//		break;
//	case 4:
//		audio.speakers = SPEAKERS_4POINT0;
//		break;
//	case 5:
//		audio.speakers = SPEAKERS_4POINT1;
//		break;
//	case 6:
//		audio.speakers = SPEAKERS_5POINT1;
//		break;
//	case 8:
//		audio.speakers = SPEAKERS_7POINT1;
//		break;
//	default:
//		audio.speakers = SPEAKERS_UNKNOWN;
//		const char *source_name = obs_source_get_name(data->source);
//		blog(LOG_ERROR,
//		     "[obs-gstreamer] %s: Unsupported audio channel count: %d",
//		     source_name, audio_info.channels);
//		break;
//	}
//
//	switch (audio_info.finfo->format) {
//	case GST_AUDIO_FORMAT_U8:
//		audio.format = AUDIO_FORMAT_U8BIT;
//		break;
//	case GST_AUDIO_FORMAT_S16LE:
//		audio.format = AUDIO_FORMAT_16BIT;
//		break;
//	case GST_AUDIO_FORMAT_S32LE:
//		audio.format = AUDIO_FORMAT_32BIT;
//		break;
//	case GST_AUDIO_FORMAT_F32LE:
//		audio.format = AUDIO_FORMAT_FLOAT;
//		break;
//	default:
//		audio.format = AUDIO_FORMAT_UNKNOWN;
//		const char *source_name = obs_source_get_name(data->source);
//		blog(LOG_ERROR, "[obs-gstreamer] %s: Unknown audio format: %s",
//		     source_name, audio_info.finfo->name);
//		break;
//	}
//
//	obs_source_output_audio(data->source, &audio);
//
//	gst_buffer_unmap(buffer, &info);
//	gst_sample_unref(sample);
//
//	return GST_FLOW_OK;
//}

const char *gstreamer_source_get_name(void *type_data)
{
	return "GStreamer Source";
}

enum obs_media_state gstreamer_source_get_state(void *user_data)
{
	data_t *data = user_data;

	if (data->buffering && data->obs_media_state != OBS_MEDIA_STATE_ERROR)
		return OBS_MEDIA_STATE_BUFFERING;

	return data->obs_media_state;
}

int64_t gstreamer_source_get_time(void *user_data)
{
	data_t *data = user_data;
	int64_t position;

	if (!data->pipe)
		return 0;

	if (gst_element_query_position(data->pipe, GST_FORMAT_TIME, &position))
		if (GST_CLOCK_TIME_IS_VALID(position))
			return GST_TIME_AS_MSECONDS(position);

	return 0;
}

int64_t gstreamer_source_get_duration(void *user_data)
{
	data_t *data = user_data;
	int64_t duration;

	if (!data->pipe)
		return 0;

	if (gst_element_query_duration(data->pipe, GST_FORMAT_TIME, &duration))
		if (GST_CLOCK_TIME_IS_VALID(duration))
			return GST_TIME_AS_MSECONDS(duration);

	return 0;
}

static gboolean pipeline_pause(gpointer user_data)
{
	data_t *data = user_data;

	if (data->pipe)
		gst_element_set_state(data->pipe, GST_STATE_PAUSED);

	return G_SOURCE_REMOVE;
}

static gboolean pipeline_play(gpointer user_data)
{
	data_t *data = user_data;

	if (data->pipe)
		gst_element_set_state(data->pipe, GST_STATE_PLAYING);

	return G_SOURCE_REMOVE;
}

void gstreamer_source_play_pause(void *user_data, bool pause)
{
	data_t *data = user_data;

	g_main_context_invoke(g_main_loop_get_context(data->loop),
			      pause ? pipeline_pause : pipeline_play, data);
}

void gstreamer_source_stop(void *user_data)
{
	data_t *data = user_data;

	g_main_context_invoke(g_main_loop_get_context(data->loop),
			      pipeline_destroy, data);
}

void gstreamer_source_restart(void *user_data)
{
	data_t *data = user_data;

	g_main_context_invoke(g_main_loop_get_context(data->loop),
			      pipeline_restart, data);
}

static gboolean pipeline_seek_to_pending(gpointer user_data)
{
	data_t *data = user_data;
	gint64 seek_pos_pending;
	gboolean seek_enabled;

	seek_pos_pending = data->seek_pos_pending;
	data->seek_pos_pending = -1;

	if (!data->pipe)
		return G_SOURCE_REMOVE;

	if (seek_pos_pending < 0) {
		const char *source_name = obs_source_get_name(data->source);
		blog(LOG_WARNING, "[obs-gstreamer] %s: No seek_pos_pending",
		     source_name);
		return G_SOURCE_REMOVE;
	}

	// determine whether seeking is possible on this pipeline
	GstQuery *query;
	gint64 start, end;
	query = gst_query_new_seeking(GST_FORMAT_TIME);
	if (!gst_element_query(data->pipe, query)) {
		const char *source_name = obs_source_get_name(data->source);
		blog(LOG_ERROR, "[obs-gstreamer] %s: Seeking query failed",
		     source_name);
		gst_query_unref(query);
		return G_SOURCE_REMOVE;
	}
	gst_query_parse_seeking(query, NULL, &seek_enabled, &start, &end);
	gst_query_unref(query);

	if (!seek_enabled) {
		const char *source_name = obs_source_get_name(data->source);
		blog(LOG_WARNING, "[obs-gstreamer] %s: Seeking is disabled",
		     source_name);
		return G_SOURCE_REMOVE;
	}

	// do the seek
	gst_element_seek_simple(data->pipe, GST_FORMAT_TIME,
				GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
				seek_pos_pending);

	return G_SOURCE_REMOVE;
}

void gstreamer_source_set_time(void *user_data, int64_t ms)
{
	data_t *data = user_data;

	data->seek_pos_pending = ms * GST_MSECOND;
	g_main_context_invoke(g_main_loop_get_context(data->loop),
			      pipeline_seek_to_pending, data);
}

static gboolean loop_startup(gpointer user_data)
{
	data_t *data = user_data;

	create_pipeline(data);

	g_mutex_lock(&data->mutex);
	g_cond_signal(&data->cond);
	g_mutex_unlock(&data->mutex);

	if (data->pipe)
		gst_element_set_state(data->pipe, GST_STATE_PLAYING);

	return G_SOURCE_REMOVE;
}

static void create_pipeline(data_t *data)
{
	GError *err = NULL;

	data->frame_count = 0;
	data->audio_count = 0;
	data->obs_media_state = OBS_MEDIA_STATE_OPENING;
	data->seek_pos_pending = -1;
	data->buffering = false;

	gchar *pipeline = g_strdup_printf(
#ifdef GST_VIDEO_FORMAT_I420_10LE
		"videoconvert name=video ! video/x-raw, format={I420,NV12,BGRA,BGRx,RGBx,RGBA,YUY2,YVYU,UYVY,I420_10LE,P010_10LE,I420_12LE,Y444_12LE} ! appsink name=video_appsink "
#else
		"videoconvert name=video ! video/x-raw, format={I420,NV12,BGRA,BGRx,RGBx,RGBA,YUY2,YVYU,UYVY} ! appsink name=video_appsink "
#endif
		"audioconvert name=audio ! audioresample ! audio/x-raw, format={U8,S16LE,S32LE,F32LE}, channels={1,2,3,4,5,6,8}, layout=interleaved ! appsink name=audio_appsink "
		"%s",
		obs_data_get_string(data->settings, "pipeline"));

	data->pipe = gst_parse_launch(pipeline, &err);
	g_free(pipeline);
	if (err != NULL) {
		const char *source_name = obs_source_get_name(data->source);
		blog(LOG_ERROR, "[obs-gstreamer] %s: Cannot start pipeline: %s",
		     source_name, err->message);
		g_error_free(err);

		data->obs_media_state = OBS_MEDIA_STATE_ERROR;

		obs_source_output_video(data->source, NULL);

		return;
	}

	GstAppSinkCallbacks video_cbs = {NULL, NULL, video_new_sample};

	GstElement *appsink =
		gst_bin_get_by_name(GST_BIN(data->pipe), "video_appsink");
	gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &video_cbs, data,
				   NULL);

	if (!obs_data_get_bool(data->settings, "sync_appsink_video"))
		g_object_set(appsink, "sync", FALSE, NULL);

	if (obs_data_get_bool(data->settings, "disable_async_appsink_video"))
		g_object_set(appsink, "async", FALSE, NULL);

	if (obs_data_get_bool(data->settings, "block_video"))
		g_object_set(appsink, "max-buffers", 1, NULL);

	if (obs_data_get_bool(data->settings, "drop_video"))
		gst_app_sink_set_drop(GST_APP_SINK(appsink), TRUE);

	// check if connected and remove if not
	GstElement *sink = gst_bin_get_by_name(GST_BIN(data->pipe), "video");
	GstPad *pad = gst_element_get_static_pad(sink, "sink");
	if (!gst_pad_is_linked(pad))
		gst_bin_remove(GST_BIN(data->pipe), appsink);
	gst_object_unref(pad);
	gst_object_unref(sink);

	gst_object_unref(appsink);

	//GstAppSinkCallbacks audio_cbs = {NULL, NULL, audio_new_sample};

	appsink = gst_bin_get_by_name(GST_BIN(data->pipe), "audio_appsink");
	//gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &audio_cbs, data,
	//			   NULL);

	if (!obs_data_get_bool(data->settings, "sync_appsink_audio"))
		g_object_set(appsink, "sync", FALSE, NULL);

	if (obs_data_get_bool(data->settings, "disable_async_appsink_audio"))
		g_object_set(appsink, "async", FALSE, NULL);

	if (obs_data_get_bool(data->settings, "block_audio"))
		g_object_set(appsink, "max-buffers", 1, NULL);

	if (obs_data_get_bool(data->settings, "drop_audio"))
		gst_app_sink_set_drop(GST_APP_SINK(appsink), TRUE);

	// check if connected and remove if not
	sink = gst_bin_get_by_name(GST_BIN(data->pipe), "audio");
	pad = gst_element_get_static_pad(sink, "sink");
	if (!gst_pad_is_linked(pad))
		gst_bin_remove(GST_BIN(data->pipe), appsink);
	gst_object_unref(pad);
	gst_object_unref(sink);

	gst_object_unref(appsink);

	GstBus *bus = gst_element_get_bus(data->pipe);
	gst_bus_add_watch(bus, bus_callback, data);
	gst_object_unref(bus);

	// set clock
	const char *server = obs_data_get_string(data->settings, "ntp_server");
	if (strlen(server) > 0) {
		gint clock_port = obs_data_get_int(data->settings, "ntp_port");
		data->clock =
			gst_ntp_clock_new("net_clock", server, clock_port, 0);
		blog(LOG_INFO, "Connect to NTP server %s", server);
		if (data->clock == NULL) {
			blog(LOG_ERROR, "Failed to connect to net clock %s",
			     server);
			return;
		}
		if (!gst_clock_wait_for_sync(data->clock, 5 * GST_SECOND)) {
			blog(LOG_ERROR,
			     "Failed to sync to net clock %s, timeout", server);
			return;
		}
		gst_pipeline_use_clock(GST_PIPELINE(data->pipe),
				       GST_CLOCK(data->clock));
	}
	gint latency = obs_data_get_int(data->settings, "latency");
	// set latency
	if (latency) {
		gst_pipeline_set_latency(GST_PIPELINE(data->pipe),
					 latency * GST_MSECOND);
		gint cur_latency =
			gst_pipeline_get_latency(GST_PIPELINE(data->pipe)) /
			GST_MSECOND;
		blog(LOG_INFO, "Set latency for pipeline to %dms", cur_latency);
	}
}

static gpointer _start(gpointer user_data)
{
	data_t *data = user_data;

	GMainContext *context = g_main_context_new();

	g_main_context_push_thread_default(context);

	data->loop = g_main_loop_new(context, FALSE);

	GSource *source = g_idle_source_new();
	g_source_set_callback(source, loop_startup, data, NULL);
	g_source_attach(source, context);

	g_main_loop_run(data->loop);

	if (data->pipe != NULL) {
		gst_element_set_state(data->pipe, GST_STATE_NULL);

		GstBus *bus = gst_element_get_bus(data->pipe);
		gst_bus_remove_watch(bus);
		gst_object_unref(bus);

		gst_object_unref(data->pipe);
		if (data->clock != NULL)
			gst_object_unref(data->clock);
		data->pipe = NULL;
		data->clock = NULL;
	}

	g_main_loop_unref(data->loop);
	data->loop = NULL;

	g_main_context_unref(context);

	return NULL;
}

static void start(data_t *data)
{
	g_mutex_lock(&data->mutex);

	data->thread = g_thread_new("GStreamer Source", _start, data);

	g_cond_wait(&data->cond, &data->mutex);
	g_mutex_unlock(&data->mutex);
}

void *gstreamer_source_create(obs_data_t *settings, obs_source_t *source)
{
	bool nobuf = obs_data_get_bool(settings, "no_buffer");
	obs_source_set_async_unbuffered(source, nobuf);

	data_t *data = g_new0(data_t, 1);

	data->source = source;
	data->settings = settings;

	g_mutex_init(&data->mutex);
	g_cond_init(&data->cond);

	if (obs_data_get_bool(settings, "stop_on_hide") == false)
		start(data);

	return data;
}

static void stop(data_t *data)
{
	if (data->thread == NULL)
		return;

	g_main_loop_quit(data->loop);

	g_thread_join(data->thread);
	data->thread = NULL;

	obs_source_output_video(data->source, NULL);
}

void gstreamer_source_destroy(void *user_data)
{
	data_t *data = user_data;

	stop(data);

	g_mutex_clear(&data->mutex);
	g_cond_clear(&data->cond);

	g_free(data);
}

void gstreamer_source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(
		settings, "pipeline",
		"videotestsrc is-live=true ! video/x-raw, framerate=30/1, width=960, height=540 ! video. "
		"audiotestsrc wave=ticks is-live=true ! audio/x-raw, channels=2, rate=44100 ! audio.");
	obs_data_set_default_bool(settings, "use_timestamps_video", true);
	obs_data_set_default_bool(settings, "use_timestamps_audio", true);
	obs_data_set_default_bool(settings, "sync_appsink_video", true);
	obs_data_set_default_bool(settings, "sync_appsink_audio", true);
	obs_data_set_default_bool(settings, "disable_async_appsink_video",
				  false);
	obs_data_set_default_bool(settings, "disable_async_appsink_audio",
				  false);
	obs_data_set_default_bool(settings, "restart_on_eos", true);
	obs_data_set_default_bool(settings, "restart_on_error", false);
	obs_data_set_default_int(settings, "restart_timeout", 2000);
	obs_data_set_default_bool(settings, "no_buffer", false);
	obs_data_set_default_int(settings, "latency", 0);
	obs_data_set_default_string(settings, "ntp_server", "");
	obs_data_set_default_int(settings, "ntp_port", 123);
	obs_data_set_default_bool(settings, "stop_on_hide", true);
	obs_data_set_default_bool(settings, "block_video", false);
	obs_data_set_default_bool(settings, "block_audio", false);
	obs_data_set_default_bool(settings, "drop_video", false);
	obs_data_set_default_bool(settings, "drop_audio", false);
	obs_data_set_default_bool(settings, "clear_on_end", true);
}

void gstreamer_source_update(void *data, obs_data_t *settings);

static bool on_apply_clicked(obs_properties_t *props, obs_property_t *property,
			     void *data)
{
	gstreamer_source_update(data, ((data_t *)data)->settings);

	return false;
}

obs_properties_t *gstreamer_source_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_property_t *prop = obs_properties_add_text(
		props, "pipeline", "Pipeline", OBS_TEXT_MULTILINE);
	obs_property_set_long_description(
		prop,
		"Use \"video\" and \"audio\" as names for the media sinks.");
	obs_properties_add_bool(props, "use_timestamps_video",
				"Use pipeline time stamps (video) HJM Church");
	obs_properties_add_bool(props, "use_timestamps_audio",
				"Use pipeline time stamps (audio)");
	obs_properties_add_bool(props, "sync_appsink_video",
				"Sync appsink to clock (video)");
	obs_properties_add_bool(props, "sync_appsink_audio",
				"Sync appsink to clock (audio)");
	obs_properties_add_bool(
		props, "disable_async_appsink_video",
		"Disable asynchronous state change in appsink (video)");
	obs_properties_add_bool(
		props, "disable_async_appsink_audio",
		"Disable asynchronous state change in appsink (audio)");
	obs_properties_add_bool(props, "restart_on_eos",
				"Try to restart when end of stream is reached");
	obs_properties_add_bool(
		props, "restart_on_error",
		"Try to restart after pipeline encountered an error");
	obs_properties_add_int(props, "restart_timeout", "Error timeout (ms)",
			       0, 10000, 100);
	obs_properties_add_bool(props, "stop_on_hide",
				"Stop pipeline when hidden");
	obs_properties_add_bool(
		props, "clear_on_end",
		"Clear image data after end-of-stream or error");
	obs_properties_add_bool(props, "block_video",
				"Disable video sink buffer");
	obs_properties_add_bool(props, "drop_video",
				"Drop video when sink is not fast enough");
	obs_properties_add_bool(props, "block_audio",
				"Disable audio sink buffer");
	obs_properties_add_bool(props, "drop_audio",
				"Drop audio when sink is not fast enough");
	obs_properties_add_bool(props, "no_buffer", "Disable buffering in OBS");
	prop = obs_properties_add_int(props, "latency", "Fixed latency (ms)", 0,
				      10000, 10);
	obs_property_set_long_description(
		prop,
		"This sets a fixed latency for the pipeline for syncing different inputs.\nCheck the error log for clock errors if the set latency is too low.\nSetting 0 auto-detects lowest possible latency for the given pipeline.");
	prop = obs_properties_add_text(props, "ntp_server", "NTP server",
				       OBS_TEXT_DEFAULT);
	obs_property_set_long_description(
		prop,
		"This sets a NTP server for syncing the gstreamer clock to.\nUse e.g. with rtspsrc rfc7273-sync or ntp-sync options.\nLeave empty to not use a NTP server.");
	obs_properties_add_int(props, "ntp_port", "NTP server port", 1, 65536,
			       1);
	obs_properties_add_button2(props, "apply", "Apply", on_apply_clicked,
				   data);

	return props;
}

void gstreamer_source_update(void *data, obs_data_t *settings)
{
	stop(data);

	bool nobuf = obs_data_get_bool(settings, "no_buffer");
	obs_source_set_async_unbuffered(((data_t *)data)->source, nobuf);

	// Don't start the pipeline if source is hidden and 'stop_on_hide' is set.
	// From GUI this is probably irrelevant but works around some quirks when
	// controlled from script.
	if (obs_data_get_bool(settings, "stop_on_hide") &&
	    !obs_source_showing(((data_t *)data)->source))
		return;

	start(data);
}

void gstreamer_source_show(void *data)
{
	if (((data_t *)data)->pipe == NULL)
		start(data);
}

void gstreamer_source_hide(void *data)
{
	if (obs_data_get_bool(((data_t *)data)->settings, "stop_on_hide"))
		stop(data);
}
#pragma warning(default : 4244)