// M10.5: career in the core: penalty points and suspension (A3.3.1 b), the player's rating over
// the season, the save file between races.

#include "uisim/Career.h"
#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Rating.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <vector>

using namespace uisim;

namespace {

const std::filesystem::path kData(UISIM_DATA_DIR);

std::vector<Entry> grid() { return loadEntries(kData / "entries" / "fictional-grid.json"); }
RaceRules loadedRules() { return loadRules(kData / "rules" / "fia-f1-2026.json"); }

SeasonCalendar calendarOf(int races, int laps) {
    SeasonCalendar calendar;
    calendar.name = "Career test season";
    for (int k = 0; k < races; ++k) {
        SeasonRace race;
        race.name = "Race " + std::to_string(k + 1);
        race.track = "testring.json";
        race.laps = laps;
        calendar.races.push_back(race);
    }
    return calendar;
}

Command verdict(int incidentId, int car, Penalty penalty) {
    Command c;
    c.type = Command::Type::IssueVerdict;
    c.incidentId = incidentId;
    c.car = car;
    c.penalty = penalty;
    return c;
}

using Judge = std::function<void(Sim&, const Incident&)>;

// The stand-in player of most tests: 5 s for the first car of every collision, a warning otherwise.
void strict(Sim& sim, const Incident& incident) {
    sim.command(verdict(incident.id, incident.car,
                        incident.type == Incident::Type::Collision ? Penalty::FiveSeconds : Penalty::Warning));
}

// Drives the next race of the career to the flag and books it. Returns the finished race.
// The judge decides after the flag and after a hearing on every incident, so no verdict is open to review.
Sim runCareerRace(Career& career, const SeasonCalendar& calendar, const std::vector<Entry>& entries, const RaceRules& rules,
                  const Judge& judge = {}) {
    Sim sim = makeCareerRace(career, calendar, kData, entries, rules);
    std::size_t seen = 0;
    auto look = [&] {
        for (; seen < sim.incidents().size(); ++seen) {
            if (judge) {
                Command hearing;
                hearing.type = Command::Type::HoldHearing;
                hearing.incidentId = sim.incidents()[seen].id;
                sim.command(hearing);
                judge(sim, sim.incidents()[seen]);
            }
        }
    };
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
    look();
    sim.step(); // verdicts on incidents raised at the flag
    finishCareerRace(career, calendar, sim, entries, rules);
    return sim;
}

void checkSame(const std::vector<ChampionshipLine>& a, const std::vector<ChampionshipLine>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t k = 0; k < a.size(); ++k) {
        CHECK(a[k].name == b[k].name);
        CHECK(a[k].points == b[k].points);
        CHECK(a[k].places == b[k].places);
    }
}

void checkSame(const Career& a, const Career& b) {
    CHECK(a.season == b.season);
    CHECK(a.seed == b.seed);
    CHECK(a.nextRace == b.nextRace);
    CHECK(a.championship.racesRun == b.championship.racesRun);
    checkSame(a.championship.drivers, b.championship.drivers);
    checkSame(a.championship.teams, b.championship.teams);
    CHECK(a.penaltyPoints == b.penaltyPoints);
    CHECK(a.pointRaces == b.pointRaces);
    CHECK(a.racesDriven == b.racesDriven);
    CHECK(a.suspended == b.suspended);
    REQUIRE(a.races.size() == b.races.size());
    for (std::size_t k = 0; k < a.races.size(); ++k) {
        const CareerRace& x = a.races[k];
        const CareerRace& y = b.races[k];
        CHECK(x.name == y.name);
        CHECK(x.podium == y.podium);
        CHECK(x.pole == y.pole);
        CHECK(x.suspended == y.suspended);
        REQUIRE(x.penaltyPoints.size() == y.penaltyPoints.size());
        for (std::size_t p = 0; p < x.penaltyPoints.size(); ++p) {
            CHECK(x.penaltyPoints[p].entry == y.penaltyPoints[p].entry);
            CHECK(x.penaltyPoints[p].points == y.penaltyPoints[p].points);
        }
        CHECK(x.overall == y.overall); // doubles survive the save file exactly
        CHECK(x.fairness == y.fairness);
        CHECK(x.consistency == y.consistency);
        CHECK(x.raceQuality == y.raceQuality);
        CHECK(x.safety == y.safety);
        CHECK(x.incidents == y.incidents);
        CHECK(x.decided == y.decided);
    }
}

