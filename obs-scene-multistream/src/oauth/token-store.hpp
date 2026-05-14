#pragma once

#include <string>

/* Persists OAuth tokens using DPAPI (Windows) or plain file (other platforms).
   Each provider has its own storage slot identified by a string key.    */
namespace token_store {

/* Store access_token + refresh_token for a provider.
   On Windows, values are DPAPI-encrypted before writing. */
void save(const std::string &provider, const std::string &access_token, const std::string &refresh_token);

/* Load tokens. Returns false if not found. */
bool load(const std::string &provider, std::string &access_token_out, std::string &refresh_token_out);

/* Remove stored tokens for provider. */
void clear(const std::string &provider);

} // namespace token_store
