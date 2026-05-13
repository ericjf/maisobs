#pragma once

#include <string>
#include <cstdint>

struct DestinationConfig {
	std::string name;
	std::string scene_name;
	std::string rtmp_url;
	std::string stream_key;
	std::string video_encoder = "obs_x264";
	std::string audio_encoder = "ffmpeg_aac";
	uint32_t width = 1920;
	uint32_t height = 1080;
	uint32_t fps_num = 30;
	uint32_t fps_den = 1;
	int video_bitrate_kbps = 6000;
	int audio_bitrate_kbps = 160;
	uint32_t audio_track = 0;
	bool enabled = true;
};
