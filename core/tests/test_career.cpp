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
Sim runCareerRace(Career& career, const SeasonCalendar& calendar, const std::vector<Entry>& entries, const RaceRules& rules,
                  const Judge& judge = {}) {
    Sim sim = makeCareerRace(career, calendar, kData, entries, rules);
    std::size_t seen = 0;
    auto look = [&] {
        for (; seen < sim.incidents().size(); ++seen) {
            if (judge) {
                judge(sim, sim.incidents()[seen]);
            }
        }
    };
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
        look();
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
    CHECK(a.suspended == b.suspended);
    REQUIRE(a.races.size() == b.races.size());
    for (std::size_t k = 0; k < a.races.size(); ++k) {
        const CareerRace& x = a.races[k];
        const CareerRace& y = b.races[k];
        CHECK(x.name == y.name);
        CHECK(x.podium == y.podium);
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
    const std::vector<int> racing = careerRaceEntries(career);
    CHECK(static_cast<int>(racing.size()) == n - 2);
    CHECK(std::find(racing.begin(), racing.end(), 3) == racing.end());
    CHECK(std::find(racing.begin(), racing.end(), 7) == racing.end());
    const int pointsOf3 = career.championship.drivers[3].points;
    const int pointsOf7 = career.championship.drivers[7].points;

    const Sim second = runCareerRace(career, calendar, entries, rules);
    CHECK(static_cast<int>(second.entries().size()) == n - 2);
    for (const Entry& entry : second.entries()) {
        CHECK(entry.name != entries[3].name);
        CHECK(entry.name != entries[7].name);
    }
    CHECK(career.championship.drivers[3].points == pointsOf3); // no race, no points
    CHECK(career.championship.drivers[7].points == pointsOf7);
    REQUIRE(career.races.size() == 2);
    CHECK(career.races[1].suspended == std::vector<int>{3, 7});
    CHECK(career.suspended.empty()); // one race only
    // The results of the smaller field are booked under the season's entry indices.
    const int winner = second.snapshot().cars.front().entryIndex;
    REQUIRE_FALSE(career.races[1].podium.empty());
    CHECK(entries[career.races[1].podium[0]].name == second.entries()[winner].name);
    int total = 0;
    for (const ChampionshipLine& driver : career.championship.drivers) {
        total += driver.points;
    }
    CHECK(total == 202); // two races, 101 points each

    const Sim third = runCareerRace(career, calendar, entries, rules);
    CHECK(static_cast<int>(third.entries().size()) == n); // back on the grid
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
