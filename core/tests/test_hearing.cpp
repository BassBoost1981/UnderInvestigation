// M11.4: hearings in the core: evidence from the recording, statements of the drivers, honesty.

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