std::filesystem::path saveFile(const char* name) { return std::filesystem::temp_directory_path() / name; }

} // namespace

TEST_CASE("A new career starts empty") {
    const std::vector<Entry> entries = grid();
    const SeasonCalendar calendar = calendarOf(3, 5);
    const Career career = newCareer(calendar, entries, 5);
    CHECK(career.season == calendar.name);
    CHECK(career.seed == 5);
    CHECK(career.nextRace == 0);
    CHECK_FALSE(careerFinished(career, calendar));
    CHECK(career.penaltyPoints == std::vector<int>(entries.size(), 0));
    CHECK(career.suspended.empty());
    CHECK(career.races.empty());
    CHECK(careerRating(career) == 0.0);
    std::vector<int> all(entries.size());
    std::iota(all.begin(), all.end(), 0);
    CHECK(careerRaceEntries(career) == all);
    REQUIRE(career.championship.drivers.size() == entries.size());
    for (const ChampionshipLine& driver : career.championship.drivers) {
        CHECK(driver.points == 0);
    }
}

TEST_CASE("Penalty points go with a penalty, by the kind of incident (A3.3.1 b)") {
    const RaceRules rules = loadedRules();
    using Type = Incident::Type;
    CHECK(penaltyPointsFor(Type::Collision, rules) == 2);
    CHECK(penaltyPointsFor(Type::OvertakeUnderYellow, rules) == 3);
    CHECK(penaltyPointsFor(Type::OvertakeUnderVsc, rules) == 2);
    CHECK(penaltyPointsFor(Type::OvertakeUnderSafetyCar, rules) == 2);
    CHECK(penaltyPointsFor(Type::OvertakeBeforeRestartLine, rules) == 2);
    CHECK(penaltyPointsFor(Type::OvertakeUnderRedFlag, rules) == 4);
    for (Type type : {Type::Accident, Type::PitLaneSpeeding, Type::UnsafeRelease, Type::TrackLimits, Type::CompoundRule,
                      Type::PitEntryClosed, Type::WetTyreRule, Type::FalseStart, Type::GridPosition, Type::IgnoringBlueFlags}) {
        CHECK(penaltyPointsFor(type, rules) == 0);
    }

    const std::vector<Entry> entries = grid();
    const SeasonCalendar calendar = calendarOf(1, 15);
    // 5 s for the first car of every collision, a warning for everything else.
    Career career = newCareer(calendar, entries, 3);
    std::vector<int> expected(entries.size(), 0);
    int collisions = 0;
    int others = 0;
    runCareerRace(career, calendar, entries, rules, [&](Sim& sim, const Incident& incident) {
        strict(sim, incident);
        if (incident.type == Type::Collision) {
            expected[incident.car] += 2;
            ++collisions;
        } else {
            ++others;
        }
    });
    INFO("collisions ", collisions, ", other incidents ", others);
    REQUIRE(collisions > 0);
    CHECK(others > 0); // warnings were given too
    REQUIRE(*std::max_element(expected.begin(), expected.end()) < rules.penaltyPointsSuspensionAt);
    CHECK(career.penaltyPoints == expected); // all cars race: race index = season index
    CHECK(career.suspended.empty());
    REQUIRE(career.races.size() == 1);
    CHECK(static_cast<int>(career.races[0].penaltyPoints.size()) == collisions);
    CHECK(career.races[0].decided == collisions + others);

    // "No further action" and warnings alone: no penalty points.
    Career lenient = newCareer(calendar, entries, 3);
    runCareerRace(lenient, calendar, entries, rules, [](Sim& sim, const Incident& incident) {
        sim.command(verdict(incident.id, incident.car, incident.id % 2 ? Penalty::None : Penalty::Warning));
    });
    CHECK(lenient.penaltyPoints == std::vector<int>(entries.size(), 0));
    CHECK(lenient.races[0].penaltyPoints.empty());

    // Without a player nothing is decided and nobody gets points.
    Career idle = newCareer(calendar, entries, 3);
    runCareerRace(idle, calendar, entries, rules);
    CHECK(idle.penaltyPoints == std::vector<int>(entries.size(), 0));
    CHECK(idle.races[0].decided == 0);
    CHECK(idle.races[0].incidents == lenient.races[0].incidents); // warnings do not change the race
}

