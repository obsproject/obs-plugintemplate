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

#pragma warning(disable : 4018)

#define _GNU_SOURCE

#include <dirent.h>
#include <obs-module.h>
#include <util/dstr.h>
#include <gst/gst.h>
#include <gst/app/app.h>

typedef struct {
	GstElement *pipe;
	GstElement *appsrc;
	GstElement *appsink;
	gsize buffer_size;
	guint8 *codec_data;
	size_t codec_data_size;
	GstSample *sample;
	GstMapInfo info;
	obs_encoder_t *encoder;
	obs_data_t *settings;
	struct obs_video_info ovi;
} data_t;

const char *gstreamer_encoder_get_name_h264(void *type_data)
{
	return "GStreamer Encoder H.264";
}

const char *gstreamer_encoder_get_name_h265(void *type_data)
{
	return "GStreamer Encoder H.265";
}

char *gstreamer_get_format(data_t *data)
{
	char *format;
	switch (data->ovi.output_format) {
	case VIDEO_FORMAT_I420:
		format = "I420";
		data->buffer_size = data->ovi.output_width *
				    data->ovi.output_height * 3 / 2;
		break;
	case VIDEO_FORMAT_NV12:
		format = "NV12";
		data->buffer_size = data->ovi.output_width *
				    data->ovi.output_height * 3 / 2;
		break;
	case VIDEO_FORMAT_YVYU:
		format = "YVYU";
		data->buffer_size =
			data->ovi.output_width * data->ovi.output_height * 2;
		break;
	case VIDEO_FORMAT_YUY2:
		format = "YUY2";
		data->buffer_size =
			data->ovi.output_width * data->ovi.output_height * 2;
		break;
	case VIDEO_FORMAT_UYVY:
		format = "UYVY";
		data->buffer_size =
			data->ovi.output_width * data->ovi.output_height * 2;
		break;
	case VIDEO_FORMAT_I422:
		format = "Y42B";
		data->buffer_size =
			data->ovi.output_width * data->ovi.output_height * 2;
		break;
	case VIDEO_FORMAT_RGBA:
		format = "RGBA";
		data->buffer_size =
			data->ovi.output_width * data->ovi.output_height * 3;
	case VIDEO_FORMAT_BGRA:
		format = "BGRA";
		data->buffer_size =
			data->ovi.output_width * data->ovi.output_height * 3;
	case VIDEO_FORMAT_BGRX:
		format = "BGRX";
		data->buffer_size =
			data->ovi.output_width * data->ovi.output_height * 3;
	case VIDEO_FORMAT_I444:
		format = "Y444";
		data->buffer_size =
			data->ovi.output_width * data->ovi.output_height * 3;
		break;
	default:
		blog(LOG_ERROR, "unhandled output format: %d\n",
		     data->ovi.output_format);
		format = NULL;
	}

	return format;
}

