// M3 acceptance: flags, VSC, Safety Car and red flag (docs/DESIGN.md chapter 8).

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

using namespace uisim;

namespace {

std::filesystem::path dataDir() { return std::filesystem::path(UISIM_DATA_DIR); }

Sim makeRace(std::uint64_t seed, double ruleBreakChance) {
    RaceConfig config;
    config.laps = 30;
    config.seed = seed;
    config.rules = loadRules(dataDir() / "rules" / "fia-f1-2026.json");
    config.ruleBreakChance = ruleBreakChance;
    return Sim(loadTrack(dataDir() / "tracks" / "testring.json"),
               loadEntries(dataDir() / "entries" / "fictional-grid.json"), config);
}

void runUntilLeaderLap(Sim& sim, int lap) {
    for (long steps = 0; sim.snapshot().leaderLap < lap && !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.snapshot().leaderLap >= lap);
}

void runSeconds(Sim& sim, double seconds) {
    const long steps = static_cast<long>(seconds / Sim::kDt);
    for (long s = 0; s < steps && !sim.finished(); ++s) {
        sim.step();
    }
}

void runToEnd(Sim& sim) {
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
}

Command simple(Command::Type type) {
    Command c;
    c.type = type;
    return c;
}

// Road distance (metres) of every lead-lap car behind the leader. Under VSC all
// cars drive the same speed profile, so these distances hold; time gaps at the
// line grow by 1 / vscSpeedFraction while the VSC is out.
std::map<int, double> leadLapDistanceGaps(const SimSnapshot& snap) {
    std::map<int, double> gaps;
    const double leader = snap.cars.front().distance;
    for (const CarState& car : snap.cars) {
        if (car.lapsDown == 0 && !car.finished) {
            gaps[car.entryIndex] = leader - car.distance;
        }
    }
    return gaps;
}

} // namespace

TEST_CASE("Rules file loads with the documented defaults") {
    const RaceRules rules = loadRules(dataDir() / "rules" / "fia-f1-2026.json");
    const RaceRules defaults;
    CHECK(rules.vscSpeedFraction == defaults.vscSpeedFraction);
    CHECK(rules.safetyCarSpeedFraction == defaults.safetyCarSpeedFraction);
    CHECK(rules.doubleYellowSpeedFactor == defaults.doubleYellowSpeedFactor);
    CHECK(rules.redFlagFreeTyreChange == defaults.redFlagFreeTyreChange);
    CHECK(rules.pitLaneSpeedKmh == 80.0);                 // B1.6.3 a
    CHECK(rules.stopAndGoSeconds == defaults.stopAndGoSeconds);
    CHECK(rules.unservedDriveThroughSeconds == 20.0);     // B1.9.6 c iii, d
    CHECK(rules.unservedStopAndGoSeconds == 30.0);
    CHECK(rules.blueFlagMarshalSectors == 3);
    CHECK(rules.overtakeDetectionGapSeconds == 1.0);
    CHECK(rules.minDryCompounds == 2);                    // B6.3.6
    CHECK(rules.twoCompoundNotRestartedSeconds == 30.0);
    CHECK(rules.timePenaltyServedAtNextStop);             // B1.9.6 b
    CHECK(rules.pitSpeedingDriveThroughFromKmh == defaults.pitSpeedingDriveThroughFromKmh);
    CHECK(rules.pitSpeedingStopAndGoAboveKmh == defaults.pitSpeedingStopAndGoAboveKmh);
    CHECK(rules.unsafeReleasePenaltySeconds == defaults.unsafeReleasePenaltySeconds);
    CHECK(rules.trackLimitsBlackWhiteAt == 3);
    CHECK(rules.trackLimitsPenaltyFrom == 4);
    CHECK(rules.trackLimitsPenaltySeconds == 5.0);
}

TEST_CASE("Invalid marshal sector is rejected") {
    Sim sim = makeRace(1, 0.0);
    Command c;
    c.type = Command::Type::SetSectorFlag;
    c.sector = sim.track().marshalSectorCount();
    CHECK_THROWS_AS(sim.command(c), std::invalid_argument);
}