TEST_CASE("At twelve penalty points a driver sits out the next race") {
    const std::vector<Entry> entries = grid();
    const RaceRules rules = loadedRules();
    const SeasonCalendar calendar = calendarOf(3, 5);
    const int n = static_cast<int>(entries.size());
    Career career = newCareer(calendar, entries, 8);
    career.penaltyPoints[3] = 12;
    career.penaltyPoints[7] = 13;
    career.penaltyPoints[5] = 11;

    const Sim first = runCareerRace(career, calendar, entries, rules);
    CHECK(static_cast<int>(first.entries().size()) == n); // the suspension starts with the next race
    CHECK(career.suspended == std::vector<int>{3, 7});
    CHECK(career.penaltyPoints[3] == 0); // twelve are taken off the licence
    CHECK(career.penaltyPoints[7] == 1);
    CHECK(career.penaltyPoints[5] == 11);
    CHECK(static_cast<int>(careerRaceEntries(career).size()) == n); // reserve drivers fill the grid
    const int pointsOf3 = career.championship.drivers[3].points;
    const int pointsOf7 = career.championship.drivers[7].points;

    const Sim second = runCareerRace(career, calendar, entries, rules);
    REQUIRE(static_cast<int>(second.entries().size()) == n);
    CHECK(second.entries()[3].name == reserveDriver(entries[3]).name); // the reserve in the suspended driver's car
    CHECK(second.entries()[3].team == entries[3].team);
    CHECK(second.entries()[3].skill < entries[3].skill);
    CHECK(second.entries()[7].name == reserveDriver(entries[7]).name);
    CHECK(second.entries()[4].name == entries[4].name);
    CHECK(career.championship.drivers[3].points == pointsOf3); // no race, no points
    CHECK(career.championship.drivers[7].points == pointsOf7);
    REQUIRE(career.races.size() == 2);
    CHECK(career.races[1].suspended == std::vector<int>{3, 7});
    CHECK(career.suspended.empty()); // one race only
    int drivers = 0;
    int teams = 0;
    for (const ChampionshipLine& driver : career.championship.drivers) {
        drivers += driver.points;
    }
    for (const ChampionshipLine& team : career.championship.teams) {
        teams += team.points;
    }
    CHECK(teams == 202);     // two races, 101 points each: what a reserve scores counts for the team
    CHECK(drivers <= teams); // but for no driver

    const Sim third = runCareerRace(career, calendar, entries, rules);
    CHECK(static_cast<int>(third.entries().size()) == n);
    CHECK(third.entries()[3].name == entries[3].name); // back in his car
    CHECK(careerFinished(career, calendar));
    CHECK_THROWS(makeCareerRace(career, calendar, kData, entries, rules));
}

TEST_CASE("Saving, loading and going on gives the same season as running it through") {
    const std::vector<Entry> entries = grid();
    const RaceRules rules = loadedRules();
    SeasonCalendar calendar = calendarOf(4, 6);
    calendar.races[2].weather = "shower.json";
    // Everyone one penalty away from a suspension, so suspensions happen and cross the save.
    auto fresh = [&] {
        Career career = newCareer(calendar, entries, 21);
        career.penaltyPoints.assign(entries.size(), 11);
        career.penaltyPoints[0] = 12; // suspended after the first race, whatever happens in it
        return career;
    };

    Career through = fresh();
    for (int k = 0; k < 4; ++k) {
        runCareerRace(through, calendar, entries, rules, strict);
    }
    REQUIRE(careerFinished(through, calendar));

    const std::filesystem::path file = saveFile("uisim_career_test.json");
    Career firstHalf = fresh();
    runCareerRace(firstHalf, calendar, entries, rules, strict);
    CHECK_FALSE(firstHalf.suspended.empty()); // a suspension is pending at the save
    saveCareer(file, firstHalf);

    Career resumed = loadCareer(file, calendar, entries);
    checkSame(resumed, firstHalf); // the file holds everything
    runCareerRace(resumed, calendar, entries, rules, strict);
    runCareerRace(resumed, calendar, entries, rules, strict);
    runCareerRace(resumed, calendar, entries, rules, strict);
    checkSame(resumed, through);

    int sittingOut = 0;
    for (const CareerRace& race : through.races) {
        sittingOut += static_cast<int>(race.suspended.size());
        CHECK(race.overall >= 0.0);
        CHECK(race.overall <= 100.0);
    }
    CHECK(sittingOut > 0);
    CHECK(careerRating(through) == doctest::Approx((through.races[0].overall + through.races[1].overall +
                                                    through.races[2].overall + through.races[3].overall) / 4.0));
    std::filesystem::remove(file);
}

