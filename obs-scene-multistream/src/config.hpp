#pragma once

#include "destination.hpp"
#include <string>
#include <vector>

namespace scenemulti {

std::string config_file_path();

std::vector<DestinationConfig> load_destinations();
bool save_destinations(const std::vector<DestinationConfig> &dests);

} // namespace scenemulti
