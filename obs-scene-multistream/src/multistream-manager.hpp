#pragma once

#include "destination.hpp"
#include <obs.h>
#include <obs-frontend-api.h>

#include <map>
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <chrono>

struct RuntimeOutput {
	DestinationConfig config;
	obs_view_t *view = nullptr;
	video_t *video = nullptr;
	obs_encoder_t *venc = nullptr;
	obs_encoder_t *aenc = nullptr;
	obs_output_t *output = nullptr;
	obs_service_t *service = nullptr;
	bool active = false;
	bool user_stopped = false; /* set when user explicitly stops — suppress reconnect */
	std::string last_error;

	/* reconnect state */
	int reconnect_attempts = 0;
	static constexpr int MAX_RECONNECT = 5;
	static constexpr int BASE_DELAY_MS = 1000;
};

class MultistreamManager {
public:
	using StatusCallback = std::function<void(const std::string &name, bool active, const std::string &error)>;

	static MultistreamManager &instance();

	bool start(const DestinationConfig &cfg, std::string &error_out);
	bool stop(const std::string &name);
	void stop_all();

	bool is_active(const std::string &name) const;
	std::vector<std::string> active_names() const;

	void set_status_callback(StatusCallback cb);

	/* v0.3: hot-swap scene on running output (no stop/start) */
	bool set_scene_for(const std::string &dest_name, const std::string &scene_name, bool follow_obs_scene);

	/* v0.3: called on OBS frontend SCENE_CHANGED — propagates new program scene
	   to all active outputs with follow_obs_scene=true */
	void on_program_scene_changed();

	/* v0.3: registered once in obs_module_load */
	void register_frontend_callback();
	void unregister_frontend_callback();

private:
	MultistreamManager() = default;
	~MultistreamManager();
	MultistreamManager(const MultistreamManager &) = delete;
	MultistreamManager &operator=(const MultistreamManager &) = delete;

	void cleanup_runtime(RuntimeOutput &rt);

	static void on_output_start(void *data, calldata_t *cd);
	static void on_output_stop(void *data, calldata_t *cd);
	static void on_frontend_event(enum obs_frontend_event evt, void *data);

	mutable std::mutex mtx_;
	std::map<std::string, RuntimeOutput> outputs_;
	StatusCallback status_cb_;
};