TEST_CASE("A save file that does not fit the season is refused") {
    const std::vector<Entry> entries = grid();
    const RaceRules rules = loadedRules();
    const SeasonCalendar calendar = calendarOf(2, 5);
    Career career = newCareer(calendar, entries, 2);
    runCareerRace(career, calendar, entries, rules);
    const std::filesystem::path file = saveFile("uisim_career_refused.json");
    saveCareer(file, career);
    CHECK_NOTHROW(loadCareer(file, calendar, entries));

    SeasonCalendar other = calendar;
    other.name = "Another season";
    CHECK_THROWS(loadCareer(file, other, entries));
    SeasonCalendar shorter = calendarOf(0, 5);
    CHECK_THROWS(loadCareer(file, shorter, entries)); // one race run, none in the calendar
    std::vector<Entry> fewer(entries.begin(), entries.end() - 1);
    CHECK_THROWS(loadCareer(file, calendar, fewer));
    std::vector<Entry> renamed = entries;
    renamed[4].name = "Somebody Else";
    CHECK_THROWS(loadCareer(file, calendar, renamed));
    CHECK_THROWS(loadCareer(saveFile("uisim_career_missing.json"), calendar, entries));
    {
        std::ofstream broken(file);
        broken << "{ \"format\": 1, \"season\": ";
    }
    CHECK_THROWS(loadCareer(file, calendar, entries));
    std::filesystem::remove(file);

    // A race that is still running cannot be booked.
    Sim running = makeCareerRace(career, calendar, kData, entries, rules);
    CHECK_THROWS(finishCareerRace(career, calendar, running, entries, rules));
}

TEST_CASE("Penalty points expire after a season's worth of races and carry over into the next season (M11.6)") {
    const std::vector<Entry> entries = grid();
    const RaceRules rules = loadedRules();
    const SeasonCalendar calendar = calendarOf(3, 4); // three races: the sim's twelve months
    Career career = newCareer(calendar, entries, 4);
    career.penaltyPoints[2] = 5; // on the licence before the first race
    for (int k = 0; k < 3; ++k) {
        runCareerRace(career, calendar, entries, rules);
        CHECK(career.penaltyPoints[2] == 5); // still within three races
    }
    REQUIRE(careerFinished(career, calendar));
    CHECK(career.racesDriven == 3);

    Career second = nextSeason(career, calendar, entries);
    CHECK(second.seed == 5);
    CHECK(second.nextRace == 0);
    CHECK(second.races.empty());
    CHECK(second.championship.racesRun == 0);
    CHECK(second.penaltyPoints[2] == 5); // the licence comes along
    CHECK(second.racesDriven == 3);
    for (const ChampionshipLine& driver : second.championship.drivers) {
        CHECK(driver.points == 0);
    }
    runCareerRace(second, calendar, entries, rules);
    CHECK(second.penaltyPoints[2] == 0); // the fourth race after: expired
    CHECK(second.racesDriven == 4);
}

TEST_CASE("Save files of the older formats are refused (M11.6, M13.6)") {
    const std::vector<Entry> entries = grid();
    const SeasonCalendar calendar = calendarOf(2, 5);
    const std::filesystem::path file = saveFile("uisim_career_old_format.json");
    Career career = newCareer(calendar, entries, 2);
    career.penaltyPoints[1] = 3;
    saveCareer(file, career);
    const Career loaded = loadCareer(file, calendar, entries);
    CHECK(loaded.penaltyPoints[1] == 3);
    // The same file with the formats of M10.5 and M11.6.
    std::ifstream in(file);
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const std::size_t at = content.find("\"format\": 3");
    REQUIRE(at != std::string::npos);
    for (const char* old : {"\"format\": 1", "\"format\": 2"}) {
        std::string changed = content;
        changed.replace(at, 11, old);
        {
            std::ofstream out(file);
            out << changed;
        }
        CHECK_THROWS(loadCareer(file, calendar, entries));
    }
    std::filesystem::remove(file);
}

