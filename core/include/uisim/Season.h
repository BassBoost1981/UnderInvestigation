#pragma once

#include "uisim/Entry.h"
#include "uisim/Rules.h"
#include "uisim/Sim.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace uisim {

// A season (M10.4): a calendar of races, points per race and the two championships.
// Rules: points A2.2.1, ties A2.1, classification B2.5.5, race distance B2.5.2.

struct SeasonRace {
    std::string name;           // event name
    std::string track;          // file below data/tracks
    std::string weather;        // file below data/weather, empty = dry
    bool shortDistance = false; // B2.5.2 b: the one street circuit with the shorter distance
    int laps = 0;               // 0 = from the race distance (B2.5.2)
};

struct SeasonCalendar {
    std::string name;
    std::vector<SeasonRace> races;
};

// Least number of complete laps that exceeds the race distance (B2.5.2).
int raceLaps(double lapLengthMetres, const RaceRules& rules, bool shortDistance);

// Points for places 1.. for the share of the scheduled distance the leader completed (A2.2.1).
// Empty = no points: fewer than two laps, or fewer than two laps without SC or VSC.
std::vector<int> racePointsTable(const RaceRules& rules, int leaderLaps, int scheduledLaps, int greenLaps);

struct RaceResultLine {
    int entry = 0;           // index into the entry list
    bool classified = false; // B2.5.5 b: at least 90 % of the winner's laps, not disqualified
    int position = 0;        // 1-based among the classified cars, 0 = not classified
    int points = 0;
};

struct RaceResult {
    std::vector<RaceResultLine> lines; // in timing order
};

// Classification and points of a finished race. The sim always runs the full distance, and it
// does not count the laps under green, so the full table applies (see racePointsTable).
RaceResult classifyRace(const Sim& sim, const RaceRules& rules);

struct ChampionshipLine {
    std::string name;        // driver or team
    int points = 0;
    std::vector<int> places; // places[k] = number of classified finishes in place k + 1
};

struct Championship {
    std::vector<ChampionshipLine> drivers; // index = entry index
    std::vector<ChampionshipLine> teams;   // in order of appearance in the entry list
    int racesRun = 0;
};

Championship newChampionship(const std::vector<Entry>& entries);
// `teamOnly`: entries whose car was driven by a reserve; their result counts for the team alone (M11.6).
void addResult(Championship& championship, const std::vector<Entry>& entries, const RaceResult& result,
               const std::vector<int>& teamOnly = {});

// Indices into `lines`, best first: points, then the number of first places, second places
// and so on (A2.1); still equal: the earlier line.
std::vector<int> standings(const std::vector<ChampionshipLine>& lines);

// Seed of race `index` (0-based) in a season: every race differs, the season is reproducible.
std::uint64_t seasonRaceSeed(std::uint64_t seasonSeed, int index);

// Form of the day (M11.2): the entries as they turn up for race `index`. Every team and driver
// has a form per race, every team a liking for fast or slow circuits and every driver one for
// rain, all from the season seed and the names, so a smaller field keeps the same values.
// Single races outside a season do not use this.
std::vector<Entry> seasonRaceEntries(const std::vector<Entry>& entries, std::uint64_t seasonSeed, int index, const Track& track,
                                     bool rain);

// Builds race `index` of the calendar: track and weather from `dataDir`, laps from the race distance,
// the entries with their form of the day.
Sim makeSeasonRace(const SeasonCalendar& calendar, int index, std::uint64_t seasonSeed, const std::filesystem::path& dataDir,
                   const std::vector<Entry>& entries, const RaceRules& rules);

} // namespace uisim
