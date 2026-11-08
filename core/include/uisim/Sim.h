#pragma once

#include "uisim/Entry.h"
#include "uisim/Rng.h"
#include "uisim/Rules.h"
#include "uisim/Snapshot.h"
#include "uisim/Track.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace uisim {

struct RaceConfig {
    int laps = 50;
    std::uint64_t seed = 1;
    RaceRules rules{};
    // Chance that a driver attacks anyway when overtaking is not allowed
    // (yellow, VSC, SC, before the restart line). Creates violations to judge.
    double ruleBreakChance = 0.02;
    // Scales the chance that errors and collisions escalate into accidents (0 = none;
    // for tests and scenarios that place accidents themselves).
    double accidentChanceScale = 1.0;
    // Chance that a lapped driver ignores the blue flags for one lapping episode.
    double blueFlagIgnoreChance = 0.05;
    // Scales follow-up accidents and punctures at accident sites (M6.4); 0 isolates scenarios.
    double followUpChanceScale = 1.0;
};

struct RaceStats {
    int overtakes = 0;    // completed passes between cars on the same lap
    int overtakeAttempts = 0;
    int driverErrors = 0; // corner mistakes (time loss, no crash in M1)
    int flagViolations = 0;
    int collisions = 0;
    int spins = 0;
    int accidents = 0; // off-track and crashes (retirements)
    int followUpAccidents = 0; // spins/crashes at an insufficiently flagged accident site
    int punctures = 0;         // from debris
    int blueFlagsIgnored = 0;
    int trackLimitOffences = 0; // every time a car left the track (lap deleted)
};

// Race-control action, applied at the start of the next step.
struct Command {
    enum class Type {
        SetSectorFlag,
        DeployVirtualSafetyCar,
        EndVirtualSafetyCar,
        DeploySafetyCar,
        RecallSafetyCar, // "Safety Car in this lap"
        RedFlag,
        Restart,         // after a red flag: restart behind the Safety Car
        IssueVerdict,    // steward decision on an incident (one per incident)
        InjectAccident   // scenario/test tool: `car` has an accident of type `accident`
    };
    Type type = Type::SetSectorFlag;
    int sector = 0;          // SetSectorFlag only
    Flag flag = Flag::Green; // SetSectorFlag only
    int incidentId = 0;      // IssueVerdict only
    int car = -1;            // IssueVerdict: entry index penalised, -1 = no one; InjectAccident: car
    Penalty penalty = Penalty::None; // IssueVerdict only
    AccidentType accident = AccidentType::Spin; // InjectAccident only
};

struct Accident {
    int id = 0; // 1-based
    AccidentType type = AccidentType::Spin;
    double time = 0.0;
    int car = 0;            // entry index
    double distance = 0.0;  // total distance of the car
    bool fromCollision = false;
    int causedBy = 0; // follow-up: id of the unprotected accident the car ran into, 0 = none
    // Accident sites only (off-track, crash), for the safety rating:
    double protectedAfter = -1.0; // seconds until the site first had a sufficient flag, -1 = never
    double waitingTime = 0.0;     // seconds the recovery had to wait for that flag
};

// A steward decision as issued.
struct Verdict {
    int incidentId = 0;
    int car = -1; // entry index penalised, -1 = no one (no further action)
    Penalty penalty = Penalty::None;
    double time = 0.0;   // race time of the decision
    bool afterRace = false;
};

// Something race control may investigate. The ground truth (blame) is what
// really happened; the player never sees it, it is the basis for rating verdicts.
struct Incident {
    enum class Type {
        Collision,                // contact in a contested corner
        OvertakeUnderYellow,
        OvertakeUnderVsc,
        OvertakeUnderSafetyCar,
        OvertakeUnderRedFlag,
        OvertakeBeforeRestartLine,
        IgnoringBlueFlags,        // lapped car did not let the faster car by in time
        Accident,                 // spin/off-track/crash without contact (otherCar == car); review only, no one at fault
        PitLaneSpeeding,          // otherCar == car; measure = km/h over the limit (M7.5)
        UnsafeRelease,            // car released into the path of otherCar; measure = gap in metres (M7.5)
        TrackLimits               // otherCar == car; offence after the black-and-white flag, measure = offence number (M7.6)
    };
    int id = 0;          // 1-based, in order of occurrence
    Type type = Type::Collision;
    double time = 0.0;   // race time
    int car = 0;         // entry index: attacker / overtaking car
    int otherCar = 0;    // entry index: defender / car passed
    double distance = 0.0; // total distance of `car` at the incident
    double blame = 0.0;  // ground truth: share of fault of `car`, 0..1 (rest: otherCar)
    double measure = 0.0; // ground truth detail: speeding km/h over the limit, unsafe release gap in metres
};

