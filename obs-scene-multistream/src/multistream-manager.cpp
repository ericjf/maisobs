#include "multistream-manager.hpp"
#include "plugin-support.h"

#include <obs.h>
#include <obs-module.h>

#include <utility>

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

	obs_source_t *scene = obs_get_source_by_name(cfg.scene_name.c_str());
	if (!scene) {
		error_out = "scene not found: " + cfg.scene_name;
		return false;
	}

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
	ovi.base_width = cfg.width;
	ovi.base_height = cfg.height;
	ovi.output_width = cfg.width;
	ovi.output_height = cfg.height;
	ovi.fps_num = cfg.fps_num;
	ovi.fps_den = cfg.fps_den;

	rt.video = obs_view_add2(rt.view, &ovi);
	if (!rt.video) {
		error_out = "obs_view_add2 failed";
		cleanup_runtime(rt);
		return false;
	}

	obs_data_t *venc_settings = obs_data_create();
	obs_data_set_int(venc_settings, "bitrate", cfg.video_bitrate_kbps);
	obs_data_set_string(venc_settings, "rate_control", "CBR");
	obs_data_set_int(venc_settings, "keyint_sec", 2);
	const std::string venc_name = "scenemulti_venc_" + cfg.name;
	rt.venc = obs_video_encoder_create(cfg.video_encoder.c_str(), venc_name.c_str(), venc_settings, nullptr);
	obs_data_release(venc_settings);
	if (!rt.venc) {
		error_out = "failed to create video encoder: " + cfg.video_encoder;
		cleanup_runtime(rt);
		return false;
	}
	obs_encoder_set_video(rt.venc, rt.video);

	obs_data_t *aenc_settings = obs_data_create();
	obs_data_set_int(aenc_settings, "bitrate", cfg.audio_bitrate_kbps);
	const std::string aenc_name = "scenemulti_aenc_" + cfg.name;
	rt.aenc = obs_audio_encoder_create(cfg.audio_encoder.c_str(), aenc_name.c_str(), aenc_settings,
					   cfg.audio_track, nullptr);
	obs_data_release(aenc_settings);
	if (!rt.aenc) {
		error_out = "failed to create audio encoder: " + cfg.audio_encoder;
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
	if (code != OBS_OUTPUT_SUCCESS) {
		const char *last = obs_output_get_last_error(out);
		err = last ? last : ("output stopped with code " + std::to_string(code));
	}

	StatusCallback cb;
	{
		std::lock_guard<std::mutex> lock(self->mtx_);
		auto it = self->outputs_.find(dest_name);
		if (it != self->outputs_.end()) {
			it->second.active = false;
			it->second.last_error = err;
		}
		cb = self->status_cb_;
	}
	if (cb)
		cb(dest_name, false, err);
}
