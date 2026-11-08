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
            seg.trackLimits = js.value("track_limits", false);
            segments.push_back(seg);
        }
        Track track(root.at("name").get<std::string>(), std::move(segments), root.value("marshal_sectors", 20));
        if (root.contains("overtake_detection_m") && root.contains("overtake_activation_m")) {
            track.setOvertakeLines(root.at("overtake_detection_m").get<double>(), root.at("overtake_activation_m").get<double>());
        }
        if (root.contains("tyres")) {
            CompoundSpecs specs;
            const char* names[] = {"soft", "medium", "hard"};
            for (int c = 0; c < 3; ++c) {
                const json& t = root.at("tyres").at(names[c]);
                specs[c] = {t.at("pace").get<double>(), t.at("wear_per_lap").get<double>(), t.at("cliff_lap").get<int>(),
                            t.at("cliff_loss_per_lap").get<double>()};
            }
            track.setCompounds(specs);
        }
        return track;
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
        rules.pitLaneSpeedKmh = number("pit_lane_speed_kmh");
        rules.stopAndGoSeconds = number("stop_and_go_seconds");
        rules.unservedDriveThroughSeconds = number("unserved_drive_through_seconds");
        rules.unservedStopAndGoSeconds = number("unserved_stop_and_go_seconds");
        rules.blueFlagMarshalSectors = static_cast<int>(number("blue_flag_marshal_sectors"));
        rules.overtakeDetectionGapSeconds = number("overtake_detection_gap_seconds");
        rules.minDryCompounds = static_cast<int>(number("min_dry_compounds"));
        rules.twoCompoundNotRestartedSeconds = number("two_compound_not_restarted_seconds");
        rules.timePenaltyServedAtNextStop = r.at("time_penalty_served_at_next_stop").at("value").get<bool>();
        rules.pitSpeedingDriveThroughFromKmh = number("pit_speeding_drive_through_from_kmh");
        rules.pitSpeedingStopAndGoAboveKmh = number("pit_speeding_stop_and_go_above_kmh");
        rules.unsafeReleasePenaltySeconds = number("unsafe_release_penalty_seconds");
        rules.trackLimitsBlackWhiteAt = static_cast<int>(number("track_limits_black_white_at"));
        rules.trackLimitsPenaltyFrom = static_cast<int>(number("track_limits_penalty_from"));
        rules.trackLimitsPenaltySeconds = number("track_limits_penalty_seconds");
        rules.lappedCarsMayOvertake = r.at("lapped_cars_may_overtake").at("value").get<bool>();
        rules.safetyCarInLapsAfterLappedCars = static_cast<int>(number("safety_car_in_laps_after_lapped_cars"));
        const std::string closedPenalty = r.at("pit_entry_closed_penalty").at("value").get<std::string>();
        if (closedPenalty == "drive_through") {
            rules.pitEntryClosedPenalty = Penalty::DriveThrough;
        } else if (closedPenalty == "stop_and_go") {
            rules.pitEntryClosedPenalty = Penalty::StopAndGo;
        } else {
            throw std::runtime_error("Bad rules data in " + displayName(path) + ": unknown pit_entry_closed_penalty '" +
                                     closedPenalty + "'");
        }
        return rules;
    } catch (const json::exception& e) {
        throw std::runtime_error("Bad rules data in " + displayName(path) + ": " + e.what());
    }
}

} // namespace uisim