// Marshal radio to race control (M6.5). Structured; the presentation layer words it.
struct RadioMessage {
    enum class Type {
        Spin,              // car spun, continuing
        CarOffTrack,       // car stopped beside the track, driver ok
        CarOnTrack,        // car stopped on the track, debris
        RecoveryNeedsFlag, // recovery ready, waiting for the flag it needs
        TrackClear         // car recovered, sector clear
    };
    double time = 0.0;  // race time when race control receives it
    Type type = Type::Spin;
    int marshalPost = 1; // 1-based = marshal sector + 1
    int car = 0;         // entry index
    int accidentId = 0;
    bool onTrack = false; // the car stands on the racing line (recovery needs double yellow)
};

// Recorded state per tick for replays (compact: ~16 bytes per car).
struct ReplayFrame {
    double time = 0.0;
    double safetyCarDistance = 0.0;
    bool safetyCarOnTrack = false;
};
struct ReplayCar {
    double distance = 0.0;
    float lateral = 0.0f;
    float speed = 0.0f;
};

// Deterministic race simulation with a fixed tick. Same track, entries,
// config and seed always produce the same race (see tests/test_sim.cpp).
class Sim {
public:
    static constexpr double kDt = 0.05; // 20 Hz

    // Throws std::invalid_argument for an empty field or laps < 1.
    Sim(Track track, std::vector<Entry> entries, RaceConfig config);

    void step();
    bool finished() const { return snapshot_.finished; }

    // Queue a race-control command. Throws std::invalid_argument for a bad sector index.
    void command(const Command& command);
    const std::vector<Incident>& incidents() const { return incidents_; }
    const std::vector<Verdict>& verdicts() const { return verdicts_; }
    const std::vector<Accident>& accidents() const { return accidents_; }
    const std::vector<RadioMessage>& radio() const { return radio_; } // marshal radio log, oldest first
    double neutralisedTime() const { return neutralisedTime_; } // seconds under VSC, SC or red flag
    double finishTime() const { return finishTime_; }           // race time when the last car finished

    // Replay: one frame per tick since the start. Car samples are indexed by entry.
    const std::vector<ReplayFrame>& replayFrames() const { return replayFrames_; }
    const ReplayCar& replayCar(std::size_t frame, int entryIndex) const {
        return replayCars_[frame * cars_.size() + static_cast<std::size_t>(entryIndex)];
    }
    // Index of the last frame at or before `time` (clamped to the recording).
    std::size_t replayFrameAt(double time) const;

    const SimSnapshot& snapshot() const { return snapshot_; }
    const RaceStats& stats() const { return stats_; }
    const Track& track() const { return track_; }
    // Metres from the line (negative = before it) of the pit box of an entry's team (M7.8).
    double boxOffset(int entryIndex) const { return cars_[static_cast<std::size_t>(entryIndex)].boxOffset; }
    const std::vector<Entry>& entries() const { return entries_; }

private:
    struct Car {
        double distance = 0.0;
        double speed = 0.0;
        double lateral = 0.0;
        double startDelay = 0.0; // reaction time at the start
        int gridPosition = 0;
        int segment = 0;
        double form = 1.0;      // re-rolled every lap
        double tyreWear = 0.0;  // laps on the current set
        Compound compound = Compound::Medium;
        double errorTimer = 0.0;
        int overtakeTarget = -1;
        double overtakeTimer = 0.0;
        double yieldTimer = 0.0; // lost a contested corner, backing off
        bool holdUntilLine = false; // after the Safety Car: no overtaking before the line
        double timePenalty = 0.0;
        Penalty pendingPenalty = Penalty::None; // drive-through / stop-and-go to serve
        bool inPit = false;
        bool stopDone = false;
        // Pit stops (M7.4)
        bool tyreStop = false;      // in the pit lane for new tyres
        int plannedStopLap = 0;     // stop at the end of this lap, INT_MAX = no further stop
        int stops = 0;
        unsigned compoundsUsed = 0; // bit per Compound (B6.3.6)
        double servablePenalty = 0.0; // 5 s / 10 s still to serve at the next stop (B1.9.6 b)
        double pitSpeedExcess = 0.0;  // km/h over the limit on this pit lane visit (limiter fault)
        bool releaseChecked = false;  // the team decided about traffic at the end of this stop
        int trackLimitStrikes = 0;    // offences counted by race control (M7.6)
        double boxOffset = 0.0;       // own team's pit box, metres from the line
        double stopTimer = 0.0;
        double spinTimer = 0.0; // > 0: spun, stationary
        bool retired = false;
        // Blue flags (M6.7): being lapped by `blueLapper`.
        int blueLapper = -1;
        bool blueIgnoring = false;
        bool blueReported = false;
        int blueSectors = 0;      // marshal sectors passed under blue
        int blueLastSector = -1;
        // Overtake mode (M6.8).
        double lastDetectionTime = -1e9;
        double detectionGap = 1e9;
        bool overtakeEligible = false;
        bool overtakeActive = false;
        // Timing: sector boundaries are crossed in order s1, s2, line.
        int boundaryIndex = 0;
        int lapsCompleted = 0;
        double lastBoundaryTime = 0.0;
        std::vector<double> boundaryTimes; // crossing time of every boundary so far
        std::array<double, 3> sectorTimes{};
        std::array<double, 3> lastSectorTimes{};
        double lastLapTime = 0.0;
        double bestLapTime = 0.0;
        bool finished = false;
    };

