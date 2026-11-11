#pragma once

#include "uisim/Tyres.h"

#include <array>
#include <vector>

namespace uisim {

enum class Flag { Green, Yellow, DoubleYellow }; // per marshal sector
enum class RaceState { Green, VirtualSafetyCar, SafetyCar, RedFlag };
// Steward penalties, ordered by severity.
enum class Penalty { None, Warning, FiveSeconds, TenSeconds, DriveThrough, StopAndGo, Disqualification };

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
    bool recovering = false;        // marshals at work right now
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
    bool disqualified = false; // removed from the results, listed last (M8.3)
    bool blueFlag = false; // being lapped: must let the faster car by
    bool overtakeActive = false; // Overtake mode (B7.2) usable this lap
    Compound compound = Compound::Medium;
    int tyreAge = 0; // laps on the current set
    int pitStops = 0;
    int compoundsUsed = 0; // number of different dry compounds so far
    int trackLimitStrikes = 0;
    bool blackWhiteFlag = false; // shown after the warning offence (track limits)
    bool unlapping = false;      // lapped car passing the queue and the Safety Car (B5.13.4 c)
    bool stalled = false;        // immobilised on the grid before the start (B5.7, M11.3)
    double detectionGap = 0.0;   // seconds behind the car ahead at the last Detection Line crossing
    bool finished = false;
};

struct SimSnapshot {
    double time = 0.0; // race time in seconds
    double rain = 0.0;         // 0 dry .. 1 downpour (M9.2)
    double trackWetness = 0.0; // 0..1, off the racing line
    double lineWetness = 0.0;  // 0..1, on the racing line (dries first)
    double standingWater = 0.0; // 0..1, aquaplaning risk (M9.4)
    bool wetTyresMandated = false; // wet-weather tyres compulsory behind the SC (B6.3.7)
    int leaderLap = 0; // lap the leader is on (1-based), capped at totalLaps
    int totalLaps = 0;
    bool chequered = false; // leader has finished
    bool finished = false;  // every car has taken the flag or retired
    RaceState state = RaceState::Green;
    std::vector<Flag> sectorFlags; // one per marshal sector
    bool safetyCarOnTrack = false;
    bool pitEntryClosed = false; // B1.6.4 (M8.3)
    bool gridStart = false; // standing start: cars forming on the grid or waiting for the signal (M10.3)
    bool formationLap = false; // the formation lap before the start is being driven (B5.6, M11.3)
    int startLights = 0;    // red lights lit on the gantry, all out = start signal (B5.7.1)
    bool safetyCarInThisLap = false;
    double safetyCarDistance = 0.0; // metres, same scale as CarState::distance
    bool lappedCarsReleased = false; // "LAPPED CARS MAY NOW OVERTAKE" sent in this SC period
    std::vector<HazardState> hazards; // not yet recovered
    bool overtakeEnabled = false;     // Overtake mode enabled by race control rules (B7.2.2)
    std::vector<CarState> cars; // sorted by position
};

} // namespace uisim
