// Console runner: simulates a full race without graphics and prints the result.
// Usage: uisim_runner [--laps N] [--seed S] [--track file.json] [--entries file.json] [--weather file.json]
//        uisim_runner --season file.json [--seed S] [--entries file.json] [--career save.json] [--races N]
//                     [--verdicts none|expected]   (M10.4/M10.5: a season, optionally continued from a save file)

#include "uisim/DataDir.h"
#include "uisim/Career.h"
#include "uisim/DataLoader.h"
#include "uisim/Rating.h"
#include "uisim/Season.h"
#include "uisim/Sim.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

using namespace uisim;

namespace {

std::string formatTime(double seconds) {
    const int hours = static_cast<int>(seconds / 3600.0);
    const int minutes = static_cast<int>((seconds - hours * 3600.0) / 60.0);
    const double rest = seconds - hours * 3600.0 - minutes * 60.0;
    char buffer[32];
    if (hours > 0) {
        std::snprintf(buffer, sizeof buffer, "%d:%02d:%06.3f", hours, minutes, rest);
    } else {
        std::snprintf(buffer, sizeof buffer, "%d:%06.3f", minutes, rest);
    }
    return buffer;
}

void printUsage() {
    std::printf("Usage: uisim_runner [--laps N] [--seed S] [--track file.json] [--entries file.json] [--weather file.json]\n");
    std::printf("       uisim_runner --season file.json [--seed S] [--entries file.json] [--career save.json] [--races N]\n");
    std::printf("                    [--verdicts none|expected]\n");
}

void printStandings(const char* title, const std::vector<ChampionshipLine>& lines, std::size_t count) {
    std::printf("%s:", title);
    const std::vector<int> order = standings(lines);
    for (std::size_t k = 0; k < order.size() && k < count; ++k) {
        const ChampionshipLine& line = lines[order[k]];
        std::printf("%s %zu. %s %d", k == 0 ? "" : ",", k + 1, line.name.c_str(), line.points);
    }
    std::printf("\n");
}

void printNames(const char* title, const std::vector<int>& list, const std::vector<Entry>& entries) {
    if (list.empty()) {
        return;
    }
    std::printf("%s:", title);
    for (std::size_t k = 0; k < list.size(); ++k) {
        std::printf("%s %s", k == 0 ? "" : ",", entries[list[k]].name.c_str());
    }
    std::printf("\n");
}

// Runs the races of the calendar as a career and prints the championship after each one.
// With a career file the season continues from it and is saved after every race (M10.5).
// `expectedVerdicts`: a stand-in player gives every incident the verdict the ground truth asks for.
int runSeason(const std::filesystem::path& seasonPath, const std::filesystem::path& entriesPath,
              const std::filesystem::path& dataDir, std::uint64_t seed, const std::filesystem::path& careerPath,
              int maxRaces, bool expectedVerdicts) {
    const SeasonCalendar calendar = loadSeason(seasonPath);
    const std::vector<Entry> entries = loadEntries(entriesPath);
    const RaceRules rules = loadRules(dataDir / "rules" / "fia-f1-2026.json");
    const bool resume = !careerPath.empty() && std::filesystem::exists(careerPath);
    Career career = resume ? loadCareer(careerPath, calendar, entries) : newCareer(calendar, entries, seed);
    std::printf("%s - %zu races, seed %llu\n", calendar.name.c_str(), calendar.races.size(),
                static_cast<unsigned long long>(career.seed));
    if (career.nextRace > 0) {
        std::printf("Continuing after race %d\n", career.nextRace);
    }
    for (int run = 0; !careerFinished(career, calendar) && (maxRaces <= 0 || run < maxRaces); ++run) {
        const int k = career.nextRace;
        Sim sim = makeCareerRace(career, calendar, dataDir, entries, rules);
        const int laps = sim.snapshot().totalLaps;
        const long maxSteps = static_cast<long>(laps) * 600 * 20;
        std::size_t judged = 0;
        auto judge = [&] {
            for (; expectedVerdicts && judged < sim.incidents().size(); ++judged) {
                const Incident& incident = sim.incidents()[judged];
                const ExpectedVerdict expected = expectedVerdict(incident);
                Command c;
                c.type = Command::Type::IssueVerdict;
                c.incidentId = incident.id;
                c.car = expected.car;
                c.penalty = expected.penalty;
                sim.command(c);
            }
        };
        for (long steps = 0; !sim.finished() && steps < maxSteps; ++steps) {
            sim.step();
            judge();
        }
        if (!sim.finished()) {
            std::fprintf(stderr, "Race %d did not finish within the step limit\n", k + 1);
            return 1;
        }
        judge();
        sim.step(); // verdicts on incidents raised at the flag still count
        finishCareerRace(career, calendar, sim, entries, rules);
        if (!careerPath.empty()) {
            saveCareer(careerPath, career);
        }
        const CareerRace& race = career.races.back();
        std::printf("\nRace %d: %s (%s, %d laps, %.1f km)\n", k + 1, race.name.c_str(), sim.track().name().c_str(), laps,
                    laps * sim.track().length() / 1000.0);
        for (std::size_t p = 0; p < race.podium.size(); ++p) {
            std::printf("  %zu. %-18s %s\n", p + 1, entries[race.podium[p]].name.c_str(), entries[race.podium[p]].team.c_str());
        }
        std::printf("  Rating %.1f (incidents %d, decided %d)\n", race.overall, race.incidents, race.decided);
        printNames("  Sat out", race.suspended, entries);
        printNames("  Suspended for the next race", career.suspended, entries);
        printStandings("  Drivers", career.championship.drivers, 5);
        printStandings("  Teams", career.championship.teams, 3);
    }
    std::printf("\nStandings after %d of %zu races\n", career.nextRace, calendar.races.size());
    printStandings("Drivers", career.championship.drivers, career.championship.drivers.size());
    printStandings("Teams", career.championship.teams, career.championship.teams.size());
    std::printf("Race director rating: %.1f\n", careerRating(career));
    std::printf("Penalty points:");
    bool any = false;
    for (std::size_t entry = 0; entry < entries.size(); ++entry) {
        if (career.penaltyPoints[entry] > 0) {
            std::printf("%s %s %d", any ? "," : "", entries[entry].name.c_str(), career.penaltyPoints[entry]);
            any = true;
        }
    }
    std::printf("%s\n", any ? "" : " none");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    RaceConfig config;
    const std::filesystem::path dataDir(UISIM_DATA_DIR);
    std::filesystem::path trackPath = dataDir / "tracks" / "testring.json";
    std::filesystem::path entriesPath = dataDir / "entries" / "fictional-grid.json";
    std::filesystem::path weatherPath; // empty = dry
    std::filesystem::path seasonPath;  // empty = a single race
    std::filesystem::path careerPath;  // season only: save file to continue from and to write
    int maxRaces = 0;                  // season only: races to run now, 0 = all that are left
    bool expectedVerdicts = false;     // season only: a stand-in player judges every incident

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (i + 1 >= argc) {
            std::fprintf(stderr, "Missing value for %s\n", arg.c_str());
            printUsage();
            return 2;
        }
        const std::string value = argv[++i];
        try {
            if (arg == "--laps") {
                config.laps = std::stoi(value);
            } else if (arg == "--seed") {
                config.seed = std::stoull(value);
            } else if (arg == "--track") {
                trackPath = value;
            } else if (arg == "--entries") {
                entriesPath = value;
            } else if (arg == "--weather") {
                weatherPath = value;
            } else if (arg == "--season") {
                seasonPath = value;
            } else if (arg == "--career") {
                careerPath = value;
            } else if (arg == "--races") {
                maxRaces = std::stoi(value);
            } else if (arg == "--verdicts") {
                if (value != "none" && value != "expected") {
                    throw std::invalid_argument("verdicts");
                }
                expectedVerdicts = value == "expected";
            } else {
                std::fprintf(stderr, "Unknown option %s\n", arg.c_str());
                printUsage();
                return 2;
            }
        } catch (const std::exception&) {
            std::fprintf(stderr, "Invalid value '%s' for %s\n", value.c_str(), arg.c_str());
            return 2;
        }
    }

