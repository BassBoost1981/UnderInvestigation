// M11.4: hearings in the core: evidence from the recording, statements of the drivers, honesty.

#include "uisim/Career.h"
#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Hearing.h"
#include "uisim/Rating.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace uisim;

namespace {

const std::filesystem::path kData(UISIM_DATA_DIR);

// A full race in which every driver has the given honesty.
Sim finishedRace(std::uint64_t seed, double honesty, int laps = 30) {
    std::vector<Entry> entries = loadEntries(kData / "entries" / "fictional-grid.json");
    for (Entry& entry : entries) {
        entry.honesty = honesty;
    }
    RaceConfig config;
    config.laps = laps;
    config.seed = seed;
    Sim sim(loadTrack(kData / "tracks" / "testring.json"), entries, config);
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
    return sim;
}

int contradictions(const Hearing& hearing, const Statement& statement) {
    return static_cast<int>(std::count_if(statement.claims.begin(), statement.claims.end(),
        [&](const Claim& claim) { return contradictsEvidence(hearing, claim); }));
}

bool atFault(const Incident& incident, int car) {
    const ExpectedVerdict expected = expectedVerdict(incident);
    return expected.car == car && expected.penalty != Penalty::None;
}

} // namespace

TEST_CASE("The evidence of a collision is what the blame came from") {
    int collisions = 0;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        const Sim sim = finishedRace(seed, 1.0);
        for (const Incident& incident : sim.incidents()) {
            const Hearing hearing = makeHearing(sim, incident);
            CHECK(hearing.incidentId == incident.id);
            if (incident.type == Incident::Type::Accident) {
                CHECK(hearing.evidence.empty()); // nobody to hear
                CHECK(hearing.statements.empty());
                continue;
            }
            REQUIRE_FALSE(hearing.evidence.empty());
            if (incident.type != Incident::Type::Collision) {
                REQUIRE(hearing.statements.size() == 1);
                CHECK(hearing.statements[0].car == incident.car);
                continue;
            }
            ++collisions;
            REQUIRE(hearing.evidence.size() == 3);
            const double gap = hearing.evidence[0].value;
            const double lateBraking = hearing.evidence[1].value;
            const double defenderMove = hearing.evidence[2].value;
            CHECK(hearing.evidence[0].kind == EvidenceKind::TurnInGap);
            CHECK(gap >= 0.0);
            // The recording explains the ground truth: further back and later on the brakes is on the
            // attacker, a defender moving across takes blame away (see the collision in Sim.cpp).
            const double blame = std::clamp(0.3 + 0.08 * gap + 0.4 * (lateBraking / 30.0 - defenderMove / 2.5), 0.0, 1.0);
            CHECK(blame == doctest::Approx(incident.blame));
            REQUIRE(hearing.statements.size() == 2);
            CHECK(hearing.statements[0].car == incident.car);
            CHECK(hearing.statements[1].car == incident.otherCar);
            for (const Statement& statement : hearing.statements) {
                CHECK(statement.claims.size() == hearing.evidence.size());
            }
        }
    }
    CHECK(collisions >= 5);
}

TEST_CASE("An honest driver never contradicts the evidence") {
    int statements = 0;
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        const Sim sim = finishedRace(seed, 1.0);
        for (const Incident& incident : sim.incidents()) {
            const Hearing hearing = makeHearing(sim, incident);
            for (const Statement& statement : hearing.statements) {
                CHECK(contradictions(hearing, statement) == 0);
                for (std::size_t k = 0; k < statement.claims.size(); ++k) {
                    CHECK(statement.claims[k].kind == hearing.evidence[k].kind);
                    CHECK(statement.claims[k].value >= 0.0);
                }
                ++statements;
            }
        }
    }
    CHECK(statements >= 20);
}

TEST_CASE("A dishonest driver at fault contradicts the evidence; the innocent side does not") {
    int atFaultStatements = 0;
    int innocentStatements = 0;
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        const Sim sim = finishedRace(seed, 0.0); // nobody tells the truth when it costs him
        for (const Incident& incident : sim.incidents()) {
            const Hearing hearing = makeHearing(sim, incident);
            for (const Statement& statement : hearing.statements) {
                if (atFault(incident, statement.car)) {
                    CHECK(contradictions(hearing, statement) >= 1); // every lie can be checked against an item
                    ++atFaultStatements;
                } else {
                    CHECK(contradictions(hearing, statement) == 0); // nothing to gain
                    ++innocentStatements;
                }
            }
        }
    }
    CHECK(atFaultStatements >= 15);
    CHECK(innocentStatements >= 3);
}

