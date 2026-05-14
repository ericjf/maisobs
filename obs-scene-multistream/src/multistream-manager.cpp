#include "multistream-manager.hpp"
#include "plugin-support.h"

#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <utility>
#include <thread>
#include <chrono>
#include <string>

/* v0.3 — Resolve "follow OBS" output settings from active profile.
   Supports OBS Simple Output Mode primarily. Advanced Output not fully
   wired (would require parsing streamEncoder.json — TODO future). */
struct ResolvedOutput {
	std::string video_encoder_id;
	std::string audio_encoder_id;
	int video_bitrate_kbps = 6000;
	int audio_bitrate_kbps = 160;
	uint32_t width = 1920;
	uint32_t height = 1080;
	uint32_t fps_num = 30;
	uint32_t fps_den = 1;
};

/* Map OBS Simple Output StreamEncoder values to obs encoder IDs. */
static std::string simple_encoder_to_id(const char *simple_name)
{
	if (!simple_name)
		return "obs_x264";
	std::string s = simple_name;
	if (s == "x264" || s == "obs_x264")
		return "obs_x264";
	if (s == "nvenc" || s == "jim_nvenc")
		return "jim_nvenc";
	if (s == "nvenc_hevc" || s == "jim_hevc_nvenc")
		return "jim_hevc_nvenc";
	if (s == "amd" || s == "h264_texture_amf")
		return "h264_texture_amf";
	if (s == "qsv" || s == "obs_qsv11_v2")
		return "obs_qsv11_v2";
	if (s == "apple_h264" || s == "com.apple.videotoolbox.videoencoder.ave.avc")
		return "com.apple.videotoolbox.videoencoder.ave.avc";
	return s; /* assume already an ID */
}

static ResolvedOutput resolve_output_settings(const DestinationConfig &cfg)
{
	ResolvedOutput r;
	struct obs_video_info ovi = {};
	obs_get_video_info(&ovi);
	r.width = ovi.base_width;
	r.height = ovi.base_height;
	r.fps_num = ovi.fps_num;
	r.fps_den = ovi.fps_den;

	if (cfg.follow_obs_video) {
		config_t *prof = obs_frontend_get_profile_config();
		if (prof) {
			const char *mode = config_get_string(prof, "Output", "Mode");
			bool advanced = mode && std::string(mode) == "Advanced";
			if (advanced) {
				const char *enc = config_get_string(prof, "AdvOut", "Encoder");
				r.video_encoder_id = enc ? enc : "obs_x264";
				/* AdvOut bitrate lives in streamEncoder.json — fall back to
				   reasonable default. Future: read streamEncoder.json from
				   profile dir. */
				r.video_bitrate_kbps = 6000;
				const char *aenc = config_get_string(prof, "AdvOut", "AudioEncoder");
				r.audio_encoder_id = aenc ? aenc : "ffmpeg_aac";
				r.audio_bitrate_kbps = (int)config_get_int(prof, "AdvOut", "Track1Bitrate");
				if (r.audio_bitrate_kbps == 0)
					r.audio_bitrate_kbps = 160;
			} else {
				const char *se = config_get_string(prof, "SimpleOutput", "StreamEncoder");
				r.video_encoder_id = simple_encoder_to_id(se);
				r.video_bitrate_kbps = (int)config_get_int(prof, "SimpleOutput", "VBitrate");
				if (r.video_bitrate_kbps == 0)
					r.video_bitrate_kbps = 6000;
				r.audio_encoder_id = "ffmpeg_aac";
				r.audio_bitrate_kbps = (int)config_get_int(prof, "SimpleOutput", "ABitrate");
				if (r.audio_bitrate_kbps == 0)
					r.audio_bitrate_kbps = 160;
			}
		} else {
			r.video_encoder_id = "obs_x264";
			r.audio_encoder_id = "ffmpeg_aac";
		}
	} else {
		r.video_encoder_id = cfg.video_encoder;
		r.audio_encoder_id = cfg.audio_encoder;
		r.video_bitrate_kbps = cfg.video_bitrate_kbps;
		r.audio_bitrate_kbps = cfg.audio_bitrate_kbps;
		r.width = cfg.width;
		r.height = cfg.height;
		r.fps_num = cfg.fps_num;
		r.fps_den = cfg.fps_den;
	}
	return r;
}

MultistreamManager &MultistreamManager::instance()
{
	static MultistreamManager inst;
	return inst;
}

MultistreamManager::~MultistreamManager()
{
	stop_all();
}