TEST_CASE("The podium names the reserve when a suspended driver's car finished (M12.8)") {
    const std::vector<Entry> entries = grid();
    CareerRace race;
    race.podium = {5, 3, 7};
    race.suspended = {3};
    CHECK(podiumName(entries, race, 0) == entries[5].name);
    CHECK(podiumName(entries, race, 1) == reserveDriver(entries[3]).name);
    CHECK(podiumName(entries, race, 1) != entries[3].name);
    CHECK(podiumName(entries, race, 2) == entries[7].name);
}

// ---------------------------------------------------------------------------------------------
// M13.6: a weekend in the career is a qualifying and the race.

namespace {

// A qualifying with shorter segments, so a weekend test does not take long.
RaceRules weekendRules() {
    RaceRules rules = loadedRules();
    rules.qualifyingSegmentMinutes = {8, 7, 6};
    return rules;
}

struct Weekend {
    Sim qualifying;
    Sim race;
    std::vector<int> grid;
};

void runToEnd(Sim& sim) {
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
}

// After the end of a session: a hearing and a verdict on every incident, then the step that applies them.
void judgeAll(Sim& sim, const Judge& judge) {
    for (const Incident& incident : std::vector<Incident>(sim.incidents())) {
        if (judge) {
            Command hearing;
            hearing.type = Command::Type::HoldHearing;
            hearing.incidentId = incident.id;
            sim.command(hearing);
            judge(sim, incident);
        }
    }
    sim.step();
}

// Qualifying, the grid after all verdicts, the race from it, booked as one weekend.
Weekend runWeekend(Career& career, const SeasonCalendar& calendar, const std::vector<Entry>& entries, const RaceRules& rules,
                   const Judge& session = {}, const Judge& race = {}) {
    Sim qualifying = makeCareerQualifying(career, calendar, kData, entries, rules);
    runToEnd(qualifying);
    judgeAll(qualifying, session);
    std::vector<int> grid = qualifying.startingGrid();
    qualifying.lockGrid();
    Sim sim = makeCareerRace(career, calendar, kData, entries, rules, grid);
    runToEnd(sim);
    judgeAll(sim, race);
    finishCareerRace(career, calendar, sim, entries, rules, &qualifying);
    return {std::move(qualifying), std::move(sim), std::move(grid)};
}

// The stand-in for a session: three places for impeding, the lap deleted otherwise.
void sessionJudge(Sim& sim, const Incident& incident) {
    Command c = verdict(incident.id, incident.car,
                        incident.type == Incident::Type::Impeding ? Penalty::GridDrop : Penalty::DeleteLap);
    c.places = incident.type == Incident::Type::Impeding ? 3 : 0;
    sim.command(c);
}

int countOf(const Sim& sim, Incident::Type type) {
    return static_cast<int>(std::count_if(sim.incidents().begin(), sim.incidents().end(),
                                          [&](const Incident& incident) { return incident.type == type; }));
}

} // namespace

TEST_CASE("Penalty points for the incidents of a qualifying (M13.6)") {
    const RaceRules rules = loadedRules();
    CHECK(penaltyPointsFor(Incident::Type::Impeding, rules) == rules.penaltyPointsQualifyingImpeding);
    CHECK(penaltyPointsFor(Incident::Type::YellowFlagLap, rules) == rules.penaltyPointsYellowFlag);
    CHECK(penaltyPointsFor(Incident::Type::LapTrackLimits, rules) == 0); // as track limits in the race
}