TEST_CASE("Honesty is the share of true statements when at fault") {
    int atFaultStatements = 0;
    int lies = 0;
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        const Sim sim = finishedRace(seed, 0.6);
        for (const Incident& incident : sim.incidents()) {
            const Hearing hearing = makeHearing(sim, incident);
            for (const Statement& statement : hearing.statements) {
                if (atFault(incident, statement.car)) {
                    ++atFaultStatements;
                    lies += contradictions(hearing, statement) > 0 ? 1 : 0;
                }
            }
        }
    }
    INFO("statements at fault ", atFaultStatements, ", with a contradiction ", lies);
    REQUIRE(atFaultStatements >= 60);
    CHECK(lies > atFaultStatements * 0.25); // 40 % expected
    CHECK(lies < atFaultStatements * 0.55);
}

TEST_CASE("The same race gives the same hearing, and asking does not change the race") {
    const Sim a = finishedRace(3, 0.5);
    const Sim b = finishedRace(3, 0.5);
    REQUIRE(a.incidents().size() == b.incidents().size());
    REQUIRE_FALSE(a.incidents().empty());
    for (std::size_t k = 0; k < a.incidents().size(); ++k) {
        const Hearing first = makeHearing(a, a.incidents()[k]);
        const Hearing again = makeHearing(a, a.incidents()[k]);
        const Hearing other = makeHearing(b, b.incidents()[k]);
        REQUIRE(first.statements.size() == again.statements.size());
        REQUIRE(first.statements.size() == other.statements.size());
        for (std::size_t s = 0; s < first.statements.size(); ++s) {
            for (std::size_t c = 0; c < first.statements[s].claims.size(); ++c) {
                CHECK(first.statements[s].claims[c].value == again.statements[s].claims[c].value);
                CHECK(first.statements[s].claims[c].value == other.statements[s].claims[c].value);
            }
        }
    }
    // Honesty is data: the entry list carries it, the default applies without it.
    const std::vector<Entry> entries = loadEntries(kData / "entries" / "fictional-grid.json");
    bool varied = false;
    for (const Entry& entry : entries) {
        CHECK(entry.honesty >= 0.0);
        CHECK(entry.honesty <= 1.0);
        varied = varied || entry.honesty != entries.front().honesty;
    }
    CHECK(varied);
    CHECK(Entry{}.honesty == 0.7);
}

// ---- M11.5: the hearing as a command, the right of review

