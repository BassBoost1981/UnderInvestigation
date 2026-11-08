#pragma once

#include <array>
#include <vector>

namespace uisim {

enum class Flag { Green, Yellow, DoubleYellow }; // per marshal sector
enum class RaceState { Green, VirtualSafetyCar, SafetyCar, RedFlag };
// Steward penalties, ordered by severity.
enum class Penalty { None, Warning, FiveSeconds, TenSeconds, DriveThrough, StopAndGo };

// Spin: stops briefly and rejoins. OffTrack: stranded beside the track, retired.
// Crash: stopped on the track with debris, retired.
enum class AccidentType { Spin, OffTrack, Crash };

// A stranded car (and its debris) that the marshals have to recover.
struct HazardState {
    int id = 0;               // = accident id
    AccidentType type = AccidentType::OffTrack;
    int car = 0;              // entry index
    double trackPosition = 0.0;
    int marshalSector = 0;
    bool onTrack = false;     // on the racing line: recovery needs double yellow, VSC, SC or red
    double recoveryLeft = 0.0; // seconds of marshal work still needed
    double waitingTime = 0.0;  // seconds the marshals had to wait for a sufficient flag
    bool recoveryRequested = false; // marshals have radioed for the flag they need
};

// Read-only state of one car after a sim step. Flat and copyable so the UE
// layer can keep two snapshots and interpolate between them.
struct CarState {
    int entryIndex = 0;   // index into Sim::entries()
    int position = 0;     // 1-based race position (timing-screen order)
    int gridPosition = 0; // 1-based
    int lapsCompleted = 0;
    double distance = 0.0;      // total metres since the line, negative on the grid
    double trackPosition = 0.0; // metres within the current lap, [0, track length)
    double speed = 0.0;         // m/s
    double lateral = 0.0;       // -1..1 of track width, 0 = racing line; below -1: pit lane
    double lastLapTime = 0.0;   // seconds, 0 = none yet
    double bestLapTime = 0.0;   // seconds, 0 = none yet
    std::array<double, 3> lastSectorTimes{};
    double lastLineTime = 0.0; // race time at the last timing line crossed (finish time once finished)
    double gapToLeader = 0.0; // seconds at the last timing line both crossed
    int lapsDown = 0;         // > 0: lapped, gapToLeader is then not meaningful
    double interval = 0.0;    // seconds to the car one position ahead
    double timePenalty = 0.0; // seconds added to the race time (5 s / 10 s verdicts)
    Penalty pendingPenalty = Penalty::None; // drive-through / stop-and-go not served yet
    bool inPitLane = false;
    bool spinning = false;
    bool retired = false; // out of the race (accident); classified after the finishers
    bool blueFlag = false; // being lapped: must let the faster car by
    bool finished = false;
};

struct SimSnapshot {
    double time = 0.0; // race time in seconds
    int leaderLap = 0; // lap the leader is on (1-based), capped at totalLaps
    int totalLaps = 0;
    bool chequered = false; // leader has finished
    bool finished = false;  // every car has taken the flag or retired
    RaceState state = RaceState::Green;
    std::vector<Flag> sectorFlags; // one per marshal sector
    bool safetyCarOnTrack = false;
    bool safetyCarInThisLap = false;
    double safetyCarDistance = 0.0; // metres, same scale as CarState::distance
    std::vector<HazardState> hazards; // not yet recovered
    std::vector<CarState> cars; // sorted by position
};

} // namespace uisim