void MultistreamManager::set_status_callback(StatusCallback cb)
{
	std::lock_guard<std::mutex> lock(mtx_);
	status_cb_ = std::move(cb);
}

bool MultistreamManager::is_active(const std::string &name) const
{
	std::lock_guard<std::mutex> lock(mtx_);
	auto it = outputs_.find(name);
	return it != outputs_.end() && it->second.active;
}

std::vector<std::string> MultistreamManager::active_names() const
{
	std::lock_guard<std::mutex> lock(mtx_);
	std::vector<std::string> names;
	for (auto &kv : outputs_) {
		if (kv.second.active)
			names.push_back(kv.first);
	}
	return names;
}

bool MultistreamManager::start(const DestinationConfig &cfg, std::string &error_out)
{
	std::lock_guard<std::mutex> lock(mtx_);

	if (outputs_.count(cfg.name) && outputs_[cfg.name].active) {
		error_out = "destination already active";
		return false;
	}

	if (cfg.rtmp_url.empty()) {
		error_out = "rtmp url empty";
		return false;
	}

	obs_source_t *scene = nullptr;
	if (cfg.follow_obs_scene) {
		scene = obs_frontend_get_current_scene(); /* returns addref'd ref */
		if (!scene) {
			error_out = "no active OBS scene";
			return false;
		}
	} else {
		scene = obs_get_source_by_name(cfg.scene_name.c_str());
		if (!scene) {
			error_out = "scene not found: " + cfg.scene_name;
			return false;
		}
	}

	ResolvedOutput out = resolve_output_settings(cfg);

	RuntimeOutput rt;
	rt.config = cfg;

	rt.view = obs_view_create();
	obs_view_set_source(rt.view, 0, scene);
	obs_source_release(scene);

	struct obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi)) {
		error_out = "failed to get base video info";
		cleanup_runtime(rt);
		return false;
	}
	ovi.base_width = out.width;
	ovi.base_height = out.height;
	ovi.output_width = out.width;
	ovi.output_height = out.height;
	ovi.fps_num = out.fps_num;
	ovi.fps_den = out.fps_den;

	rt.video = obs_view_add2(rt.view, &ovi);
	if (!rt.video) {
		error_out = "obs_view_add2 failed";
		cleanup_runtime(rt);
		return false;
	}

	obs_data_t *venc_settings = obs_data_create();
	obs_data_set_int(venc_settings, "bitrate", out.video_bitrate_kbps);
	obs_data_set_string(venc_settings, "rate_control", "CBR");
	obs_data_set_int(venc_settings, "keyint_sec", 2);
	const std::string venc_name = "scenemulti_venc_" + cfg.name;
	rt.venc = obs_video_encoder_create(out.video_encoder_id.c_str(), venc_name.c_str(), venc_settings, nullptr);
	obs_data_release(venc_settings);
	if (!rt.venc) {
		error_out = "failed to create video encoder: " + out.video_encoder_id;
		cleanup_runtime(rt);
		return false;
	}
	obs_encoder_set_video(rt.venc, rt.video);

	obs_data_t *aenc_settings = obs_data_create();
	obs_data_set_int(aenc_settings, "bitrate", out.audio_bitrate_kbps);
	const std::string aenc_name = "scenemulti_aenc_" + cfg.name;
	rt.aenc = obs_audio_encoder_create(out.audio_encoder_id.c_str(), aenc_name.c_str(), aenc_settings,
					   cfg.audio_track, nullptr);
	obs_data_release(aenc_settings);
	if (!rt.aenc) {
		error_out = "failed to create audio encoder: " + out.audio_encoder_id;
		cleanup_runtime(rt);
		return false;
	}
	obs_encoder_set_audio(rt.aenc, obs_get_audio());

	obs_data_t *svc_settings = obs_data_create();
	obs_data_set_string(svc_settings, "server", cfg.rtmp_url.c_str());
	obs_data_set_string(svc_settings, "key", cfg.stream_key.c_str());
	const std::string svc_name = "scenemulti_svc_" + cfg.name;
	rt.service = obs_service_create("rtmp_custom", svc_name.c_str(), svc_settings, nullptr);
	obs_data_release(svc_settings);
	if (!rt.service) {
		error_out = "failed to create service";
		cleanup_runtime(rt);
		return false;
	}

	const std::string out_name = "scenemulti_out_" + cfg.name;
	rt.output = obs_output_create("rtmp_output", out_name.c_str(), nullptr, nullptr);
	if (!rt.output) {
		error_out = "failed to create rtmp_output";
		cleanup_runtime(rt);
		return false;
	}
	obs_output_set_service(rt.output, rt.service);
	obs_output_set_video_encoder(rt.output, rt.venc);
	obs_output_set_audio_encoder(rt.output, rt.aenc, 0);

	signal_handler_t *sh = obs_output_get_signal_handler(rt.output);
	signal_handler_connect(sh, "start", on_output_start, this);
	signal_handler_connect(sh, "stop", on_output_stop, this);

	if (!obs_output_start(rt.output)) {
		const char *err = obs_output_get_last_error(rt.output);
		error_out = err ? err : "obs_output_start failed";
		cleanup_runtime(rt);
		return false;
	}

	rt.active = true;
	outputs_[cfg.name] = std::move(rt);
	obs_log(LOG_INFO, "[scene-multistream] started '%s' -> %s", cfg.name.c_str(), cfg.rtmp_url.c_str());
	return true;
}

