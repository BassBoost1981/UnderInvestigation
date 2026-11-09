#pragma once

#include "uisim/Entry.h"
#include "uisim/Rules.h"
#include "uisim/Track.h"
#include "uisim/Weather.h"

#include <filesystem>
#include <vector>

namespace uisim {

// Load data/tracks/*.json. Throws std::runtime_error with the file name on
// missing files, invalid JSON or missing fields.
Track loadTrack(const std::filesystem::path& path);

// Load data/entries/*.json (teams with drivers). Same error handling.
std::vector<Entry> loadEntries(const std::filesystem::path& path);

// Load data/rules/*.json. Every parameter must be present. Same error handling.
RaceRules loadRules(const std::filesystem::path& path);

// Load data/weather/*.json (rain over the race, M9.2). Same error handling.
WeatherPlan loadWeather(const std::filesystem::path& path);

} // namespace uisim