TEST_CASE("A weekend: the reserve qualifies, the race starts from the session's grid, pole and one rating are booked (M13.6)") {
    const std::vector<Entry> entries = grid();
    const RaceRules rules = weekendRules();
    const SeasonCalendar calendar = calendarOf(2, 5);
    Career career = newCareer(calendar, entries, 5);
    career.suspended = {4};
    const Weekend w = runWeekend(career, calendar, entries, rules, sessionJudge);

    // A suspension holds for the whole weekend; both sessions have the same drivers in the same order.
    REQUIRE(w.qualifying.entries().size() == entries.size());
    REQUIRE(w.race.entries().size() == entries.size());
    CHECK(w.qualifying.entries()[4].name == reserveDriver(entries[4]).name);
    for (std::size_t k = 0; k < entries.size(); ++k) {
        CHECK(w.race.entries()[k].name == w.qualifying.entries()[k].name);
        CHECK(w.race.entries()[k].skill == w.qualifying.entries()[k].skill); // a dry race: the same form of the day
        CHECK(w.race.entries()[k].teamPace == w.qualifying.entries()[k].teamPace);
    }

    // The race starts from the grid after the verdicts.
    CHECK(w.grid == w.qualifying.startingGrid());
    for (const CarState& car : w.race.snapshot().cars) {
        const auto slot = std::find(w.grid.begin(), w.grid.end(), car.entryIndex) - w.grid.begin();
        CHECK(car.gridPosition == slot + 1);
    }
    const int impeding = countOf(w.qualifying, Incident::Type::Impeding);
    INFO("qualifying incidents ", w.qualifying.incidents().size(), ", impeding ", impeding);
    REQUIRE(impeding > 0);
    CHECK(w.grid != w.qualifying.qualifyingOrder()); // the grid drops moved someone

    // Pole, one rating over both sessions.
    const CareerRace& race = career.races.back();
    CHECK(race.pole == w.qualifying.qualifyingOrder().front());
    CHECK(poleName(entries, race) == w.qualifying.entries()[race.pole].name);
    const RaceRating weekend = rateWeekend(w.qualifying, w.race);
    const RaceRating alone = rateRace(w.race);
    CHECK(race.overall == weekend.overall);
    CHECK(race.incidents == static_cast<int>(w.qualifying.incidents().size() + w.race.incidents().size()));
    CHECK(race.decided == static_cast<int>(w.qualifying.incidents().size())); // the race was not judged
    CHECK(weekend.raceQuality == alone.raceQuality);
    CHECK(weekend.accidentSites == rateRace(w.qualifying).accidentSites + alone.accidentSites);
    for (std::size_t k = 0; k < weekend.incidents.size(); ++k) {
        CHECK(weekend.incidents[k].qualifying == (k < w.qualifying.incidents().size()));
    }

    // Penalty points: one per grid drop for impeding, none for a deleted lap, none for the reserve's regular driver.
    std::vector<int> points(entries.size(), 0);
    for (const Incident& incident : w.qualifying.incidents()) {
        if (incident.type == Incident::Type::Impeding && incident.car != 4) {
            points[static_cast<std::size_t>(incident.car)] += rules.penaltyPointsQualifyingImpeding;
        }
    }
    CHECK(career.penaltyPoints == points);
}

TEST_CASE("Saving, loading and going on gives the same season with weekends (M13.6)") {
    const std::vector<Entry> entries = grid();
    const RaceRules rules = weekendRules();
    SeasonCalendar calendar = calendarOf(3, 5);
    calendar.races[1].weather = "shower.json"; // the qualifying stays dry
    auto fresh = [&] {
        Career career = newCareer(calendar, entries, 23);
        career.penaltyPoints.assign(entries.size(), 11); // a grid drop for impeding suspends
        career.penaltyPoints[0] = 12;
        return career;
    };

    Career through = fresh();
    for (int k = 0; k < 3; ++k) {
        runWeekend(through, calendar, entries, rules, sessionJudge, strict);
    }
    REQUIRE(careerFinished(through, calendar));

    const std::filesystem::path file = saveFile("uisim_career_weekend_test.json");
    Career firstHalf = fresh();
    runWeekend(firstHalf, calendar, entries, rules, sessionJudge, strict);
    CHECK_FALSE(firstHalf.suspended.empty());
    saveCareer(file, firstHalf);
    Career resumed = loadCareer(file, calendar, entries);
    checkSame(resumed, firstHalf);
    runWeekend(resumed, calendar, entries, rules, sessionJudge, strict);
    runWeekend(resumed, calendar, entries, rules, sessionJudge, strict);
    checkSame(resumed, through);
    for (const CareerRace& race : through.races) {
        CHECK(race.pole >= 0);
    }
    std::filesystem::remove(file);
}