bool MultistreamManager::stop(const std::string &name)
{
	std::lock_guard<std::mutex> lock(mtx_);
	auto it = outputs_.find(name);
	if (it == outputs_.end())
		return false;

	RuntimeOutput &rt = it->second;
	rt.user_stopped = true; /* suppress reconnect */
	if (rt.output && obs_output_active(rt.output)) {
		obs_output_stop(rt.output);
	}
	cleanup_runtime(rt);
	outputs_.erase(it);
	obs_log(LOG_INFO, "[scene-multistream] stopped '%s'", name.c_str());
	return true;
}

void MultistreamManager::stop_all()
{
	std::vector<std::string> names;
	{
		std::lock_guard<std::mutex> lock(mtx_);
		for (auto &kv : outputs_)
			names.push_back(kv.first);
	}
	for (auto &n : names)
		stop(n);
}

void MultistreamManager::cleanup_runtime(RuntimeOutput &rt)
{
	if (rt.output) {
		signal_handler_t *sh = obs_output_get_signal_handler(rt.output);
		if (sh) {
			signal_handler_disconnect(sh, "start", on_output_start, this);
			signal_handler_disconnect(sh, "stop", on_output_stop, this);
		}
		obs_output_release(rt.output);
		rt.output = nullptr;
	}
	if (rt.service) {
		obs_service_release(rt.service);
		rt.service = nullptr;
	}
	if (rt.venc) {
		obs_encoder_release(rt.venc);
		rt.venc = nullptr;
	}
	if (rt.aenc) {
		obs_encoder_release(rt.aenc);
		rt.aenc = nullptr;
	}
	if (rt.view) {
		obs_view_remove(rt.view);
		obs_view_destroy(rt.view);
		rt.view = nullptr;
		rt.video = nullptr;
	}
	rt.active = false;
}

void MultistreamManager::on_output_start(void *data, calldata_t *cd)
{
	auto *self = static_cast<MultistreamManager *>(data);
	obs_output_t *out = nullptr;
	calldata_get_ptr(cd, "output", &out);
	if (!out)
		return;
	const char *out_name = obs_output_get_name(out);
	if (!out_name)
		return;

	std::string dest_name = out_name;
	const std::string prefix = "scenemulti_out_";
	if (dest_name.rfind(prefix, 0) == 0)
		dest_name = dest_name.substr(prefix.size());

	StatusCallback cb;
	{
		std::lock_guard<std::mutex> lock(self->mtx_);
		cb = self->status_cb_;
	}
	if (cb)
		cb(dest_name, true, "");
}