void *gstreamer_encoder_create_h264(obs_data_t *settings, obs_encoder_t *encoder)
{
	data_t *data = g_new0(data_t, 1);

	data->encoder = encoder;
	data->settings = settings;

	obs_get_video_info(&data->ovi);

	data->ovi.output_width = obs_encoder_get_width(encoder);
	data->ovi.output_height = obs_encoder_get_height(encoder);

	const char *format = gstreamer_get_format(data);

	const gchar *encoder_type = obs_data_get_string(data->settings, "encoder_type");

	const gchar *encoder_string = obs_data_get_string(data->settings, "encoder_string");

	const gboolean is_cbr = g_strcmp0(obs_data_get_string(data->settings, "rate_control"), "CBR") == 0 ? true : false;

	//gchar *encoder_string = "";
	//if (g_strcmp0(encoder_type, "x264") == 0) {
	//	encoder_string = g_strdup_printf(
	//		"x264enc tune=zerolatency bitrate=%d pass=%s key-int-max=%d",
	//		(int)obs_data_get_int(data->settings, "bitrate"),
	//		is_cbr ? "cbr" : "pass1",
	//		(int)obs_data_get_int(data->settings, "keyint_sec") *
	//			data->ovi.fps_num / data->ovi.fps_den);
	//} else if (g_strcmp0(encoder_type, "nvh264enc") == 0) {
	//	encoder_string = g_strdup_printf(
	//		"nvh264enc bitrate=%d rc-mode=%s gop-size=%d",
	//		(int)obs_data_get_int(data->settings, "bitrate"),
	//		is_cbr ? "cbr" : "vbr",
	//		(int)obs_data_get_int(data->settings, "keyint_sec") *
	//			data->ovi.fps_num / data->ovi.fps_den);
	//} else if (g_strcmp0(encoder_type, "vaapih264enc") == 0) {
	//	g_setenv("GST_VAAPI_DRM_DEVICE",
	//		 obs_data_get_string(data->settings, "device"), TRUE);
	//	encoder_string = g_strdup_printf(
	//		"vaapih264enc bitrate=%d rate-control=%s keyframe-period=%d",
	//		(int)obs_data_get_int(data->settings, "bitrate"),
	//		is_cbr ? "cbr" : "vbr",
	//		(int)obs_data_get_int(data->settings, "keyint_sec") *
	//			data->ovi.fps_num / data->ovi.fps_den);
	//} else if (g_strcmp0(encoder_type, "omxh264enc") == 0) {
	//	encoder_string = g_strdup_printf(
	//		"omxh264enc target-bitrate=%d control-rate=%s periodicity-idr=%d",
	//		(int)obs_data_get_int(data->settings, "bitrate") * 1000,
	//		is_cbr ? "constant" : "variable",
	//		(int)obs_data_get_int(data->settings, "keyint_sec") *
	//			data->ovi.fps_num / data->ovi.fps_den);
	//} else if (g_strcmp0(encoder_type, "omxh264enc_old") == 0) {
	//	encoder_string = g_strdup_printf(
	//		"omxh264enc bitrate=%d control-rate=%s iframeinterval=%d",
	//		(int)obs_data_get_int(data->settings, "bitrate") * 1000,
	//		is_cbr ? "constant" : "variable",
	//		(int)obs_data_get_int(data->settings, "keyint_sec") *
	//			data->ovi.fps_num / data->ovi.fps_den);
	//} else if (g_strcmp0(encoder_type, "vtenc_h264") == 0) {
	//	encoder_string = g_strdup_printf(
	//		"vtenc_h264 bitrate=%d max-keyframe-interval=%d",
	//		(int)obs_data_get_int(data->settings, "bitrate"),
	//		(int)obs_data_get_int(data->settings, "keyint_sec") *
	//			data->ovi.fps_num / data->ovi.fps_den);
	//} else if (g_strcmp0(encoder_type, "msdkh264enc") == 0) {
	//	encoder_string = g_strdup_printf(
	//		"msdkh264enc bitrate=%d rate-control=%s gop-size=%d",
	//		(int)obs_data_get_int(data->settings, "bitrate"),
	//		is_cbr ? "cbr" : "vbr",
	//		(int)obs_data_get_int(data->settings, "keyint_sec") *
	//			data->ovi.fps_num / data->ovi.fps_den);
	//} else if (g_strcmp0(encoder_type, "mpph264enc") == 0) {
	//	encoder_string = g_strdup_printf(
	//		"mpph264enc bps=%d rc-mode=%s gop=%d",
	//		(int)obs_data_get_int(data->settings, "bitrate") * 1000,
	//		is_cbr ? "cbr" : "vbr",
	//		(int)obs_data_get_int(data->settings, "keyint_sec") *
	//			data->ovi.fps_num / data->ovi.fps_den);
	//} else {
	//	blog(LOG_ERROR, "invalid encoder selected");
	//	return NULL;
	//}

	gchar *pipe_string = g_strdup_printf(
		"appsrc name=appsrc ! video/x-raw, format=%s, width=%d, height=%d, framerate=%d/%d, interlace-mode=progressive ! videoconvert ! %s name=video_encoder ! h264parse ! video/x-h264, stream-format=byte-stream, alignment=au ! appsink sync=false name=appsink",
		format, data->ovi.output_width, data->ovi.output_height,
		data->ovi.fps_num, data->ovi.fps_den, encoder_string,
		obs_data_get_string(data->settings, "extra_options"));

	GError *err = NULL;

	data->pipe = gst_parse_launch(pipe_string, &err);

	//g_free(encoder_string);
	g_free(pipe_string);

	if (err != NULL) {
		blog(LOG_ERROR, "%s", err->message);
		return NULL;
	}

	data->appsrc = gst_bin_get_by_name(GST_BIN(data->pipe), "appsrc");
	data->appsink = gst_bin_get_by_name(GST_BIN(data->pipe), "appsink");

	gst_element_set_state(data->pipe, GST_STATE_PLAYING);

	return data;
}

