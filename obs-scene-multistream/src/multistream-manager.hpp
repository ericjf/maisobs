#pragma once

#include "destination.hpp"
#include <obs.h>

#include <map>
#include <string>
#include <vector>
#include <mutex>
#include <functional>

struct RuntimeOutput {
	DestinationConfig config;
	obs_view_t *view = nullptr;
	video_t *video = nullptr;
	obs_encoder_t *venc = nullptr;
	obs_encoder_t *aenc = nullptr;
	obs_output_t *output = nullptr;
	obs_service_t *service = nullptr;
	bool active = false;
	std::string last_error;
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

private:
	MultistreamManager() = default;
	~MultistreamManager();
	MultistreamManager(const MultistreamManager &) = delete;
	MultistreamManager &operator=(const MultistreamManager &) = delete;

	void cleanup_runtime(RuntimeOutput &rt);

	static void on_output_start(void *data, calldata_t *cd);
	static void on_output_stop(void *data, calldata_t *cd);

	mutable std::mutex mtx_;
	std::map<std::string, RuntimeOutput> outputs_;
	StatusCallback status_cb_;
};