TEST_CASE("VSC: no overtaking and road gaps hold") {
    Sim sim = makeRace(3, 0.0);
    runUntilLeaderLap(sim, 6);
    const auto before = leadLapDistanceGaps(sim.snapshot());
    const int overtakesBefore = sim.stats().overtakes;

    sim.command(simple(Command::Type::DeployVirtualSafetyCar));
    runUntilLeaderLap(sim, 9);
    CHECK(sim.snapshot().state == RaceState::VirtualSafetyCar);
    CHECK(sim.stats().overtakes == overtakesBefore);

    // Teams use the VSC for cheap stops (M7.4): compare only cars that stayed out,
    // with the gaps measured to the first of them.
    std::map<int, double> distanceBefore;
    for (const auto& [entry, gap] : before) {
        distanceBefore[entry] = -gap;
    }
    std::map<int, double> distanceAfter;
    for (const CarState& car : sim.snapshot().cars) {
        if (car.pitStops == 0 && !car.inPitLane && distanceBefore.count(car.entryIndex)) {
            distanceAfter[car.entryIndex] = car.distance;
        }
    }
    REQUIRE(distanceAfter.size() >= 2);
    const int reference = distanceAfter.begin()->first;
    double worst = 0.0;
    for (const auto& [entry, distance] : distanceAfter) {
        const double gapBefore = distanceBefore.at(entry) - distanceBefore.at(reference);
        const double gapAfter = distance - distanceAfter.at(reference);
        worst = std::max(worst, std::abs(gapAfter - gapBefore));
    }
    INFO("largest change of a road gap to the leader under VSC: ", worst, " m");
    CHECK(worst < 100.0); // < ~1 s at race pace; transitions into the VSC shift gaps a little

    sim.command(simple(Command::Type::EndVirtualSafetyCar));
    runToEnd(sim);
    CHECK(sim.stats().flagViolations == 0);
}

TEST_CASE("Safety Car: field closes up, no overtaking, racing resumes") {
    Sim sim = makeRace(3, 0.0);
    runUntilLeaderLap(sim, 6);
    const int overtakesBefore = sim.stats().overtakes;

    sim.command(simple(Command::Type::DeploySafetyCar));
    runUntilLeaderLap(sim, 9);
    const SimSnapshot& snap = sim.snapshot();
    REQUIRE(snap.state == RaceState::SafetyCar);
    REQUIRE(snap.safetyCarOnTrack);
    CHECK(sim.stats().overtakes == overtakesBefore);

    // One compact queue behind the Safety Car: sort by road distance behind it.
    std::vector<double> behind;
    for (const CarState& car : snap.cars) {
        if (!car.finished) {
            behind.push_back(sim.track().wrap(snap.safetyCarDistance - car.distance));
        }
    }
    std::sort(behind.begin(), behind.end());
    double largestGap = behind.front();
    for (std::size_t k = 1; k < behind.size(); ++k) {
        largestGap = std::max(largestGap, behind[k] - behind[k - 1]);
    }
    INFO("queue length ", behind.back(), " m, largest gap ", largestGap, " m");
    CHECK(largestGap < 30.0);

    sim.command(simple(Command::Type::RecallSafetyCar));
    for (long steps = 0; sim.snapshot().state != RaceState::Green && steps < 200'000; ++steps) {
        sim.step();
    }
    CHECK(sim.snapshot().state == RaceState::Green);
    CHECK_FALSE(sim.snapshot().safetyCarOnTrack);
    runToEnd(sim);
    CHECK(sim.stats().overtakes > overtakesBefore); // racing resumed after the restart
    CHECK(sim.stats().flagViolations == 0);
}

TEST_CASE("Red flag: cars stop in a queue, restart behind the Safety Car") {
    Sim sim = makeRace(3, 0.0);
    runUntilLeaderLap(sim, 6);
    const int overtakesBefore = sim.stats().overtakes;
    const int lapBefore = sim.snapshot().leaderLap;

    sim.command(simple(Command::Type::RedFlag));
    runSeconds(sim, 300.0);
    for (const CarState& car : sim.snapshot().cars) {
        CHECK(car.speed == 0.0);
    }
    CHECK(sim.snapshot().leaderLap <= lapBefore + 1);
    CHECK(sim.stats().overtakes == overtakesBefore);

    sim.command(simple(Command::Type::Restart));
    runSeconds(sim, 1.0);
    CHECK(sim.snapshot().state == RaceState::SafetyCar);
    CHECK(sim.snapshot().safetyCarInThisLap);
    runToEnd(sim);
    CHECK(sim.snapshot().cars.front().lapsCompleted == 30);
}

TEST_CASE("Yellow flags: no overtaking; rule-breakers are recorded") {
    auto allDoubleYellow = [](Sim& sim) {
        for (int s = 0; s < sim.track().marshalSectorCount(); ++s) {
            Command c;
            c.type = Command::Type::SetSectorFlag;
            c.sector = s;
            c.flag = Flag::DoubleYellow;
            sim.command(c);
        }
    };

    SUBCASE("disciplined drivers") {
        Sim sim = makeRace(4, 0.0);
        runUntilLeaderLap(sim, 3);
        const int overtakesBefore = sim.stats().overtakes;
        allDoubleYellow(sim);
        runUntilLeaderLap(sim, 10);
        CHECK(sim.stats().overtakes == overtakesBefore);
        CHECK(sim.stats().flagViolations == 0); // collisions may still happen
    }
    SUBCASE("every driver ignores the flags") {
        Sim sim = makeRace(4, 1.0);
        runUntilLeaderLap(sim, 3);
        allDoubleYellow(sim);
        runUntilLeaderLap(sim, 10);
        int yellowPasses = 0;
        for (const Incident& v : sim.incidents()) {
            if (v.type == Incident::Type::OvertakeUnderYellow) {
                ++yellowPasses;
                CHECK(v.car != v.otherCar);
                CHECK(v.blame == 1.0);
            }
        }
        CHECK(yellowPasses > 0);
        CHECK(sim.stats().flagViolations == yellowPasses);
    }
}