void *gstreamer_encoder_create_h265(obs_data_t *settings, obs_encoder_t *encoder)
{
	data_t *data = g_new0(data_t, 1);

	data->encoder = encoder;
	data->settings = settings;

	obs_get_video_info(&data->ovi);

	data->ovi.output_width = obs_encoder_get_width(encoder);
	data->ovi.output_height = obs_encoder_get_height(encoder);

	const char *format = gstreamer_get_format(data);

	const gchar *encoder_type =
		obs_data_get_string(data->settings, "encoder_type");

	const gboolean is_cbr =
		g_strcmp0(obs_data_get_string(data->settings, "rate_control"),
			  "CBR") == 0
			? true
			: false;

	gchar *encoder_string = "";
	if (g_strcmp0(encoder_type, "vaapih265enc") == 0) {
		g_setenv("GST_VAAPI_DRM_DEVICE",
			 obs_data_get_string(data->settings, "device"), TRUE);
		encoder_string = g_strdup_printf(
			"vaapih265enc bitrate=%d rate-control=%s keyframe-period=%d",
			(int)obs_data_get_int(data->settings, "bitrate"),
			is_cbr ? "cbr" : "vbr",
			(int)obs_data_get_int(data->settings, "keyint_sec") *
				data->ovi.fps_num / data->ovi.fps_den);
	} else if (g_strcmp0(encoder_type, "nvh265enc") == 0) {
		encoder_string = g_strdup_printf(
			"nvh265enc bitrate=%d rc-mode=%s gop-size=%d",
			(int)obs_data_get_int(data->settings, "bitrate"),
			is_cbr ? "cbr" : "vbr",
			(int)obs_data_get_int(data->settings, "keyint_sec") *
				data->ovi.fps_num / data->ovi.fps_den);
	} else if (g_strcmp0(encoder_type, "msdkh265enc") == 0) {
		encoder_string = g_strdup_printf(
			"msdkh265enc bitrate=%d rate-control=%s gop-size=%d",
			(int)obs_data_get_int(data->settings, "bitrate"),
			is_cbr ? "cbr" : "vbr",
			(int)obs_data_get_int(data->settings, "keyint_sec") *
				data->ovi.fps_num / data->ovi.fps_den);
	} else if (g_strcmp0(encoder_type, "mpph265enc") == 0) {
		encoder_string = g_strdup_printf(
			"mpph265enc bps=%d rc-mode=%s gop=%d",
			(int)obs_data_get_int(data->settings, "bitrate") * 1000,
			is_cbr ? "cbr" : "vbr",
			(int)obs_data_get_int(data->settings, "keyint_sec") *
				data->ovi.fps_num / data->ovi.fps_den);
	} else {
		blog(LOG_ERROR, "invalid encoder selected");
		return NULL;
	}

	gchar *pipe_string = g_strdup_printf(
		"appsrc name=appsrc ! video/x-raw, format=%s, width=%d, height=%d, framerate=%d/%d, interlace-mode=progressive ! videoconvert ! %s name=video_encoder  %s ! h265parse ! video/x-h265, stream-format=byte-stream, alignment=au ! appsink sync=false name=appsink",
		format, data->ovi.output_width, data->ovi.output_height,
		data->ovi.fps_num, data->ovi.fps_den, encoder_string,
		obs_data_get_string(data->settings, "extra_options"));

	GError *err = NULL;

	data->pipe = gst_parse_launch(pipe_string, &err);

	g_free(encoder_string);
	g_free(pipe_string);

	if (err != NULL) {
		blog(LOG_ERROR, "%s", err->message);
		return NULL;
	}

	data->appsrc = gst_bin_get_by_name(GST_BIN(data->pipe), "appsrc");
	data->appsink = gst_bin_get_by_name(GST_BIN(data->pipe), "appsink");

	gst_element_set_state(data->pipe, GST_STATE_PLAYING);

	return data;
}