    try {
        if (!seasonPath.empty()) {
            return runSeason(seasonPath, entriesPath, dataDir, config.seed, careerPath, maxRaces, expectedVerdicts);
        }
        const auto start = std::chrono::steady_clock::now();
        if (!weatherPath.empty()) {
            config.weather = loadWeather(weatherPath);
        }
        Sim sim(loadTrack(trackPath), loadEntries(entriesPath), config);
        const long maxSteps = static_cast<long>(config.laps) * 600 * 20;
        long steps = 0;
        while (!sim.finished() && steps < maxSteps) {
            sim.step();
            ++steps;
        }
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        const SimSnapshot& snap = sim.snapshot();
        std::printf("%s - %d laps (%.3f km), seed %llu\n\n", sim.track().name().c_str(), config.laps,
                    sim.track().length() / 1000.0, static_cast<unsigned long long>(config.seed));
        std::printf("%-4s %-4s %-4s %-18s %-20s %-5s %-5s %-12s %s\n",
                    "Pos", "No", "Code", "Driver", "Team", "Grid", "Laps", "Time/Gap", "Best lap");

        const CarState* fastest = nullptr;
        for (const CarState& car : snap.cars) {
            const Entry& entry = sim.entries()[car.entryIndex];
            std::string result;
            if (car.disqualified) {
                result = "DSQ";
            } else if (car.position == 1) {
                result = formatTime(car.lastLineTime);
            } else if (car.retired) {
                result = "DNF";
            } else if (car.lapsDown > 0) {
                result = "+" + std::to_string(car.lapsDown) + (car.lapsDown == 1 ? " lap" : " laps");
            } else {
                char buffer[32];
                std::snprintf(buffer, sizeof buffer, "+%.3f", car.gapToLeader);
                result = buffer;
            }
            std::printf("%-4d %-4d %-4s %-18s %-20s %-5d %-5d %-12s %s\n", car.position, entry.number,
                        entry.code.c_str(), entry.name.c_str(), entry.team.c_str(), car.gridPosition,
                        car.lapsCompleted, result.c_str(), formatTime(car.bestLapTime).c_str());
            if (car.bestLapTime > 0.0 && (!fastest || car.bestLapTime < fastest->bestLapTime)) {
                fastest = &car;
            }
        }

        if (fastest) {
            std::printf("\nFastest lap: %s %s\n", sim.entries()[fastest->entryIndex].name.c_str(),
                        formatTime(fastest->bestLapTime).c_str());
        }
        std::printf("Overtakes: %d (attempts %d), driver errors: %d\n", sim.stats().overtakes,
                    sim.stats().overtakeAttempts, sim.stats().driverErrors);
        std::printf("Incidents: %zu (collisions %d, flag violations %d)\n", sim.incidents().size(),
                    sim.stats().collisions, sim.stats().flagViolations);
        std::printf("Accidents: %d (spins %d)\n", sim.stats().accidents, sim.stats().spins);
        std::printf("Simulated %s of race time in %.3f s (%ld ticks at %.0f Hz)\n",
                    formatTime(snap.time).c_str(), wall, steps, 1.0 / Sim::kDt);

        if (!sim.finished()) {
            std::fprintf(stderr, "Race did not finish within the step limit\n");
            return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
