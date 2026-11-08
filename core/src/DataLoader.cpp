#include "uisim/DataLoader.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

namespace uisim {
namespace {

using nlohmann::json;

// path.string() throws on Windows for non-ASCII paths; go through UTF-8.
std::string displayName(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

json readJson(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open " + displayName(path));
    }
    try {
        return json::parse(file);
    } catch (const json::exception& e) {
        throw std::runtime_error("Invalid JSON in " + displayName(path) + ": " + e.what());
    }
}

} // namespace

Track loadTrack(const std::filesystem::path& path) {
    const json root = readJson(path);
    try {
        std::vector<TrackSegment> segments;
        for (const json& js : root.at("segments")) {
            TrackSegment seg;
            seg.length = js.at("length_m").get<double>();
            seg.speed = js.at("speed_kmh").get<double>() / 3.6;
            seg.overtakeChance = js.value("overtake", 0.0);
            seg.sector = js.at("sector").get<int>() - 1;
            seg.turn = js.value("turn_deg", 0.0) * (3.14159265358979323846 / 180.0);
            segments.push_back(seg);
        }
        return Track(root.at("name").get<std::string>(), std::move(segments), root.value("marshal_sectors", 20));
    } catch (const json::exception& e) {
        throw std::runtime_error("Bad track data in " + displayName(path) + ": " + e.what());
    } catch (const std::invalid_argument& e) {
        throw std::runtime_error(displayName(path) + ": " + e.what());
    }
}

std::vector<Entry> loadEntries(const std::filesystem::path& path) {
    const json root = readJson(path);
    try {
        std::vector<Entry> entries;
        for (const json& team : root.at("teams")) {
            for (const json& driver : team.at("drivers")) {
                Entry e;
                e.number = driver.at("number").get<int>();
                e.name = driver.at("name").get<std::string>();
                e.code = driver.at("code").get<std::string>();
                e.team = team.at("name").get<std::string>();
                e.teamPace = team.at("pace").get<double>();
                e.skill = driver.at("skill").get<double>();
                e.aggression = driver.value("aggression", 0.5);
                entries.push_back(std::move(e));
            }
        }
        return entries;
    } catch (const json::exception& e) {
        throw std::runtime_error("Bad entry data in " + displayName(path) + ": " + e.what());
    }
}

RaceRules loadRules(const std::filesystem::path& path) {
    const json root = readJson(path);
    try {
        const json& r = root.at("rules");
        auto number = [&](const char* key) { return r.at(key).at("value").get<double>(); };
        RaceRules rules;
        rules.yellowSpeedFactor = number("yellow_speed_factor");
        rules.doubleYellowSpeedFactor = number("double_yellow_speed_factor");
        rules.vscSpeedFraction = number("vsc_speed_fraction");
        rules.safetyCarSpeedFraction = number("safety_car_speed_fraction");
        rules.safetyCarCatchUpFraction = number("safety_car_catch_up_fraction");
        rules.redFlagSpeedFraction = number("red_flag_speed_fraction");
        rules.redFlagFreeTyreChange = r.at("red_flag_free_tyre_change").at("value").get<bool>();
        return rules;
    } catch (const json::exception& e) {
        throw std::runtime_error("Bad rules data in " + displayName(path) + ": " + e.what());
    }
}

} // namespace uisim
