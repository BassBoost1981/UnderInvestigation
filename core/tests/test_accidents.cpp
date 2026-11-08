// M6.3 acceptance: accidents, stranded cars, recovery and retirements (docs/DESIGN.md 8.1).

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Rating.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

using namespace uisim;

namespace {

// Scenario races place their accidents themselves (accidentChanceScale 0) and, unless a test
// is about them, have no follow-up accidents either; the rate test uses the real values.
Sim makeRace(std::uint64_t seed, int laps = 20, double accidentChanceScale = 0.0, double followUpChanceScale = 0.0) {
    const std::filesystem::path data(UISIM_DATA_DIR);
    RaceConfig config;
    config.laps = laps;
    config.seed = seed;
    config.accidentChanceScale = accidentChanceScale;
    config.followUpChanceScale = followUpChanceScale;
    config.rules = loadRules(data / "rules" / "fia-f1-2026.json");
    return Sim(loadTrack(data / "tracks" / "testring.json"), loadEntries(data / "entries" / "fictional-grid.json"), config);
}

void runSeconds(Sim& sim, double seconds) {
    for (long s = 0; s < static_cast<long>(seconds / Sim::kDt) && !sim.finished(); ++s) {
        sim.step();
    }
}

void runToEnd(Sim& sim) {
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
}

void inject(Sim& sim, int car, AccidentType type) {
    Command c;
    c.type = Command::Type::InjectAccident;
    c.car = car;
    c.accident = type;
    sim.command(c);
    sim.step();
}

void setFlag(Sim& sim, int sector, Flag flag) {
    Command c;
    c.type = Command::Type::SetSectorFlag;
    c.sector = sector;
    c.flag = flag;
    sim.command(c);
}

const CarState& stateOf(const Sim& sim, int entry) {
    for (const CarState& car : sim.snapshot().cars) {
        if (car.entryIndex == entry) {
            return car;
        }
    }
    FAIL("entry not found");
    return sim.snapshot().cars.front();
}

// A car in mid-field after a few laps, away from the start/finish straight.
int victim(const Sim& sim) { return sim.snapshot().cars[8].entryIndex; }

} // namespace

TEST_CASE("A crash leaves a stopped car on the track that the field drives around") {
    Sim sim = makeRace(3);
    runSeconds(sim, 400.0);
    const int car = victim(sim);
    inject(sim, car, AccidentType::Crash);

    REQUIRE(sim.snapshot().hazards.size() == 1);
    const HazardState hazard = sim.snapshot().hazards.front();
    CHECK(hazard.car == car);
    CHECK(hazard.onTrack);
    CHECK(stateOf(sim, car).retired);
    const double where = stateOf(sim, car).distance;

    // The car stays exactly where it stopped, the race goes on around it.
    runSeconds(sim, 120.0);
    CHECK(stateOf(sim, car).distance == where);
    CHECK(stateOf(sim, car).speed == 0.0);
    CHECK(sim.snapshot().cars.back().entryIndex == car); // classified last
    REQUIRE(sim.snapshot().hazards.size() == 1);
    CHECK(sim.snapshot().hazards.front().trackPosition == hazard.trackPosition);
}

TEST_CASE("Recovery on the track waits for double yellow, VSC or Safety Car") {
    Sim sim = makeRace(3);
    runSeconds(sim, 400.0);
    inject(sim, victim(sim), AccidentType::Crash);
    const int sector = sim.snapshot().hazards.front().marshalSector;

    SUBCASE("green: the marshals have to wait") {
        runSeconds(sim, 300.0);
        REQUIRE(sim.snapshot().hazards.size() == 1);
        CHECK(sim.snapshot().hazards.front().waitingTime == doctest::Approx(300.0).epsilon(0.01));
    }
    SUBCASE("single yellow is not enough for a car on the track") {
        setFlag(sim, sector, Flag::Yellow);
        runSeconds(sim, 300.0);
        CHECK(sim.snapshot().hazards.size() == 1);
    }
    SUBCASE("double yellow: recovered within the recovery time") {
        setFlag(sim, sector, Flag::DoubleYellow);
        runSeconds(sim, 185.0);
        CHECK(sim.snapshot().hazards.empty());
    }
    SUBCASE("VSC: recovered as well") {
        Command vsc;
        vsc.type = Command::Type::DeployVirtualSafetyCar;
        sim.command(vsc);
        runSeconds(sim, 185.0);
        CHECK(sim.snapshot().hazards.empty());
    }
}

TEST_CASE("A car stranded beside the track needs at least a single yellow") {
    Sim sim = makeRace(3);
    runSeconds(sim, 400.0);
    inject(sim, victim(sim), AccidentType::OffTrack);
    REQUIRE(sim.snapshot().hazards.size() == 1);
    CHECK_FALSE(sim.snapshot().hazards.front().onTrack);
    const int sector = sim.snapshot().hazards.front().marshalSector;

    runSeconds(sim, 150.0);
    CHECK(sim.snapshot().hazards.size() == 1); // green: waiting
    setFlag(sim, sector, Flag::Yellow);
    runSeconds(sim, 125.0);
    CHECK(sim.snapshot().hazards.empty());
}