    struct SafetyCar {
        bool onTrack = false;
        bool inThisLap = false;
        double distance = 0.0;
        double speed = 0.0;
    };

    void applyCommands();
    void deploySafetyCar(bool inThisLap);
    void cancelAttacks();
    void moveSafetyCar();
    bool overtakingAllowed(int car) const;
    void recordViolation(int car, int passedCar);
    void addIncident(Incident::Type type, int car, int otherCar, double blame);
    void applyVerdict(const Command& command);
    void startAccident(int car, AccidentType type, bool fromCollision, int causedBy = 0);
    void checkHazardPasses(const std::vector<int>& cars, const std::vector<double>& previousDistance);
    void maybeAccident(int car, double crashChance, double offTrackChance, double spinChance, bool fromCollision);
    void updateHazards();
    void updateBlueFlags(const std::vector<int>& order);
    void updateOvertakeMode(const std::vector<int>& racing, const std::vector<int>& ahead, const std::vector<double>& previous);
    bool yieldsTo(int lapped, int lapper) const;
    bool recoveryAllowed(const HazardState& hazard) const;
    bool outOfRace(int car) const { return cars_[car].finished || cars_[car].retired; }
    void movePitCar(int car);
    Compound chooseCompound(int car) const; // for the rest of the race, respecting B6.3.6
    void fitTyres(int car, Compound compound);
    void planNextStop(int car);
    bool wantsTyreStop(int car) const;
    void releaseFromBox(int car);
    void convertUnservedPenalties();
    void recordReplayFrame();
    void radioReport(RadioMessage::Type type, int car, int accidentId, double distance, double delay, bool onTrack);
    void deliverRadio();
    double paceFactor(int car) const; // performance or the neutralised share of the reference speed
    Flag flagAt(double distance) const { return sectorFlags_[track_.marshalSectorAt(distance)]; }
    double performance(int car) const;
    double speedCap(int car, double gapAhead) const; // free-track potential in the current segment
    double desiredSpeed(int car, double cap) const;
    void processTimingLines(int car, double previousDistance);
    void onSegmentEntered(int car);
    void buildSnapshot();
    double rollForm();

    Track track_;
    std::vector<Entry> entries_;
    RaceConfig config_;
    Pcg32 rng_;
    std::vector<Car> cars_;
    std::array<double, 3> boundaries_{}; // s1 start, s2 start, lap length
    double time_ = 0.0;
    bool chequered_ = false;
    RaceState state_ = RaceState::Green;
    SafetyCar safetyCar_;
    std::vector<Flag> sectorFlags_;
    std::vector<Command> pending_;
    std::vector<Incident> incidents_;
    std::vector<Verdict> verdicts_;
    std::vector<Accident> accidents_;
    struct SiteMemory {
        int passes = 0;    // cars that passed the site under green: drivers know about it
        int followUps = 0; // follow-up accidents it caused
    };
    std::vector<SiteMemory> siteMemory_; // indexed by hazard id
    std::vector<HazardState> hazards_;
    bool overtakeEnabled_ = false;
    bool overtakeAfterSafetyCar_ = false; // re-enable once everyone crossed the line after the SC
    std::vector<RadioMessage> radio_;
    std::vector<RadioMessage> pendingRadio_; // reports still "on their way" (marshal delay)
    double neutralisedTime_ = 0.0;
    double finishTime_ = 0.0;
    std::vector<ReplayFrame> replayFrames_;
    std::vector<ReplayCar> replayCars_;
    RaceStats stats_;
    SimSnapshot snapshot_;
};

} // namespace uisim
