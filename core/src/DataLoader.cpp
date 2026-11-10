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
            CompoundSpecs specs = track.compounds(); // rain tyres keep their defaults without data
            const char* names[] = {"soft", "medium", "hard", "intermediate", "wet"};
            const json& tyres = root.at("tyres");
            for (int c = 0; c < 5; ++c) {
                if (c >= 3 && !tyres.contains(names[c])) {
                    continue;
                }
                const json& t = tyres.at(names[c]);
                specs[c] = {t.at("pace").get<double>(), t.at("wear_per_lap").get<double>(), t.at("cliff_lap").get<int>(),
                            t.at("cliff_loss_per_lap").get<double>(), t.value("wet_grip_loss", specs[c].wetGripLoss),
                            t.value("dry_wear_factor", specs[c].dryWearFactor)};
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
        auto penalty = [&](const char* key) {
            const std::string name = r.at(key).at("value").get<std::string>();
            if (name == "drive_through") {
                return Penalty::DriveThrough;
            }
            if (name == "stop_and_go") {
                return Penalty::StopAndGo;
            }
            throw std::runtime_error("Bad rules data in " + displayName(path) + ": unknown " + key + " '" + name + "'");
        };
        auto flag = [&](const char* key) { return r.at(key).at("value").get<bool>(); };
        rules.pitEntryClosedPenalty = penalty("pit_entry_closed_penalty");
        rules.wetTyresVoidCompoundRule = flag("wet_tyres_void_compound_rule");
        rules.wetStartTyreChangePenalty = penalty("wet_start_tyre_change_penalty");
        rules.safetyCarStartLapsDeductedOffset = static_cast<int>(number("safety_car_start_laps_deducted_offset"));
        rules.safetyCarStartStandingRestart = flag("safety_car_start_standing_restart");
        rules.trackDeclaredWetInRace = flag("track_declared_wet_in_race");
        rules.rainLightsWithWetTyres = flag("rain_lights_with_wet_tyres");
        rules.rainHazardPrecipitationProbability = number("rain_hazard_precipitation_probability");
        auto integer = [&](const char* key) { return static_cast<int>(number(key)); };
        auto integers = [&](const char* key) { return r.at(key).at("value").get<std::vector<int>>(); };
        rules.racePoints = integers("race_points");
        rules.racePointsFrom50Percent = integers("race_points_from_50_percent");
        rules.racePointsFrom25Percent = integers("race_points_from_25_percent");
        rules.racePointsFromTwoLaps = integers("race_points_from_two_laps");
        rules.racePointsMinGreenLaps = integer("race_points_min_green_laps");
        rules.classifiedMinDistanceFraction = number("classified_min_distance_fraction");
        rules.raceDistanceKm = number("race_distance_km");
        rules.raceDistanceShortKm = number("race_distance_short_km");
        rules.startLights = integer("start_lights");
        rules.startLightIntervalSeconds = number("start_light_interval_seconds");
        rules.falseStartPenaltySeconds = number("false_start_penalty_seconds");
        rules.falseStartSeriousPenaltySeconds = number("false_start_serious_penalty_seconds");
        rules.startRepeatLapsDeducted = integer("start_repeat_laps_deducted");
        rules.standingRestartAfterRed = flag("standing_restart_after_red");
        rules.penaltyPointsSuspensionAt = integer("penalty_points_suspension_at");
        rules.penaltyPointsValidMonths = integer("penalty_points_valid_months");
        rules.penaltyPointsCollision = integer("penalty_points_collision");
        rules.penaltyPointsYellowFlag = integer("penalty_points_yellow_flag");
        rules.penaltyPointsOvertakingNeutralised = integer("penalty_points_overtaking_neutralised");
        return rules;
    } catch (const json::exception& e) {
        throw std::runtime_error("Bad rules data in " + displayName(path) + ": " + e.what());
    }
}

WeatherPlan loadWeather(const std::filesystem::path& path) {
    const json root = readJson(path);
    try {
        WeatherPlan plan;
        plan.name = root.at("name").get<std::string>();
        plan.variation = root.value("variation", 0.0);
        for (const json& p : root.at("points")) {
            WeatherPoint point;
            point.time = p.at("minute").get<double>() * 60.0;
            point.rain = p.at("rain").get<double>();
            if (point.rain < 0.0 || point.rain > 1.0 || (!plan.points.empty() && point.time < plan.points.back().time)) {
                throw std::runtime_error("Bad weather data in " + displayName(path) +
                                         ": rain must be 0..1 and the points sorted by minute");
            }
            plan.points.push_back(point);
        }
        return plan;
    } catch (const json::exception& e) {
        throw std::runtime_error("Bad weather data in " + displayName(path) + ": " + e.what());
    }
}

SeasonCalendar loadSeason(const std::filesystem::path& path) {
    const json root = readJson(path);
    try {
        SeasonCalendar calendar;
        calendar.name = root.at("name").get<std::string>();
        for (const json& r : root.at("races")) {
            SeasonRace race;
            race.name = r.at("name").get<std::string>();
            race.track = r.at("track").get<std::string>();
            race.weather = r.value("weather", std::string());
            race.shortDistance = r.value("short_distance", false);
            race.laps = r.value("laps", 0);
            if (race.laps < 0) {
                throw std::runtime_error("Bad season data in " + displayName(path) + ": laps must not be negative");
            }
            calendar.races.push_back(race);
        }
        if (calendar.races.empty()) {
            throw std::runtime_error("Bad season data in " + displayName(path) + ": no races");
        }
        return calendar;
    } catch (const json::exception& e) {
        throw std::runtime_error("Bad season data in " + displayName(path) + ": " + e.what());
    }
}

} // namespace uisim