void gstreamer_encoder_destroy(void *p)
{
	data_t *data = (data_t *)p;

	gst_element_set_state(data->pipe, GST_STATE_NULL);

	gst_object_unref(data->appsink);
	gst_object_unref(data->appsrc);
	gst_object_unref(data->pipe);

	data->appsink = NULL;
	data->appsrc = NULL;
	data->pipe = NULL;

	if (data->sample != NULL) {
		GstBuffer *buffer = gst_sample_get_buffer(data->sample);
		gst_buffer_unmap(buffer, &data->info);
		gst_sample_unref(data->sample);
	}

	g_free(data->codec_data);
	g_free(data);
}

bool gstreamer_encoder_encode(void *p, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet)
{
	data_t *data = (data_t *)p;

	// delayed release of previous sample
	if (data->sample != NULL) {
		GstBuffer *buffer = gst_sample_get_buffer(data->sample);
		gst_buffer_unmap(buffer, &data->info);
		gst_sample_unref(data->sample);
		data->sample = NULL;
	}

	GstBuffer *buffer;

	if (obs_data_get_bool(data->settings, "force_copy") == true) {
		buffer = gst_buffer_new_allocate(NULL, data->buffer_size, NULL);

		gint32 offset = 0;
		for (int j = 0; frame->linesize[j] != 0; j++) {
			for (int i = 0; i < data->ovi.output_height; i++) {
				gst_buffer_fill(buffer, offset, frame->data[j] + i * frame->linesize[j], frame->linesize[j]);
				offset += frame->linesize[j];
			}
		}
	}
	else {
		buffer = gst_buffer_new_wrapped_full(0, frame->data[0], data->buffer_size, 0, data->buffer_size, NULL, NULL);
	}
	GST_BUFFER_PTS(buffer) = frame->pts * (GST_SECOND / (data->ovi.fps_num / data->ovi.fps_den));

	gst_app_src_push_buffer(GST_APP_SRC(data->appsrc), buffer);

	data->sample = gst_app_sink_try_pull_sample(GST_APP_SINK(data->appsink), 0);
	if (data->sample == NULL) return true;

	*received_packet = true;

	buffer = gst_sample_get_buffer(data->sample);

	gst_buffer_map(buffer, &data->info, GST_MAP_READ);

	if (data->codec_data == NULL) {
		size_t size;

		// this is pretty lazy..
		for (size = 0; size < data->info.size; size++) {
			if (data->info.data[size + 0] == 0 &&
			    data->info.data[size + 1] == 0 &&
			    data->info.data[size + 2] == 0 &&
			    data->info.data[size + 3] == 1 &&
			    (data->info.data[size + 4] & 0x1f) == 5) {
				break;
			}
		}

		data->codec_data = g_malloc(size);
		memcpy(data->codec_data, data->info.data, size);

		data->codec_data_size = size;
	}

	packet->data = data->info.data;
	packet->size = data->info.size;

	packet->pts = GST_BUFFER_PTS(buffer);
	packet->dts = GST_BUFFER_DTS(buffer);

	// this is a bit wonky?
	packet->pts /= GST_SECOND / (packet->timebase_den / packet->timebase_num);
	packet->dts /= GST_SECOND / (packet->timebase_den / packet->timebase_num);

	packet->type = OBS_ENCODER_VIDEO;

	packet->keyframe = !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);

	return true;
}

