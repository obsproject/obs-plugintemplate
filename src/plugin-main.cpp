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
#include<opencv2/opencv.hpp>
#include "rpc/client.h"
struct send_img_data {
	obs_source_t *source;
	gs_texrender_t *texrender;
	gs_stagesurf_t *stagesurface;
	gs_effect_t *kawaseBlurEffect;
	gs_effect_t *maskingEffect;
	gs_effect_t *pixelateEffect;
	cv::Mat inputBGRA;
	std::mutex inputBGRALock;
	std::mutex outputLock;
	std::mutex modelMutex;
};
send_img_data *tf;

rpc::client *client;

bool getRGBAFromStageSurface(send_img_data *tf, uint32_t &width,
			     uint32_t &height)
{

	if (!obs_source_enabled(tf->source)) {
		return false;
	}

	obs_source_t *target = obs_filter_get_target(tf->source);
	if (!target) {
		return false;
	}
	width = obs_source_get_base_width(target);
	height = obs_source_get_base_height(target);
	if (width == 0 || height == 0) {
		return false;
	}
	gs_texrender_reset(tf->texrender);
	if (!gs_texrender_begin(tf->texrender, width, height)) {
		return false;
	}
	
	struct vec4 background;
	vec4_zero(&background);
	gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
	gs_ortho(0.0f, static_cast<float>(width), 0.0f,
		 static_cast<float>(height), -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	obs_source_video_render(target);
	gs_blend_state_pop();
	gs_texrender_end(tf->texrender);

	if (tf->stagesurface) {
		uint32_t stagesurf_width =
			gs_stagesurface_get_width(tf->stagesurface);
		uint32_t stagesurf_height =
			gs_stagesurface_get_height(tf->stagesurface);
		if (stagesurf_width != width || stagesurf_height != height) {
			gs_stagesurface_destroy(tf->stagesurface);
			tf->stagesurface = nullptr;
		}
	}
	if (!tf->stagesurface) {
		tf->stagesurface =
			gs_stagesurface_create(width, height, GS_BGRA);
	}
	gs_stage_texture(tf->stagesurface,
			 gs_texrender_get_texture(tf->texrender));
	uint8_t *video_data;
	uint32_t linesize;
	if (!gs_stagesurface_map(tf->stagesurface, &video_data, &linesize)) {
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(tf->inputBGRALock);
		tf->inputBGRA =
			cv::Mat(height, width, CV_8UC4, video_data, linesize);
		std::vector<uchar> image_buf;
		std::vector<int> params;

		cv::imencode(".jpg", tf->inputBGRA, image_buf);
		auto boxes = client->call("rpc_inference", image_buf)
				     .as<bool>();
	}
	gs_stagesurface_unmap(tf->stagesurface);
	return true;
}


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")
const char *send_img_filter_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	obs_log(LOG_INFO, "plugin loaded successfully send_img_filter_getname");
	return obs_module_text("send_img");
}
void *send_img_filter_create(obs_data_t *settings, obs_source_t *source)
{
	obs_log(LOG_INFO, "Detect filter created send_img_filter_create");
	tf->source = source;
	tf->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	return tf;
}
obs_properties_t *send_img_filter_properties(void *data)
{
	obs_log(LOG_INFO, "Detect filter created send_img_filter_properties");
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_bool(props, "test", obs_module_text("test"));
	obs_properties_add_float_slider(
		props, 
		"test_float_slider",	
		obs_module_text("test_float_slider"),
		1,10,1
	);
	
	return props;
};
void send_img_filter_defaults(obs_data_t *settings){
	obs_data_set_default_bool(settings, "test", true);
	obs_data_set_default_double(settings, "test_float_slider", 5.0);

};
obs_source_t *target;
void detect_filter_video_tick(void *data, float seconds)
{
	obs_log(LOG_INFO, "tick_1");
	if (!obs_source_enabled(tf->source)) {
		return ;
	}
	obs_log(LOG_INFO, "tick_2");
	target = obs_filter_get_target(tf->source);
	if (!target) {
		return ;
	}
	obs_log(LOG_INFO, "tick_3");
	auto width = obs_source_get_base_width(target);
	auto height = obs_source_get_base_height(target);
	if (width == 0 || height == 0) {
		return ;
	}
	obs_log(LOG_INFO, "tick_4");
	obs_log(LOG_INFO, "%x", tf->texrender);
	gs_texrender_reset(tf->texrender);
	obs_log(LOG_INFO, "tick_4_1");
	obs_log(LOG_INFO, "%x", tf->texrender);
	if (!gs_texrender_begin(tf->texrender, width, height)) {
		return ;
	}
	obs_log(LOG_INFO, "%x", tf->texrender);
	obs_log(LOG_INFO, "tick_5");
	struct vec4 background;
	vec4_zero(&background);
	gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
	gs_ortho(0.0f, static_cast<float>(width), 0.0f,
		 static_cast<float>(height), -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	obs_source_video_render(target);
	gs_blend_state_pop();
	gs_texrender_end(tf->texrender);

	if (tf->stagesurface) {
		uint32_t stagesurf_width =
			gs_stagesurface_get_width(tf->stagesurface);
		uint32_t stagesurf_height =
			gs_stagesurface_get_height(tf->stagesurface);
		if (stagesurf_width != width || stagesurf_height != height) {
			gs_stagesurface_destroy(tf->stagesurface);
			tf->stagesurface = nullptr;
		}
	}
	if (!tf->stagesurface) {
		tf->stagesurface =
			gs_stagesurface_create(width, height, GS_BGRA);
	}
	gs_stage_texture(tf->stagesurface,
			 gs_texrender_get_texture(tf->texrender));
	uint8_t *video_data;
	uint32_t linesize;
	obs_log(LOG_INFO, "tick_6");
	if (!gs_stagesurface_map(tf->stagesurface, &video_data, &linesize)) {
		return ;
	}
	{
		std::lock_guard<std::mutex> lock(tf->inputBGRALock);
		tf->inputBGRA =
			//cv::Mat(height, width, CV_8UC4, video_data, linesize);


			cv::Mat(height, width, CV_8UC4, video_data, linesize)(cv::Rect(960, 540, 320, 320));

		obs_log(LOG_INFO, "inputBGRALock");
		
	}
	obs_log(LOG_INFO, "tick_7");
	gs_stagesurface_unmap(tf->stagesurface);
	return ;
	obs_log(LOG_INFO, "tick_8");
	obs_log(LOG_INFO, "%f" ,seconds);
	return;
};
void send_img_filter_destroy(void *data)
{
	

};
void send_img_filter_video_render(void *data, gs_effect_t *_effect)
{
	uint32_t w, h;
	getRGBAFromStageSurface(tf, w, h);
	obs_source_skip_video_filter(tf->source);
	
};

struct obs_source_info send_img_filter_info;



bool obs_module_load(void)
{
	
	client = new rpc::client("192.168.241.144", 15321);
	send_img_filter_info.id = "send_img-filter";
	send_img_filter_info.type = OBS_SOURCE_TYPE_FILTER;
	send_img_filter_info.output_flags = OBS_SOURCE_VIDEO;
	send_img_filter_info.get_name = send_img_filter_getname;
	send_img_filter_info.create = send_img_filter_create;
	send_img_filter_info.destroy = send_img_filter_destroy;
	send_img_filter_info.get_properties = send_img_filter_properties;
	send_img_filter_info.get_defaults = send_img_filter_defaults;
	//send_img_filter_info.video_tick = detect_filter_video_tick;
	send_img_filter_info.video_render = send_img_filter_video_render;
	tf = new send_img_data();
	
	obs_register_source(&send_img_filter_info);
	
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