TEST_CASE("Blue flags: lapped cars let the leaders by; ignoring them is an incident") {
    auto race = [](double ignoreChance) {
        RaceConfig config;
        config.laps = 50;
        config.seed = 2;
        config.accidentChanceScale = 0.0; // keep the field intact
        config.followUpChanceScale = 0.0;
        config.blueFlagIgnoreChance = ignoreChance;
        config.rules = loadRules(dataDir() / "rules" / "fia-f1-2026.json");
        Sim sim(loadTrack(dataDir() / "tracks" / "testring.json"),
                loadEntries(dataDir() / "entries" / "fictional-grid.json"), config);
        bool sawBlue = false;
        for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
            sim.step();
            for (const CarState& car : sim.snapshot().cars) {
                sawBlue = sawBlue || car.blueFlag;
            }
        }
        REQUIRE(sim.finished());
        CHECK(sawBlue);
        return sim;
    };
    auto ignored = [](const Sim& sim) {
        int count = 0;
        for (const Incident& incident : sim.incidents()) {
            if (incident.type == Incident::Type::IgnoringBlueFlags) {
                ++count;
                CHECK(incident.blame == 1.0);
                CHECK(sim.snapshot().cars.size() > 0);
            }
        }
        return count;
    };

    const Sim disciplined = race(0.0);
    CHECK(ignored(disciplined) == 0);
    int lapped = 0;
    for (const CarState& car : disciplined.snapshot().cars) {
        lapped += car.lapsDown > 0 ? 1 : 0;
    }
    CHECK(lapped > 0); // the leaders did get past the backmarkers

    const Sim stubborn = race(1.0);
    CHECK(ignored(stubborn) > 0);
}

TEST_CASE("Overtake mode follows B7.2: start, detection gap, Safety Car") {
    Sim sim = makeRace(2, 0.0);
    sim.step();
    CHECK_FALSE(sim.snapshot().overtakeEnabled); // disabled at the start
    bool sawActive = false;
    std::map<int, bool> wasActive;
    for (long steps = 0; sim.snapshot().leaderLap < 6; ++steps) {
        sim.step();
        for (const CarState& car : sim.snapshot().cars) {
            if (car.overtakeActive && !wasActive[car.entryIndex]) {
                sawActive = true; // activated at the Activation Line ...
                CHECK(sim.snapshot().overtakeEnabled);
                CHECK(car.detectionGap < 1.0); // ... only if within the Detection Gap
            }
            wasActive[car.entryIndex] = car.overtakeActive;
        }
    }
    CHECK(sim.snapshot().overtakeEnabled); // enabled once the leader crossed the Detection Line
    CHECK(sawActive);

    sim.command(simple(Command::Type::DeploySafetyCar));
    sim.step();
    CHECK_FALSE(sim.snapshot().overtakeEnabled);
    for (const CarState& car : sim.snapshot().cars) {
        CHECK_FALSE(car.overtakeActive);
    }
    runUntilLeaderLap(sim, 8);
    sim.command(simple(Command::Type::RecallSafetyCar));
    for (long steps = 0; !sim.snapshot().overtakeEnabled && steps < 200'000; ++steps) {
        sim.step();
        if (sim.snapshot().overtakeEnabled) {
            CHECK(sim.snapshot().state == RaceState::Green);
        }
    }
    CHECK(sim.snapshot().overtakeEnabled); // back after everyone crossed the line behind the SC
}

TEST_CASE("Race-control commands keep the sim deterministic") {
    auto race = [](std::uint64_t seed) {
        Sim sim = makeRace(seed, 0.05);
        runUntilLeaderLap(sim, 4);
        sim.command(simple(Command::Type::DeploySafetyCar));
        runUntilLeaderLap(sim, 6);
        sim.command(simple(Command::Type::RecallSafetyCar));
        runUntilLeaderLap(sim, 12);
        sim.command(simple(Command::Type::DeployVirtualSafetyCar));
        runSeconds(sim, 60.0);
        sim.command(simple(Command::Type::EndVirtualSafetyCar));
        runToEnd(sim);
        return sim;
    };
    const Sim a = race(5);
    const Sim b = race(5);
    CHECK(a.snapshot().time == b.snapshot().time);
    CHECK(a.incidents().size() == b.incidents().size());
    for (std::size_t r = 0; r < a.snapshot().cars.size(); ++r) {
        CHECK(a.snapshot().cars[r].entryIndex == b.snapshot().cars[r].entryIndex);
        CHECK(a.snapshot().cars[r].lastLineTime == b.snapshot().cars[r].lastLineTime);
    }
}