void gstreamer_encoder_get_defaults_h264(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "device", "/dev/dri/renderD128");
	obs_data_set_default_string(settings, "encoder_type", "x264");
	obs_data_set_default_int(settings, "bitrate", 2500);
	obs_data_set_default_string(settings, "rate_control", "CBR");
	obs_data_set_default_int(settings, "keyint_sec", 2);
	obs_data_set_default_bool(settings, "force_copy", false);
	obs_data_set_default_string(settings, "encoder_string", "x264enc");
}

void gstreamer_encoder_get_defaults_h265(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "device", "/dev/dri/renderD128");
	obs_data_set_default_string(settings, "encoder_type", "vaapih265enc");
	obs_data_set_default_int(settings, "bitrate", 2500);
	obs_data_set_default_string(settings, "rate_control", "CBR");
	obs_data_set_default_int(settings, "keyint_sec", 2);
	obs_data_set_default_bool(settings, "force_copy", false);
}

static bool check_feature(char *name)
{
	GstRegistry *registry = gst_registry_get();
	GstPluginFeature *feature = gst_registry_lookup_feature(registry, name);

	if (feature) {
		gst_object_unref(feature);
		return true;
	}

	return false;
}

#ifdef __linux__
static int scanfilter(const struct dirent *entry)
{
	return !astrcmp_n(entry->d_name, "renderD", 7);
}

static void populate_vaapi_devices(obs_property_t *prop)
{
	struct dirent **list;
	int n = scandir("/dev/dri", &list, scanfilter, versionsort);

	for (int i = 0; i < n; i++) {
		char device[16 + NAME_MAX] = {0};
		int w = snprintf(device, sizeof(device), "/dev/dri/%s",
				 list[i]->d_name);
		(void)w;
		obs_property_list_add_string(prop, device, device);
	}

	while (n--)
		free(list[n]);
	free(list);
}
#endif

static bool encoder_modified(obs_properties_t *props, obs_property_t *property,
			     obs_data_t *settings)
{
	obs_property_t *device = obs_properties_get(props, "device");

	if (g_strcmp0(obs_data_get_string(settings, "encoder_type"),
		      "vaapih264enc") == 0 ||
	    g_strcmp0(obs_data_get_string(settings, "encoder_type"),
		      "vaapih265enc") == 0)
		obs_property_set_visible(device, true);
	else
		obs_property_set_visible(device, false);

	return true;
}

