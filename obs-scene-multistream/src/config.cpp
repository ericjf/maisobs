#include "config.hpp"
#include "plugin-support.h"

#include <obs-module.h>
#include <util/platform.h>

#include <filesystem>
#include <vector>

/* ── DPAPI stream-key encryption (Windows only) ──────────────────────── */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

static std::string dpapi_encrypt(const std::string &plain)
{
	if (plain.empty())
		return {};
	DATA_BLOB in_blob{(DWORD)plain.size(), (BYTE *)plain.c_str()};
	DATA_BLOB out_blob{};
	if (!CryptProtectData(&in_blob, L"obs-scene-multistream", nullptr, nullptr, nullptr,
			      0, &out_blob)) {
		obs_log(LOG_WARNING, "[scene-multistream] DPAPI encrypt failed (0x%lx) — storing plain",
			(unsigned long)GetLastError());
		return plain;
	}
	/* encode to base64 for JSON storage */
	DWORD b64_len = 0;
	CryptBinaryToStringA(out_blob.pbData, out_blob.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
			     nullptr, &b64_len);
	std::string b64(b64_len, '\0');
	CryptBinaryToStringA(out_blob.pbData, out_blob.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
			     b64.data(), &b64_len);
	/* b64_len includes null terminator — trim it */
	if (!b64.empty() && b64.back() == '\0')
		b64.pop_back();
	LocalFree(out_blob.pbData);
	return b64;
}

static std::string dpapi_decrypt(const std::string &b64)
{
	if (b64.empty())
		return {};
	DWORD bin_len = 0;
	if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, nullptr, &bin_len,
				  nullptr, nullptr)) {
		/* not base64 — probably plain text from old version, return as-is */
		return b64;
	}
	std::vector<BYTE> bin(bin_len);
	CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, bin.data(), &bin_len,
			     nullptr, nullptr);
	DATA_BLOB in_blob{bin_len, bin.data()};
	DATA_BLOB out_blob{};
	if (!CryptProtectData(&in_blob, nullptr, nullptr, nullptr, nullptr, 0,
			      &out_blob)) {
		obs_log(LOG_WARNING, "[scene-multistream] DPAPI decrypt failed (0x%lx) — returning raw",
			(unsigned long)GetLastError());
		return b64;
	}
	std::string plain((char *)out_blob.pbData, out_blob.cbData);
	LocalFree(out_blob.pbData);
	return plain;
}
#else
/* macOS / Linux: no encryption yet — store plain, warn on first use */
static bool warned_plain = false;
static std::string dpapi_encrypt(const std::string &plain)
{
	if (!warned_plain) {
		obs_log(LOG_WARNING,
			"[scene-multistream] stream key encryption not supported on this platform — stored plain");
		warned_plain = true;
	}
	return plain;
}
static std::string dpapi_decrypt(const std::string &stored)
{
	return stored;
}
#endif
/* ─────────────────────────────────────────────────────────────────────── */

namespace scenemulti {

std::string config_file_path()
{
	char *path = obs_module_config_path("destinations.json");
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

static DestinationConfig from_obs_data(obs_data_t *d)
{
	DestinationConfig c;
	c.name = obs_data_get_string(d, "name");
	c.scene_name = obs_data_get_string(d, "scene_name");
	c.rtmp_url = obs_data_get_string(d, "rtmp_url");
	c.stream_key = dpapi_decrypt(obs_data_get_string(d, "stream_key"));
	const char *venc = obs_data_get_string(d, "video_encoder");
	if (venc && *venc)
		c.video_encoder = venc;
	const char *aenc = obs_data_get_string(d, "audio_encoder");
	if (aenc && *aenc)
		c.audio_encoder = aenc;
	if (obs_data_has_user_value(d, "width"))
		c.width = (uint32_t)obs_data_get_int(d, "width");
	if (obs_data_has_user_value(d, "height"))
		c.height = (uint32_t)obs_data_get_int(d, "height");
	if (obs_data_has_user_value(d, "fps_num"))
		c.fps_num = (uint32_t)obs_data_get_int(d, "fps_num");
	if (obs_data_has_user_value(d, "fps_den"))
		c.fps_den = (uint32_t)obs_data_get_int(d, "fps_den");
	if (obs_data_has_user_value(d, "video_bitrate_kbps"))
		c.video_bitrate_kbps = (int)obs_data_get_int(d, "video_bitrate_kbps");
	if (obs_data_has_user_value(d, "audio_bitrate_kbps"))
		c.audio_bitrate_kbps = (int)obs_data_get_int(d, "audio_bitrate_kbps");
	if (obs_data_has_user_value(d, "audio_track"))
		c.audio_track = (uint32_t)obs_data_get_int(d, "audio_track");
	if (obs_data_has_user_value(d, "enabled"))
		c.enabled = obs_data_get_bool(d, "enabled");
	return c;
}

static obs_data_t *to_obs_data(const DestinationConfig &c)
{
	obs_data_t *d = obs_data_create();
	obs_data_set_string(d, "name", c.name.c_str());
	obs_data_set_string(d, "scene_name", c.scene_name.c_str());
	obs_data_set_string(d, "rtmp_url", c.rtmp_url.c_str());
	obs_data_set_string(d, "stream_key", dpapi_encrypt(c.stream_key).c_str());
	obs_data_set_string(d, "video_encoder", c.video_encoder.c_str());
	obs_data_set_string(d, "audio_encoder", c.audio_encoder.c_str());
	obs_data_set_int(d, "width", c.width);
	obs_data_set_int(d, "height", c.height);
	obs_data_set_int(d, "fps_num", c.fps_num);
	obs_data_set_int(d, "fps_den", c.fps_den);
	obs_data_set_int(d, "video_bitrate_kbps", c.video_bitrate_kbps);
	obs_data_set_int(d, "audio_bitrate_kbps", c.audio_bitrate_kbps);
	obs_data_set_int(d, "audio_track", c.audio_track);
	obs_data_set_bool(d, "enabled", c.enabled);
	return d;
}

std::vector<DestinationConfig> load_destinations()
{
	std::vector<DestinationConfig> result;
	std::string path = config_file_path();
	if (path.empty() || !os_file_exists(path.c_str()))
		return result;

	obs_data_t *root = obs_data_create_from_json_file(path.c_str());
	if (!root) {
		obs_log(LOG_WARNING, "[scene-multistream] failed to parse %s", path.c_str());
		return result;
	}
	obs_data_array_t *arr = obs_data_get_array(root, "destinations");
	if (arr) {
		size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(arr, i);
			if (item) {
				result.push_back(from_obs_data(item));
				obs_data_release(item);
			}
		}
		obs_data_array_release(arr);
	}
	obs_data_release(root);
	return result;
}

bool save_destinations(const std::vector<DestinationConfig> &dests)
{
	std::string path = config_file_path();
	if (path.empty())
		return false;

	std::filesystem::path p(path);
	std::error_code ec;
	std::filesystem::create_directories(p.parent_path(), ec);

	obs_data_t *root = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();
	for (const auto &c : dests) {
		obs_data_t *item = to_obs_data(c);
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}
	obs_data_set_array(root, "destinations", arr);
	obs_data_array_release(arr);

	bool ok = obs_data_save_json(root, path.c_str());
	obs_data_release(root);
	if (!ok)
		obs_log(LOG_WARNING, "[scene-multistream] failed to save %s", path.c_str());
	return ok;
}

} // namespace scenemulti