TEST_CASE("A spun car stops, rejoins and finishes") {
    Sim sim = makeRace(3);
    runSeconds(sim, 400.0);
    const int car = victim(sim);
    inject(sim, car, AccidentType::Spin);
    CHECK(stateOf(sim, car).spinning);
    CHECK(stateOf(sim, car).speed == 0.0);
    CHECK(sim.snapshot().hazards.empty());
    runSeconds(sim, 7.0);
    CHECK_FALSE(stateOf(sim, car).spinning);
    runToEnd(sim);
    CHECK(stateOf(sim, car).finished);
    CHECK_FALSE(stateOf(sim, car).retired);
}

TEST_CASE("Retired cars are classified after the finishers and the race still ends") {
    Sim sim = makeRace(3);
    runSeconds(sim, 400.0);
    const int car = victim(sim);
    inject(sim, car, AccidentType::OffTrack);
    runToEnd(sim);
    const auto& cars = sim.snapshot().cars;
    CHECK(cars.back().entryIndex == car);
    CHECK(cars.back().retired);
    CHECK(cars.back().lapsDown > 0);
    for (std::size_t r = 0; r + 1 < cars.size(); ++r) {
        CHECK(cars[r].finished);
    }
}

TEST_CASE("Accident rate per race is in the target range") {
    // Target (2026-11-08): 0.5-3 retirements (off-track or crash) and 1-6 spins per 50-lap race on average.
    constexpr int kRaces = 10;
    int accidents = 0;
    int spins = 0;
    for (int seed = 1; seed <= kRaces; ++seed) {
        Sim sim = makeRace(static_cast<std::uint64_t>(seed), 50, 1.0, 1.0);
        runToEnd(sim);
        // Primary accidents only: without a race director nobody flags, so follow-ups pile up.
        for (const Accident& a : sim.accidents()) {
            if (a.causedBy == 0) {
                (a.type == AccidentType::Spin ? spins : accidents) += 1;
            }
        }
    }
    INFO("accidents per race ", accidents / double(kRaces), ", spins per race ", spins / double(kRaces));
    CHECK(accidents >= 5);
    CHECK(accidents <= 30);
    CHECK(spins >= 10);
    CHECK(spins <= 60);
}

TEST_CASE("Unprotected accident sites cause follow-up accidents, flags prevent them") {
    // Same scenario over many seeds: a crash on the racing line, then 5 minutes of racing.
    auto followUps = [](bool yellow, bool doubleYellow) {
        int total = 0;
        for (std::uint64_t seed = 1; seed <= 15; ++seed) {
            Sim sim = makeRace(seed, 20, 0.0, 1.0); // no random accidents; follow-ups active
            runSeconds(sim, 400.0);
            inject(sim, victim(sim), AccidentType::Crash);
            const int sector = sim.snapshot().hazards.front().marshalSector;
            if (yellow || doubleYellow) {
                setFlag(sim, sector, doubleYellow ? Flag::DoubleYellow : Flag::Yellow);
            }
            runSeconds(sim, 300.0);
            total += sim.stats().followUpAccidents;
            for (const Accident& a : sim.accidents()) {
                if (a.causedBy != 0) {
                    CHECK(a.causedBy < a.id); // linked to an earlier site (follow-ups can chain)
                }
            }
        }
        return total;
    };
    const int none = followUps(false, false);
    const int yellow = followUps(true, false);
    const int doubleYellow = followUps(false, true);
    INFO("follow-up accidents in 15 scenarios: no flag ", none, ", yellow ", yellow, ", double yellow ", doubleYellow);
    CHECK(none >= 10);
    CHECK(yellow < none);
    CHECK(doubleYellow * 4 < none);
}

TEST_CASE("Marshals radio every accident with place and situation") {
    Sim sim = makeRace(3);
    runSeconds(sim, 400.0);
    const int car = victim(sim);
    inject(sim, car, AccidentType::Crash);
    const HazardState hazard = sim.snapshot().hazards.front();

    runSeconds(sim, 4.0);
    CHECK(sim.radio().empty()); // the report takes a few seconds
    runSeconds(sim, 2.0);
    REQUIRE(sim.radio().size() == 1);
    const RadioMessage& report = sim.radio().front();
    CHECK(report.type == RadioMessage::Type::CarOnTrack);
    CHECK(report.car == car);
    CHECK(report.accidentId == hazard.id);
    CHECK(report.marshalPost == hazard.marshalSector + 1);

    runSeconds(sim, 15.0); // still green: marshals ask for the flag they need
    REQUIRE(sim.radio().size() == 2);
    CHECK(sim.radio()[1].type == RadioMessage::Type::RecoveryNeedsFlag);

    setFlag(sim, hazard.marshalSector, Flag::DoubleYellow);
    runSeconds(sim, 185.0);
    REQUIRE(sim.radio().size() == 3);
    CHECK(sim.radio()[2].type == RadioMessage::Type::TrackClear);
    CHECK(sim.radio()[2].accidentId == hazard.id);
}