obs_properties_t *gstreamer_encoder_get_properties_h264(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *prop = obs_properties_add_list(props, "encoder_type",
						       "Encoder type",
						       OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback(prop, encoder_modified);

	if (check_feature("x264enc"))
		obs_property_list_add_string(prop, "x264", "x264");
	if (check_feature("nvh264enc"))
		obs_property_list_add_string(prop, "NVIDIA (NVENC)",
					     "nvh264enc");
	if (check_feature("vaapih264enc"))
		obs_property_list_add_string(prop, "VA-API", "vaapih264enc");
	if (check_feature("omxh264enc"))
		obs_property_list_add_string(prop, "OpenMAX (Raspberry Pi)",
					     "omxh264enc");
	if (check_feature("omxh264enc"))
		obs_property_list_add_string(prop, "OpenMAX (Tegra)",
					     "omxh264enc_old");
	if (check_feature("vtenc_h264"))
		obs_property_list_add_string(prop, "Apple (VideoToolBox)",
					     "vtenc_h264");
	if (check_feature("msdkh264enc"))
		obs_property_list_add_string(prop, "Intel MSDK H264 encoder",
					     "msdkh264enc");
	if (check_feature("mpph264enc"))
		obs_property_list_add_string(prop, "Rockchip MPP H264 encoder",
					     "mpph264enc");

	prop = obs_properties_add_list(props, "device", "Device",
				       OBS_COMBO_TYPE_LIST,
				       OBS_COMBO_FORMAT_STRING);

	obs_property_set_long_description(prop, "For VAAPI only");

#ifdef __linux__
	populate_vaapi_devices(prop);
#endif

	prop = obs_properties_add_int(props, "bitrate", "Bitrate", 50, 10000000,
				      50);
	//	obs_property_int_set_suffix(prop, " Kbps");

	prop = obs_properties_add_list(props, "rate_control", "Rate control",
				       OBS_COMBO_TYPE_LIST,
				       OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(prop, "Constant bitrate", "CBR");
	obs_property_list_add_string(prop, "Variable bitrate", "VBR");
	obs_property_list_add_string(prop, "Constant QP", "CQP");
	obs_property_list_add_string(prop, "Constant QP - Intelligent", "ICQ");
	obs_property_list_add_string(prop, "Variable bitrate - Quality defined",
				     "QVBR");

	prop = obs_properties_add_int(props, "keyint_sec", "Keyframe interval",
				      0, 20, 1);
	//	obs_property_int_set_suffix(prop, " seconds");

	prop = obs_properties_add_text(props, "extra_options",
				       "Extra encoder options",
				       OBS_TEXT_MULTILINE);


	obs_property_set_long_description(
		prop,
		"Extra encoder options. Use the form of key=value separated by spaces.");

	prop = obs_properties_add_text(props, "encoder_string", "Encoder string", OBS_TEXT_MULTILINE);

	obs_properties_add_bool(props, "force_copy", "Force memory copy");

	return props;
}

obs_properties_t *gstreamer_encoder_get_properties_h265(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *prop = obs_properties_add_list(props, "encoder_type",
						       "Encoder type",
						       OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);

	if (check_feature("vaapih265enc"))
		obs_property_list_add_string(prop, "VA-API", "vaapih265enc");
	if (check_feature("nvh265enc"))
		obs_property_list_add_string(prop, "NVIDIA (NVENC)",
					     "nvh265enc");
	if (check_feature("msdkh265enc"))
		obs_property_list_add_string(prop, "Intel MSDK H265 encoder",
					     "msdkh265enc");
	if (check_feature("mpph265enc"))
		obs_property_list_add_string(prop, "Rockchip MPP H265 encoder",
					     "mpph265enc");
	prop = obs_properties_add_list(props, "device", "Device",
				       OBS_COMBO_TYPE_LIST,
				       OBS_COMBO_FORMAT_STRING);

	obs_property_set_long_description(prop, "For VAAPI only");

#ifdef __linux__
	populate_vaapi_devices(prop);
#endif

	prop = obs_properties_add_int(props, "bitrate", "Bitrate", 50, 10000000,
				      50);
	//	obs_property_int_set_suffix(prop, " Kbps");

	prop = obs_properties_add_list(props, "rate_control", "Rate control",
				       OBS_COMBO_TYPE_LIST,
				       OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(prop, "Constant bitrate", "CBR");
	obs_property_list_add_string(prop, "Variable bitrate", "VBR");
	obs_property_list_add_string(prop, "Constant QP", "CQP");
	obs_property_list_add_string(prop, "Constant QP - Intelligent", "ICQ");
	obs_property_list_add_string(prop, "Variable bitrate - Quality defined",
				     "QVBR");

	prop = obs_properties_add_int(props, "keyint_sec", "Keyframe interval",
				      0, 20, 1);
	//	obs_property_int_set_suffix(prop, " seconds");

	prop = obs_properties_add_text(props, "extra_options",
				       "Extra encoder options",
				       OBS_TEXT_MULTILINE);
	obs_property_set_long_description(
		prop,
		"Extra encoder options. Use the form of key=value separated by spaces.");

	obs_properties_add_bool(props, "force_copy", "Force memory copy");

	return props;
}

bool gstreamer_encoder_get_extra_data(void *p, uint8_t **extra_data,
				      size_t *size)
{
	data_t *data = (data_t *)p;

	if (data->codec_data == NULL)
		return false;

	*extra_data = data->codec_data;
	*size = data->codec_data_size;

	return true;
}
#pragma warning(default : 4018)