#include "token-store.hpp"
#include "../plugin-support.h"

#include <obs-module.h>
#include <util/platform.h>

#include <filesystem>
#include <fstream>
#include <sstream>

/* ── DPAPI helpers (Windows only) ────────────────────────────────────── */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

static std::string dpapi_encrypt_b64(const std::string &plain)
{
	if (plain.empty())
		return {};
	DATA_BLOB in{(DWORD)plain.size(), (BYTE *)plain.c_str()};
	DATA_BLOB out{};
	if (!CryptProtectData(&in, L"obs-scene-multistream-tokens", nullptr, nullptr, nullptr, 0, &out)) {
		obs_log(LOG_WARNING, "[scene-multistream] DPAPI encrypt failed 0x%lx", (unsigned long)GetLastError());
		return plain;
	}
	DWORD n = 0;
	CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
	std::string b64(n, '\0');
	CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &n);
	if (!b64.empty() && b64.back() == '\0')
		b64.pop_back();
	LocalFree(out.pbData);
	return b64;
}

static std::string dpapi_decrypt_b64(const std::string &b64)
{
	if (b64.empty())
		return {};
	DWORD bin_len = 0;
	if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, nullptr, &bin_len, nullptr,
				  nullptr))
		return b64; /* plain text fallback */
	std::vector<BYTE> bin(bin_len);
	CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, bin.data(), &bin_len, nullptr,
			     nullptr);
	DATA_BLOB in{bin_len, bin.data()};
	DATA_BLOB out{};
	if (!CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
		obs_log(LOG_WARNING, "[scene-multistream] DPAPI decrypt failed 0x%lx", (unsigned long)GetLastError());
		return b64;
	}
	std::string plain((char *)out.pbData, out.cbData);
	LocalFree(out.pbData);
	return plain;
}
#else
static std::string dpapi_encrypt_b64(const std::string &s)
{
	return s;
}
static std::string dpapi_decrypt_b64(const std::string &s)
{
	return s;
}
#endif
/* ─────────────────────────────────────────────────────────────────────── */

#include <vector>

static std::string token_file_path(const std::string &provider)
{
	char *base = obs_module_config_path((provider + "_tokens.dat").c_str());
	std::string result = base ? base : "";
	bfree(base);
	return result;
}

namespace token_store {

void save(const std::string &provider, const std::string &access_token, const std::string &refresh_token)
{
	std::string path = token_file_path(provider);
	if (path.empty())
		return;
	std::filesystem::path p(path);
	std::error_code ec;
	std::filesystem::create_directories(p.parent_path(), ec);

	std::string enc_access = dpapi_encrypt_b64(access_token);
	std::string enc_refresh = dpapi_encrypt_b64(refresh_token);

	std::ofstream f(path, std::ios::out | std::ios::trunc);
	if (!f) {
		obs_log(LOG_WARNING, "[scene-multistream] cannot write token file: %s", path.c_str());
		return;
	}
	f << enc_access << "\n" << enc_refresh << "\n";
}

bool load(const std::string &provider, std::string &access_token_out, std::string &refresh_token_out)
{
	std::string path = token_file_path(provider);
	if (path.empty() || !os_file_exists(path.c_str()))
		return false;
	std::ifstream f(path);
	if (!f)
		return false;
	std::string enc_access, enc_refresh;
	if (!std::getline(f, enc_access) || !std::getline(f, enc_refresh))
		return false;
	access_token_out = dpapi_decrypt_b64(enc_access);
	refresh_token_out = dpapi_decrypt_b64(enc_refresh);
	return !access_token_out.empty();
}

void clear(const std::string &provider)
{
	std::string path = token_file_path(provider);
	if (!path.empty())
		std::filesystem::remove(path);
}

} // namespace token_store