TEST_CASE("Spins and off-track excursions are reported too") {
    Sim sim = makeRace(3);
    runSeconds(sim, 400.0);
    inject(sim, sim.snapshot().cars[4].entryIndex, AccidentType::Spin);
    inject(sim, sim.snapshot().cars[12].entryIndex, AccidentType::OffTrack);
    runSeconds(sim, 6.0);
    REQUIRE(sim.radio().size() == 2);
    CHECK(sim.radio()[0].type == RadioMessage::Type::Spin);
    CHECK(sim.radio()[1].type == RadioMessage::Type::CarOffTrack);
}

TEST_CASE("Safety rating rewards a quick, sufficient flag") {
    // Crash on the racing line, double yellow after `reactAfter` seconds, then race to the end.
    auto safetyAfter = [](double reactAfter) {
        Sim sim = makeRace(3);
        runSeconds(sim, 400.0);
        inject(sim, victim(sim), AccidentType::Crash);
        runSeconds(sim, reactAfter);
        setFlag(sim, sim.snapshot().hazards.front().marshalSector, Flag::DoubleYellow);
        runToEnd(sim);
        const RaceRating rating = rateRace(sim);
        CHECK(rating.accidentSites == 1);
        CHECK(rating.overall == doctest::Approx(100.0 * (0.35 * rating.fairness + 0.15 * rating.consistency +
                                                        0.3 * rating.safety + 0.2 * rating.raceQuality)));
        return rating.safety;
    };
    CHECK(safetyAfter(10.0) == doctest::Approx(1.0));
    CHECK(safetyAfter(70.0) == doctest::Approx(0.5).epsilon(0.02));
    CHECK(safetyAfter(300.0) == doctest::Approx(0.0));
}

TEST_CASE("Safety rating counts follow-up accidents") {
    int followUps = 0;
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
        Sim sim = makeRace(seed, 20, 0.0, 1.0); // follow-ups active
        runSeconds(sim, 400.0);
        inject(sim, victim(sim), AccidentType::Crash);
        runSeconds(sim, 600.0); // nobody reacts
        const RaceRating rating = rateRace(sim);
        CHECK(rating.followUpAccidents == sim.stats().followUpAccidents);
        CHECK(rating.safety == doctest::Approx(0.0)); // never protected
        followUps += rating.followUpAccidents;
    }
    CHECK(followUps > 0);
}

TEST_CASE("Accidents keep the sim deterministic") {
    auto race = [] {
        Sim sim = makeRace(4);
        runSeconds(sim, 300.0);
        inject(sim, victim(sim), AccidentType::Crash);
        runSeconds(sim, 60.0);
        Command sc;
        sc.type = Command::Type::DeploySafetyCar;
        sim.command(sc);
        runToEnd(sim);
        return sim;
    };
    const Sim a = race();
    const Sim b = race();
    CHECK(a.snapshot().time == b.snapshot().time);
    CHECK(a.accidents().size() == b.accidents().size());
    for (std::size_t r = 0; r < a.snapshot().cars.size(); ++r) {
        CHECK(a.snapshot().cars[r].entryIndex == b.snapshot().cars[r].entryIndex);
    }
}

TEST_CASE("An accident without contact is a review entry that only counts when decided") {
    Sim sim = makeRace(3, 5);
    runSeconds(sim, 400.0);
    const int car = victim(sim);
    const std::size_t before = sim.incidents().size();
    inject(sim, car, AccidentType::Spin);
    REQUIRE(sim.incidents().size() == before + 1);
    const Incident accident = sim.incidents().back();
    CHECK(accident.type == Incident::Type::Accident);
    CHECK(accident.car == car);
    CHECK(accident.otherCar == car);
    CHECK(expectedVerdict(accident).car == -1);

    Sim penalised = sim; // same race, the stewards wrongly penalise the spinner
    Command c;
    c.type = Command::Type::IssueVerdict;
    c.incidentId = accident.id;
    c.car = car;
    c.penalty = Penalty::TenSeconds;
    penalised.command(c);
    runToEnd(sim);
    runToEnd(penalised);
    // Left alone the spin does not dilute fairness; a wrong penalty lowers it.
    CHECK(rateRace(penalised).fairness < rateRace(sim).fairness);
}

TEST_CASE("Without any reaction follow-up accidents do not wreck the race") {
    // Real accident and follow-up rates, nobody sets a flag: on average at most 4 retirements.
    int retired = 0;
    constexpr int kRaces = 10;
    for (std::uint64_t seed = 1; seed <= kRaces; ++seed) {
        Sim sim = makeRace(seed, 50, 1.0, 1.0);
        runToEnd(sim);
        for (const CarState& car : sim.snapshot().cars) {
            retired += car.retired ? 1 : 0;
        }
    }
    INFO("retirements in ", kRaces, " races: ", retired);
    CHECK(retired <= 4 * kRaces);
}