void MultistreamManager::on_output_stop(void *data, calldata_t *cd)
{
	auto *self = static_cast<MultistreamManager *>(data);
	obs_output_t *out = nullptr;
	calldata_get_ptr(cd, "output", &out);
	long long code = 0;
	calldata_get_int(cd, "code", &code);
	if (!out)
		return;
	const char *out_name = obs_output_get_name(out);
	if (!out_name)
		return;

	std::string dest_name = out_name;
	const std::string prefix = "scenemulti_out_";
	if (dest_name.rfind(prefix, 0) == 0)
		dest_name = dest_name.substr(prefix.size());

	std::string err;
	bool should_reconnect = false;
	int attempt = 0;
	int delay_ms = 0;
	DestinationConfig reconnect_cfg;

	if (code != OBS_OUTPUT_SUCCESS) {
		const char *last = obs_output_get_last_error(out);
		err = last ? last : ("stopped with code " + std::to_string(code));
	}

	{
		std::lock_guard<std::mutex> lock(self->mtx_);
		auto it = self->outputs_.find(dest_name);
		if (it != self->outputs_.end()) {
			RuntimeOutput &rt = it->second;
			rt.active = false;
			rt.last_error = err;

			if (code != OBS_OUTPUT_SUCCESS && !rt.user_stopped &&
			    rt.reconnect_attempts < RuntimeOutput::MAX_RECONNECT) {
				should_reconnect = true;
				attempt = ++rt.reconnect_attempts;
				/* exponential backoff: 1s, 2s, 4s, 8s, 16s */
				delay_ms = RuntimeOutput::BASE_DELAY_MS * (1 << (attempt - 1));
				reconnect_cfg = rt.config;
				err = "reconnecting (" + std::to_string(attempt) + "/" +
				      std::to_string(RuntimeOutput::MAX_RECONNECT) + "): " + err;
			}
		}
	}

	StatusCallback cb;
	{
		std::lock_guard<std::mutex> lock(self->mtx_);
		cb = self->status_cb_;
	}
	if (cb)
		cb(dest_name, false, err);

	if (should_reconnect) {
		obs_log(LOG_INFO, "[scene-multistream] '%s' reconnect attempt %d/%d in %dms", dest_name.c_str(),
			attempt, RuntimeOutput::MAX_RECONNECT, delay_ms);
		/* Reconnect on a background thread after delay.
		   MultistreamManager lifetime >= OBS session, safe to capture self. */
		std::thread([self, reconnect_cfg, dest_name, delay_ms, attempt]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
			std::string err2;
			/* Only reconnect if not user-stopped in the meantime */
			bool still_pending = false;
			{
				std::lock_guard<std::mutex> lock(self->mtx_);
				auto it = self->outputs_.find(dest_name);
				if (it != self->outputs_.end() && !it->second.user_stopped &&
				    it->second.reconnect_attempts == attempt)
					still_pending = true;
			}
			if (!still_pending)
				return;
			/* tear down existing (dead) runtime before rebuilding */
			{
				std::lock_guard<std::mutex> lock(self->mtx_);
				auto it = self->outputs_.find(dest_name);
				if (it != self->outputs_.end())
					self->cleanup_runtime(it->second);
				self->outputs_.erase(dest_name);
			}
			self->start(reconnect_cfg, err2);
		}).detach();
	}
}

/* v0.3 — hot-swap scene on an already-running output. */
bool MultistreamManager::set_scene_for(const std::string &dest_name, const std::string &scene_name,
				       bool follow_obs_scene)
{
	std::lock_guard<std::mutex> lock(mtx_);
	auto it = outputs_.find(dest_name);
	if (it == outputs_.end())
		return false;
	RuntimeOutput &rt = it->second;

	obs_source_t *scene = follow_obs_scene ? obs_frontend_get_current_scene()
					       : obs_get_source_by_name(scene_name.c_str());
	if (!scene) {
		obs_log(LOG_WARNING, "[scene-multistream] set_scene_for '%s': scene not found", dest_name.c_str());
		return false;
	}

	if (rt.view)
		obs_view_set_source(rt.view, 0, scene);
	obs_source_release(scene);

	rt.config.scene_name = scene_name;
	rt.config.follow_obs_scene = follow_obs_scene;
	obs_log(LOG_INFO, "[scene-multistream] '%s' scene swapped to %s", dest_name.c_str(),
		follow_obs_scene ? "(Follow OBS)" : scene_name.c_str());
	return true;
}

/* v0.3 — called from frontend event callback when OBS program scene changes.
   Propagates the new program scene to all active outputs flagged follow_obs_scene. */
void MultistreamManager::on_program_scene_changed()
{
	obs_source_t *new_scene = obs_frontend_get_current_scene();
	if (!new_scene)
		return;
	std::lock_guard<std::mutex> lock(mtx_);
	for (auto &kv : outputs_) {
		RuntimeOutput &rt = kv.second;
		if (rt.active && rt.config.follow_obs_scene && rt.view)
			obs_view_set_source(rt.view, 0, new_scene);
	}
	obs_source_release(new_scene);
}

void MultistreamManager::on_frontend_event(enum obs_frontend_event evt, void *data)
{
	if (evt != OBS_FRONTEND_EVENT_SCENE_CHANGED)
		return;
	auto *self = static_cast<MultistreamManager *>(data);
	if (self)
		self->on_program_scene_changed();
}

void MultistreamManager::register_frontend_callback()
{
	obs_frontend_add_event_callback(&MultistreamManager::on_frontend_event, this);
}

void MultistreamManager::unregister_frontend_callback()
{
	obs_frontend_remove_event_callback(&MultistreamManager::on_frontend_event, this);
}