namespace {

Command hearing(int incidentId) {
    Command c;
    c.type = Command::Type::HoldHearing;
    c.incidentId = incidentId;
    return c;
}

Command verdict(int incidentId, int car, Penalty penalty) {
    Command c;
    c.type = Command::Type::IssueVerdict;
    c.incidentId = incidentId;
    c.car = car;
    c.penalty = penalty;
    return c;
}

// The first collision of the race for which one driver should be penalised; -1 if there is none.
int clearCollision(const Sim& sim) {
    for (std::size_t k = 0; k < sim.incidents().size(); ++k) {
        const Incident& incident = sim.incidents()[k];
        if (incident.type == Incident::Type::Collision && expectedVerdict(incident).car >= 0) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

// A finished race that has such a collision.
Sim raceWithClearCollision(std::uint64_t* seedOut = nullptr) {
    for (std::uint64_t seed = 1; seed < 40; ++seed) {
        Sim sim = finishedRace(seed, 1.0, 20);
        if (clearCollision(sim) >= 0) {
            if (seedOut) {
                *seedOut = seed;
            }
            return sim;
        }
    }
    FAIL("no race with a clear collision");
    return finishedRace(1, 1.0, 20);
}

} // namespace

TEST_CASE("A hearing during the race keeps the verdict waiting; after the flag there is time") {
    std::vector<Entry> entries = loadEntries(kData / "entries" / "fictional-grid.json");
    RaceConfig config;
    config.laps = 30;
    config.seed = 1;
    Sim sim(loadTrack(kData / "tracks" / "testring.json"), entries, config);
    for (long steps = 0; sim.incidents().empty() && !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE_FALSE(sim.incidents().empty());
    REQUIRE_FALSE(sim.finished());
    const Incident first = sim.incidents().front();
    CHECK_FALSE(first.heard);
    sim.command(hearing(first.id));
    sim.command(verdict(first.id, first.car, Penalty::FiveSeconds)); // too early: the drivers are still being heard
    sim.step();
    CHECK(sim.incidents().front().heard);
    CHECK(sim.verdicts().empty());
    for (int k = 0; k < 20 * 170; ++k) {
        sim.step();
    }
    sim.command(verdict(first.id, first.car, Penalty::FiveSeconds)); // 170 s later: still too early
    sim.step();
    CHECK(sim.verdicts().empty());
    for (int k = 0; k < 20 * 15; ++k) {
        sim.step();
    }
    sim.command(verdict(first.id, first.car, Penalty::FiveSeconds)); // after three minutes
    sim.step();
    REQUIRE(sim.verdicts().size() == 1);
    CHECK(sim.verdicts().front().incidentId == first.id);
    CHECK_FALSE(sim.verdicts().front().afterRace);

    // After the flag hearing and verdict go together.
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
    REQUIRE(sim.incidents().size() >= 2);
    const Incident last = sim.incidents().back();
    sim.command(hearing(last.id));
    sim.command(verdict(last.id, -1, Penalty::None));
    sim.step();
    CHECK(sim.verdicts().size() == 2);
    CHECK(sim.incidents().back().heard);
}

TEST_CASE("Right of review: a clearly wrong verdict without a hearing is set aside, after a hearing it stands") {
    Sim unheard = raceWithClearCollision();
    Sim heard = raceWithClearCollision();
    Sim right = raceWithClearCollision();
    const int k = clearCollision(unheard);
    const Incident incident = unheard.incidents()[k];
    const ExpectedVerdict expected = expectedVerdict(incident);
    const int wrongCar = expected.car == incident.car ? incident.otherCar : incident.car;

    unheard.command(verdict(incident.id, wrongCar, Penalty::TenSeconds)); // the wrong driver, without a hearing
    unheard.step();
    heard.command(hearing(incident.id));
    heard.command(verdict(incident.id, wrongCar, Penalty::TenSeconds)); // the same verdict, after hearing both
    heard.step();
    right.command(verdict(incident.id, expected.car, expected.penalty)); // the right verdict, without a hearing
    right.step();

    const RaceRating a = rateRace(unheard);
    const RaceRating b = rateRace(heard);
    const RaceRating c = rateRace(right);
    CHECK(a.incidents[k].overturned);
    CHECK(a.overturned == 1);
    CHECK_FALSE(b.incidents[k].overturned); // nothing new: they had seen the evidence
    CHECK(b.incidents[k].heard);
    CHECK(b.overturned == 0);
    CHECK_FALSE(c.incidents[k].overturned); // a right verdict gives nobody a reason
    CHECK(c.overturned == 0);
    CHECK(a.fairness == b.fairness); // equally wrong ...
    CHECK(a.overall == doctest::Approx(b.overall - 5.0)); // ... but being set aside costs on top
    CHECK(c.overall > b.overall);
}

TEST_CASE("Right of review in the career: the penalty points follow the verdict as it stands") {
    const std::vector<Entry> entries = loadEntries(kData / "entries" / "fictional-grid.json");
    const RaceRules rules = loadRules(kData / "rules" / "fia-f1-2026.json");
    SeasonCalendar calendar;
    calendar.name = "Review test";
    SeasonRace race;
    race.name = "Race 1";
    race.track = "testring.json";
    race.laps = 20;
    calendar.races.push_back(race);

    for (std::uint64_t seed = 1; seed < 40; ++seed) {
        Career withoutHearing = newCareer(calendar, entries, seed);
        Sim sim = makeCareerRace(withoutHearing, calendar, kData, entries, rules);
        for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
            sim.step();
        }
        REQUIRE(sim.finished());
        const int k = clearCollision(sim);
        if (k < 0) {
            continue; // this season's race has no clear collision: try the next seed
        }
        const Incident incident = sim.incidents()[k];
        const ExpectedVerdict expected = expectedVerdict(incident);
        const int wrongCar = expected.car == incident.car ? incident.otherCar : incident.car;

        Career withHearing = withoutHearing;
        Sim heard = makeCareerRace(withHearing, calendar, kData, entries, rules);
        for (long steps = 0; !heard.finished() && steps < 2'000'000; ++steps) {
            heard.step();
        }
        sim.command(verdict(incident.id, wrongCar, Penalty::TenSeconds));
        sim.step();
        heard.command(hearing(incident.id));
        heard.command(verdict(incident.id, wrongCar, Penalty::TenSeconds));
        heard.step();
        finishCareerRace(withoutHearing, calendar, sim, entries, rules);
        finishCareerRace(withHearing, calendar, heard, entries, rules);

        // Set aside: the points go to the driver who was to blame, not to the one penalised.
        CHECK(withoutHearing.races[0].overturned == 1);
        CHECK(withoutHearing.penaltyPoints[wrongCar] == 0);
        CHECK(withoutHearing.penaltyPoints[expected.car] == rules.penaltyPointsCollision);
        // After a hearing the verdict stands, wrong as it is.
        CHECK(withHearing.races[0].overturned == 0);
        CHECK(withHearing.penaltyPoints[wrongCar] == rules.penaltyPointsCollision);
        CHECK(withHearing.penaltyPoints[expected.car] == 0);
        CHECK(withoutHearing.races[0].overall < withHearing.races[0].overall);
        return;
    }
    FAIL("no race with a clear collision");
}
