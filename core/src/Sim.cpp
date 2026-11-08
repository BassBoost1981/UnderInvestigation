#include "uisim/Sim.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace uisim {
namespace {

// Car model (kinematic, see docs/DESIGN.md 5.2). Tuned for ~1:30 laps on a 5.6 km track.
constexpr double kBrake = 38.0;       // m/s^2
constexpr double kGridSpacing = 8.0;  // metres between grid slots
constexpr double kGridOffset = 5.0;   // pole position behind the line
constexpr double kQualifyingSpread = 0.010; // grid noise: qualifying is not race pace
// Drag-limited acceleration: falls to zero at the car's own top speed, which
// scales with performance and slipstream. Without this, pace and slipstream
// would not matter on straights that end before the car reaches its limit.
constexpr double kMaxAcceleration = 16.0; // m/s^2 at standstill
constexpr double kCarTopSpeed = 340.0 / 3.6;
double acceleration(double v, double topSpeed) {
    return std::max(0.0, kMaxAcceleration * (1.0 - v / topSpeed));
}

// Car interaction.
constexpr double kFollowGap = 12.0;   // closer than this: blocked unless overtaking
constexpr double kMinGap = 3.0;       // hard separation
constexpr double kSlipstreamGap = 40.0;
constexpr double kSlipstreamBonus = 0.02; // top-speed gain right behind another car
constexpr double kOvertakeBoost = 0.03;    // Overtake mode (B7.2): extra electrical power on the straights
constexpr double kDirtyAirGap = 25.0;
constexpr double kDirtyAirFactor = 0.99;  // corner-speed loss right behind another car
constexpr double kFastSegment = 250.0 / 3.6;
constexpr double kOvertakeAdvantageScale = 1.0; // m/s of potential for the full chance
constexpr double kOvertakeTimeout = 8.0;
constexpr double kOvertakeLateral = 0.6;
constexpr double kLateBrakeFactor = 1.15; // an attacker brakes later than usual
constexpr double kAlongsideGap = 5.0;     // at corner entry: closer = alongside, corner is contested
constexpr double kYieldTime = 2.5;        // defender who lost the corner backs off this long
constexpr double kYieldFactor = 0.95;
constexpr double kContactChance = 0.08;   // per contested corner, times (attack + defend aggression)
constexpr double kContactDuration = 3.0;  // both cars lose speed this long after contact

// Driver errors.
constexpr double kSlowCorner = 200.0 / 3.6;
constexpr double kErrorChance = 0.002; // per slow corner entry
constexpr double kErrorDuration = 1.5;
constexpr double kErrorSpeedFactor = 0.7;

// Race control (M3).
constexpr double kSafetyCarLead = 150.0;      // Safety Car joins this far ahead of the race leader
constexpr double kPitEntryBeforeLine = 300.0; // Safety Car leaves the track here when "in this lap"
constexpr double kRedFlagStopOffset = 2.0;    // red flag: cars stop this far before the line

// Penalties (M5). Pit lane runs alongside the main straight, from the pit entry
// (kPitEntryBeforeLine before the line) to the exit after the line.
constexpr double kPitLateral = -5.0;          // snapshot lateral units: 20 m right of the racing line
constexpr double kPitExitAfterLine = 300.0;

// Accidents (M6.3). Chances per driver error / per collision (for the car that was hit).
constexpr double kErrorCrashChance = 0.02;
constexpr double kErrorOffTrackChance = 0.04;
constexpr double kErrorSpinChance = 0.15;
constexpr double kCollisionCrashChance = 0.08;
constexpr double kCollisionSpinChance = 0.20;
constexpr double kSpinMin = 3.0, kSpinMax = 6.0;                    // seconds stationary
constexpr double kOffTrackRecoveryMin = 60.0, kOffTrackRecoveryMax = 120.0;
constexpr double kCrashRecoveryMin = 120.0, kCrashRecoveryMax = 180.0;
constexpr double kOffTrackLateral = 2.5;  // snapshot units: 10 m beside the racing line
constexpr double kCrashLateral = 0.3;
constexpr double kSpinLateral = 0.5;
constexpr double kHazardSightDistance = 40.0; // drivers lift when they see a car on the track
constexpr double kHazardCautionFactor = 0.6;
constexpr double kRadioReportDelay = 5.0;      // marshal post to race control
constexpr double kRecoveryRequestAfter = 15.0; // marshals radio for a flag after waiting this long

// Blue flags (M6.7).
constexpr double kBlueFlagRange = 80.0;       // metres: a lapping car this close behind gets a blue flag shown
constexpr double kBlueYieldFactor = 0.93;     // the lapped car lifts to let it by

// Follow-up accidents (M6.4): chance per car passing an accident site, before flag and speed factors.
constexpr double kPassRiskCrashSite = 0.04;    // stopped car on the racing line
constexpr double kPassRiskOffTrackSite = 0.01; // stranded beside the track
constexpr double kPassRiskYellow = 0.3;        // single yellow: drivers are warned
constexpr double kPassRiskDoubleYellow = 0.05; // double yellow: ready to stop
constexpr double kFollowUpCrashShare = 0.35;   // rest are spins
constexpr double kPassRiskReferenceSpeed = 70.0; // m/s; faster passes are riskier
constexpr double kPunctureChance = 0.02;       // per pass over crash debris at racing speed
constexpr double kPunctureDuration = 20.0;     // limping, seconds

// Long-run effects (no pit stops in M1).
constexpr double kTyreLossPerLap = 0.0006;
constexpr double kFuelStartFactor = 0.985;
constexpr double kFormSpread = 0.002;

} // namespace

Sim::Sim(Track track, std::vector<Entry> entries, RaceConfig config)
    : track_(std::move(track)), entries_(std::move(entries)), config_(config), rng_(config.seed) {
    if (entries_.empty()) {
        throw std::invalid_argument("Sim needs at least one entry");
    }
    if (config_.laps < 1) {
        throw std::invalid_argument("Race needs at least one lap");
    }
    boundaries_ = {track_.sectorStart(1), track_.sectorStart(2), track_.length()};

    // Grid by expected pace with a little noise (stand-in for qualifying).
    const int n = static_cast<int>(entries_.size());
    std::vector<std::pair<double, int>> rating;
    for (int i = 0; i < n; ++i) {
        rating.emplace_back(entries_[i].teamPace * entries_[i].skill +
                                rng_.uniform(-kQualifyingSpread, kQualifyingSpread), i);
    }
    std::sort(rating.begin(), rating.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });

    cars_.resize(n);
    for (int slot = 0; slot < n; ++slot) {
        Car& car = cars_[rating[slot].second];
        car.gridPosition = slot + 1;
        car.distance = -(kGridOffset + kGridSpacing * slot);
        car.segment = track_.segmentIndexAt(track_.wrap(car.distance));
        car.startDelay = rng_.uniform(0.2, 0.5);
        car.form = rollForm();
    }
    sectorFlags_.assign(track_.marshalSectorCount(), Flag::Green);
    buildSnapshot();
    recordReplayFrame();
}

void Sim::command(const Command& command) {
    if (command.type == Command::Type::SetSectorFlag &&
        (command.sector < 0 || command.sector >= track_.marshalSectorCount())) {
        throw std::invalid_argument("Marshal sector " + std::to_string(command.sector) + " does not exist");
    }
    pending_.push_back(command);
}

void Sim::applyCommands() {
    using Type = Command::Type;
    for (const Command& c : pending_) {
        switch (c.type) {
        case Type::SetSectorFlag:
            sectorFlags_[c.sector] = c.flag;
            break;
        case Type::DeployVirtualSafetyCar:
            if (state_ == RaceState::Green) {
                state_ = RaceState::VirtualSafetyCar;
                cancelAttacks();
            }
            break;
        case Type::EndVirtualSafetyCar:
            if (state_ == RaceState::VirtualSafetyCar) {
                state_ = RaceState::Green;
            }
            break;
        case Type::DeploySafetyCar:
            if (state_ == RaceState::Green || state_ == RaceState::VirtualSafetyCar) {
                deploySafetyCar(false);
            }
            break;
        case Type::RecallSafetyCar:
            if (state_ == RaceState::SafetyCar) {
                safetyCar_.inThisLap = true;
            }
            break;
        case Type::RedFlag:
            if (state_ != RaceState::RedFlag) {
                state_ = RaceState::RedFlag;
                safetyCar_ = SafetyCar{};
                overtakeEnabled_ = false; // re-enabled after the restart behind the Safety Car
                for (Car& car : cars_) {
                    car.overtakeActive = false;
                }
                cancelAttacks();
                if (config_.rules.redFlagFreeTyreChange) {
                    for (Car& car : cars_) {
                        car.tyreWear = 0.0;
                    }
                }
            }
            break;
        case Type::Restart:
            if (state_ == RaceState::RedFlag) {
                deploySafetyCar(true); // one lap behind the Safety Car, then racing
            }
            break;
        case Type::IssueVerdict:
            applyVerdict(c);
            break;
        case Type::InjectAccident:
            if (c.car >= 0 && c.car < static_cast<int>(cars_.size()) && !outOfRace(c.car) && cars_[c.car].spinTimer <= 0.0) {
                startAccident(c.car, c.accident, false);
            }
            break;
        }
    }
    pending_.clear();
}

void Sim::applyVerdict(const Command& c) {
    const bool known = c.incidentId >= 1 && c.incidentId <= static_cast<int>(incidents_.size());
    const bool decided = std::any_of(verdicts_.begin(), verdicts_.end(),
        [&](const Verdict& v) { return v.incidentId == c.incidentId; });
    if (!known || decided || c.car < -1 || c.car >= static_cast<int>(cars_.size())) {
        return; // one decision per incident
    }
    const bool raceOver = snapshot_.finished;
    verdicts_.push_back({c.incidentId, c.car, c.penalty, time_, raceOver});
    if (c.car < 0) {
        return;
    }
    Car& car = cars_[c.car];
    switch (c.penalty) {
    case Penalty::FiveSeconds:
        car.timePenalty += 5.0;
        break;
    case Penalty::TenSeconds:
        car.timePenalty += 10.0;
        break;
    case Penalty::DriveThrough:
    case Penalty::StopAndGo:
        // Served at the next pit entry; after the flag (or if one is already pending) it becomes time.
        if (car.finished || car.pendingPenalty != Penalty::None) {
            car.timePenalty += c.penalty == Penalty::DriveThrough ? config_.rules.unservedDriveThroughSeconds
                                                               : config_.rules.unservedStopAndGoSeconds;
        } else {
            car.pendingPenalty = c.penalty;
        }
        break;
    case Penalty::None:
    case Penalty::Warning:
        break;
    }
}

void Sim::maybeAccident(int i, double crashChance, double offTrackChance, double spinChance, bool fromCollision) {
    if (outOfRace(i) || cars_[i].spinTimer > 0.0 || cars_[i].inPit) {
        return;
    }
    const double roll = rng_.uniform() / std::max(config_.accidentChanceScale, 1e-12);
    if (roll < crashChance) {
        startAccident(i, AccidentType::Crash, fromCollision);
    } else if (roll < crashChance + offTrackChance) {
        startAccident(i, AccidentType::OffTrack, fromCollision);
    } else if (roll < crashChance + offTrackChance + spinChance) {
        startAccident(i, AccidentType::Spin, fromCollision);
    }
}

void Sim::checkHazardPasses(const std::vector<int>& racing, const std::vector<double>& previousDistance) {
    const std::size_t sites = hazards_.size(); // follow-ups added below are not passed this tick
    for (int i : racing) {
        Car& car = cars_[i];
        if (outOfRace(i) || car.inPit || car.spinTimer > 0.0) {
            continue;
        }
        const double from = track_.wrap(previousDistance[i]);
        const double to = track_.wrap(car.distance);
        for (std::size_t h = 0; h < sites && !outOfRace(i) && car.spinTimer <= 0.0; ++h) {
            const HazardState hazard = hazards_[h];
            const double at = hazard.trackPosition;
            const bool passed = from <= to ? (from <= at && at < to) : (at >= from || at < to);
            if (!passed || hazard.car == i || state_ != RaceState::Green) {
                continue; // VSC, Safety Car and red flag protect every site
            }
            const Flag flag = sectorFlags_[hazard.marshalSector];
            const double flagFactor = flag == Flag::DoubleYellow ? kPassRiskDoubleYellow
                                    : flag == Flag::Yellow       ? kPassRiskYellow
                                                                 : 1.0;
            const double speedFactor = std::clamp(car.speed / kPassRiskReferenceSpeed, 0.3, 1.3);
            const double risk = (hazard.onTrack ? kPassRiskCrashSite : kPassRiskOffTrackSite) * flagFactor *
                                speedFactor * config_.followUpChanceScale;
            if (rng_.uniform() < risk) {
                ++stats_.followUpAccidents;
                const AccidentType type = rng_.uniform() < kFollowUpCrashShare ? AccidentType::Crash : AccidentType::Spin;
                startAccident(i, type, false, hazard.id);
            } else if (hazard.onTrack &&
                       rng_.uniform() < kPunctureChance * flagFactor * speedFactor * config_.followUpChanceScale) {
                ++stats_.punctures; // debris
                car.errorTimer = std::max(car.errorTimer, kPunctureDuration);
            }
        }
    }
}

void Sim::startAccident(int i, AccidentType type, bool fromCollision, int causedBy) {
    Car& car = cars_[i];
    Accident accident;
    accident.id = static_cast<int>(accidents_.size()) + 1;
    accident.type = type;
    accident.time = time_;
    accident.car = i;
    accident.distance = car.distance;
    accident.fromCollision = fromCollision;
    accident.causedBy = causedBy;
    accidents_.push_back(accident);

    car.speed = 0.0;
    car.overtakeTarget = -1;
    for (Car& other : cars_) {
        if (other.overtakeTarget == i) {
            other.overtakeTarget = -1; // nothing left to attack
        }
    }
    if (type == AccidentType::Spin) {
        car.spinTimer = rng_.uniform(kSpinMin, kSpinMax);
        car.lateral = kSpinLateral;
        ++stats_.spins;
        radioReport(RadioMessage::Type::Spin, i, accident.id, car.distance, kRadioReportDelay, false);
        return;
    }

    car.retired = true;
    ++stats_.accidents;
    HazardState hazard;
    hazard.id = accident.id;
    hazard.type = type;
    hazard.car = i;
    hazard.trackPosition = track_.wrap(car.distance);
    hazard.marshalSector = track_.marshalSectorAt(car.distance);
    hazard.onTrack = type == AccidentType::Crash;
    if (hazard.onTrack) {
        hazard.recoveryLeft = rng_.uniform(kCrashRecoveryMin, kCrashRecoveryMax);
        car.lateral = kCrashLateral;
    } else {
        hazard.recoveryLeft = rng_.uniform(kOffTrackRecoveryMin, kOffTrackRecoveryMax);
        car.lateral = rng_.uniform() < 0.5 ? kOffTrackLateral : -kOffTrackLateral;
    }
    hazards_.push_back(hazard);
    radioReport(hazard.onTrack ? RadioMessage::Type::CarOnTrack : RadioMessage::Type::CarOffTrack, i, accident.id,
                car.distance, kRadioReportDelay, hazard.onTrack);
}

void Sim::radioReport(RadioMessage::Type type, int car, int accidentId, double distance, double delay, bool onTrack) {
    RadioMessage message;
    message.time = time_ + delay;
    message.type = type;
    message.marshalPost = track_.marshalSectorAt(distance) + 1;
    message.car = car;
    message.accidentId = accidentId;
    message.onTrack = onTrack;
    pendingRadio_.push_back(message);
}

void Sim::deliverRadio() {
    // Pending reports are queued in time order per delay; deliver everything that is due.
    std::stable_sort(pendingRadio_.begin(), pendingRadio_.end(),
                     [](const RadioMessage& a, const RadioMessage& b) { return a.time < b.time; });
    std::size_t due = 0;
    while (due < pendingRadio_.size() && pendingRadio_[due].time <= time_) {
        radio_.push_back(pendingRadio_[due++]);
    }
    pendingRadio_.erase(pendingRadio_.begin(), pendingRadio_.begin() + static_cast<std::ptrdiff_t>(due));
}

bool Sim::recoveryAllowed(const HazardState& hazard) const {
    if (state_ != RaceState::Green) {
        return true; // VSC, Safety Car or red flag
    }
    const Flag flag = sectorFlags_[hazard.marshalSector];
    return hazard.onTrack ? flag == Flag::DoubleYellow : flag != Flag::Green;
}

void Sim::updateHazards() {
    for (HazardState& hazard : hazards_) {
        Accident& accident = accidents_[static_cast<std::size_t>(hazard.id - 1)];
        accident.waitingTime = hazard.waitingTime;
        if (recoveryAllowed(hazard) && accident.protectedAfter < 0.0) {
            accident.protectedAfter = time_ - accident.time; // first moment the site is protected
        }
        hazard.recovering = recoveryAllowed(hazard);
        if (hazard.recovering) {
            hazard.recoveryLeft -= kDt;
        } else {
            hazard.waitingTime += kDt; // marshals cannot go out yet
            if (!hazard.recoveryRequested && hazard.waitingTime >= kRecoveryRequestAfter) {
                hazard.recoveryRequested = true;
                radioReport(RadioMessage::Type::RecoveryNeedsFlag, hazard.car, hazard.id,
                            hazard.trackPosition, 0.0, hazard.onTrack);
            }
        }
        if (hazard.recoveryLeft <= 0.0) {
            radioReport(RadioMessage::Type::TrackClear, hazard.car, hazard.id, hazard.trackPosition, 0.0, hazard.onTrack);
        }
    }
    hazards_.erase(std::remove_if(hazards_.begin(), hazards_.end(),
                                  [](const HazardState& h) { return h.recoveryLeft <= 0.0; }),
                   hazards_.end());
}

void Sim::updateOvertakeMode(const std::vector<int>& racing, const std::vector<int>& ahead,
                             const std::vector<double>& previous) {
    const double detection = track_.overtakeDetectionLine();
    const double activation = track_.overtakeActivationLine();
    if (detection < 0.0 || activation < 0.0) {
        return; // track without Overtake lines
    }
    // B7.2.2 c: after the Safety Car, enabled once every car has crossed the Control Line.
    if (overtakeAfterSafetyCar_ && state_ == RaceState::Green &&
        std::none_of(cars_.begin(), cars_.end(), [](const Car& c) { return c.holdUntilLine && !c.finished && !c.retired; })) {
        overtakeAfterSafetyCar_ = false;
        overtakeEnabled_ = true;
    }
    int leader = -1;
    for (int i : racing) {
        if (leader < 0 || cars_[i].distance > cars_[leader].distance) {
            leader = i;
        }
    }
    auto crossed = [&](int i, double line) {
        const double from = track_.wrap(previous[i]);
        const double to = track_.wrap(cars_[i].distance);
        return from <= to ? (from < line && line <= to) : (line > from || line <= to);
    };
    for (int i : racing) {
        Car& car = cars_[i];
        if (car.inPit || car.spinTimer > 0.0 || car.retired) {
            continue;
        }
        if (crossed(i, detection)) {
            // Gap in time to the car ahead on the road when both crossed the Detection Line.
            const int a = ahead[i];
            car.detectionGap = a >= 0 ? time_ - cars_[a].lastDetectionTime : 1e9;
            car.lastDetectionTime = time_;
            car.overtakeEligible = car.detectionGap < config_.rules.overtakeDetectionGapSeconds;
            if (i == leader && !overtakeEnabled_ && !overtakeAfterSafetyCar_ && state_ == RaceState::Green) {
                overtakeEnabled_ = true; // B7.2.2 b: after the start, once the leader crossed the Detection Line
            }
        }
        if (crossed(i, activation)) {
            car.overtakeActive = overtakeEnabled_ && car.overtakeEligible; // B7.2.3 c
        }
    }
}

bool Sim::yieldsTo(int lapped, int lapper) const {
    const Car& car = cars_[lapped];
    return car.blueLapper == lapper && !car.blueIgnoring;
}

void Sim::updateBlueFlags(const std::vector<int>& order) {
    const int m = static_cast<int>(order.size());
    const double lapLength = track_.length();
    for (int k = 0; k < m; ++k) {
        Car& car = cars_[order[k]];
        int lapper = -1;
        if (m > 1 && state_ == RaceState::Green) {
            const int behind = order[(k - 1 + m) % m]; // physically behind on the road
            const Car& other = cars_[behind];
            const bool close = track_.wrap(car.distance - other.distance) < kBlueFlagRange;
            const bool lapping = other.distance - car.distance > lapLength / 2.0; // at least a lap ahead in the race
            if (close && lapping) {
                lapper = behind;
            }
        }
        if (lapper < 0) {
            car.blueLapper = -1;
            continue;
        }
        const int sector = track_.marshalSectorAt(car.distance);
        if (car.blueLapper != lapper) { // a new blue flag episode
            car.blueLapper = lapper;
            car.blueIgnoring = rng_.uniform() < config_.blueFlagIgnoreChance;
            car.blueReported = false;
            car.blueSectors = 0;
            car.blueLastSector = sector;
        } else if (sector != car.blueLastSector) {
            ++car.blueSectors;
            car.blueLastSector = sector;
        }
        if (car.blueIgnoring && !car.blueReported && car.blueSectors >= config_.rules.blueFlagMarshalSectors) {
            car.blueReported = true;
            ++stats_.blueFlagsIgnored;
            addIncident(Incident::Type::IgnoringBlueFlags, order[k], lapper, 1.0); // entirely the lapped driver's fault
        }
    }
}

void Sim::movePitCar(int i) {
    Car& car = cars_[i];
    const double lapLength = track_.length();
    const double pitSpeed = config_.rules.pitLaneSpeedKmh / 3.6;
    const double before = track_.wrap(car.distance);
    if (car.stopTimer > 0.0) {
        car.stopTimer = std::max(0.0, car.stopTimer - kDt);
        car.speed = 0.0;
    } else if (car.speed > pitSpeed) {
        car.speed = std::max(pitSpeed, car.speed - kBrake * kDt);
    } else {
        car.speed = std::min(pitSpeed, car.speed + 4.0 * kDt);
    }
    car.distance += car.speed * kDt;
    car.lateral = kPitLateral;
    const double after = track_.wrap(car.distance);

    // Stop-and-go: the box is at the line.
    if (car.pendingPenalty == Penalty::StopAndGo && !car.stopDone && before > lapLength / 2.0 && after < lapLength / 2.0) {
        car.stopDone = true;
        car.stopTimer = config_.rules.stopAndGoSeconds;
        car.speed = 0.0;
    }
    if (before < kPitExitAfterLine && after >= kPitExitAfterLine) {
        car.inPit = false;
        car.pendingPenalty = Penalty::None; // served
        car.lateral = 0.0;
    }
}

void Sim::convertUnservedPenalties() {
    for (Car& car : cars_) {
        if (car.pendingPenalty == Penalty::DriveThrough) {
            car.timePenalty += config_.rules.unservedDriveThroughSeconds;
        } else if (car.pendingPenalty == Penalty::StopAndGo) {
            car.timePenalty += config_.rules.unservedStopAndGoSeconds;
        }
        car.pendingPenalty = Penalty::None;
    }
}

void Sim::deploySafetyCar(bool inThisLap) {
    int front = -1; // race leader on the road: most distance covered
    for (int i = 0; i < static_cast<int>(cars_.size()); ++i) {
        if (!outOfRace(i) && (front < 0 || cars_[i].distance > cars_[front].distance)) {
            front = i;
        }
    }
    if (front < 0) {
        return;
    }
    safetyCar_.onTrack = true;
    safetyCar_.inThisLap = inThisLap;
    safetyCar_.distance = cars_[front].distance + kSafetyCarLead;
    safetyCar_.speed = 0.6 * cars_[front].speed;
    state_ = RaceState::SafetyCar;
    overtakeEnabled_ = false; // B7.2.2 c
    overtakeAfterSafetyCar_ = true;
    for (Car& car : cars_) {
        car.overtakeActive = false;
    }
    cancelAttacks();
    for (Car& car : cars_) {
        car.holdUntilLine = false;
    }
}

void Sim::cancelAttacks() {
    for (Car& car : cars_) {
        car.overtakeTarget = -1;
        car.yieldTimer = 0.0;
    }
}

void Sim::moveSafetyCar() {
    // Speed only; the position is integrated together with the cars.
    const auto& segments = track_.segments();
    const int count = static_cast<int>(segments.size());
    const double fraction = config_.rules.safetyCarSpeedFraction;
    const double s = track_.wrap(safetyCar_.distance);
    const int index = track_.segmentIndexAt(s);
    double target = segments[index].speed * fraction;
    double distanceToNext = segments[index].start + segments[index].length - s;
    for (int k = 1; k <= 3; ++k) {
        const TrackSegment& next = segments[(index + k) % count];
        const double vNext = next.speed * fraction;
        target = std::min(target, std::sqrt(vNext * vNext + 2.0 * kBrake * distanceToNext));
        distanceToNext += next.length;
    }
    const double v = safetyCar_.speed;
    safetyCar_.speed = v < target ? std::min(target, v + acceleration(v, kCarTopSpeed) * kDt)
                                  : std::max(target, v - kBrake * kDt);
}

bool Sim::overtakingAllowed(int i) const {
    const Car& car = cars_[i];
    return state_ == RaceState::Green && !car.holdUntilLine && flagAt(car.distance) == Flag::Green;
}

void Sim::addIncident(Incident::Type type, int car, int otherCar, double blame) {
    Incident incident;
    incident.id = static_cast<int>(incidents_.size()) + 1;
    incident.type = type;
    incident.time = time_;
    incident.car = car;
    incident.otherCar = otherCar;
    incident.distance = cars_[car].distance;
    incident.blame = blame;
    incidents_.push_back(incident);
}

void Sim::recordReplayFrame() {
    replayFrames_.push_back({time_, safetyCar_.distance, safetyCar_.onTrack});
    for (const Car& car : cars_) {
        replayCars_.push_back({car.distance, static_cast<float>(car.lateral), static_cast<float>(car.speed)});
    }
}

std::size_t Sim::replayFrameAt(double time) const {
    const auto it = std::upper_bound(replayFrames_.begin(), replayFrames_.end(), time,
        [](double t, const ReplayFrame& frame) { return t < frame.time; });
    const auto index = static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, it - replayFrames_.begin() - 1));
    return std::min(index, replayFrames_.size() - 1);
}

void Sim::recordViolation(int car, int passedCar) {
    using Type = Incident::Type;
    Type type = Type::OvertakeUnderYellow;
    if (state_ == RaceState::VirtualSafetyCar) {
        type = Type::OvertakeUnderVsc;
    } else if (state_ == RaceState::SafetyCar) {
        type = Type::OvertakeUnderSafetyCar;
    } else if (state_ == RaceState::RedFlag) {
        type = Type::OvertakeUnderRedFlag;
    } else if (cars_[car].holdUntilLine) {
        type = Type::OvertakeBeforeRestartLine;
    }
    addIncident(type, car, passedCar, 1.0); // a pass under a flag is entirely the overtaker's fault
    ++stats_.flagViolations;
}

double Sim::paceFactor(int i) const {
    switch (state_) {
    case RaceState::VirtualSafetyCar:
        return config_.rules.vscSpeedFraction; // same delta for everyone: time gaps hold
    case RaceState::SafetyCar:
        return performance(i) * config_.rules.safetyCarCatchUpFraction;
    case RaceState::RedFlag:
        return config_.rules.redFlagSpeedFraction;
    case RaceState::Green:
        break;
    }
    return performance(i);
}

double Sim::rollForm() { return 1.0 + rng_.uniform(-kFormSpread, kFormSpread); }

double Sim::performance(int i) const {
    const Car& car = cars_[i];
    const Entry& entry = entries_[i];
    const double fuel = kFuelStartFactor +
        (1.0 - kFuelStartFactor) * std::min(1.0, car.lapsCompleted / static_cast<double>(config_.laps));
    const double tyres = 1.0 - kTyreLossPerLap * car.tyreWear;
    return entry.teamPace * entry.skill * car.form * tyres * fuel;
}

double Sim::speedCap(int i, double gapAhead) const {
    const Car& car = cars_[i];
    const TrackSegment& seg = track_.segments()[car.segment];
    double cap = seg.speed * paceFactor(i);
    if (state_ == RaceState::Green) {
        if (car.overtakeActive && seg.speed >= kFastSegment) {
            cap *= 1.0 + kOvertakeBoost;
        }
        if (gapAhead < kSlipstreamGap && seg.speed >= kFastSegment) {
            cap *= 1.0 + kSlipstreamBonus * (1.0 - gapAhead / kSlipstreamGap);
        } else if (gapAhead < kDirtyAirGap && seg.speed < kFastSegment) {
            cap *= kDirtyAirFactor;
        }
    }
    const Flag flag = flagAt(car.distance);
    if (flag == Flag::Yellow) {
        cap *= config_.rules.yellowSpeedFactor;
    } else if (flag == Flag::DoubleYellow) {
        cap *= config_.rules.doubleYellowSpeedFactor;
    }
    if (car.errorTimer > 0.0) {
        cap *= kErrorSpeedFactor;
    }
    if (car.blueLapper >= 0 && !car.blueIgnoring) {
        cap *= kBlueYieldFactor; // blue flag: let the faster car by
    }
    for (const HazardState& hazard : hazards_) {
        if (hazard.onTrack && track_.wrap(hazard.trackPosition - car.distance) < kHazardSightDistance) {
            cap *= kHazardCautionFactor; // a car on the racing line ahead: lift and go around
            break;
        }
    }
    if (car.yieldTimer > 0.0) {
        cap *= kYieldFactor;
    }
    return cap;
}

double Sim::desiredSpeed(int i, double cap) const {
    const Car& car = cars_[i];
    if (time_ < car.startDelay) {
        return 0.0;
    }
    const auto& segments = track_.segments();
    const int count = static_cast<int>(segments.size());
    const TrackSegment& seg = segments[car.segment];
    const double pace = paceFactor(i);

    // Brake early enough for the next corners: v^2 = v_next^2 + 2*b*d.
    const double brake = car.overtakeTarget >= 0 ? kBrake * kLateBrakeFactor : kBrake;
    double target = cap;
    double distanceToNext = seg.start + seg.length - track_.wrap(car.distance);
    for (int k = 1; k <= 3; ++k) {
        const TrackSegment& next = segments[(car.segment + k) % count];
        const double vNext = next.speed * pace;
        target = std::min(target, std::sqrt(vNext * vNext + 2.0 * brake * distanceToNext));
        distanceToNext += next.length;
    }

    // Red flag: stop in order just before the line (stand-in for the pit lane).
    if (state_ == RaceState::RedFlag) {
        const double stopAt = (car.lapsCompleted + 1) * track_.length() - kRedFlagStopOffset;
        const double remaining = stopAt - car.distance;
        if (remaining <= 0.1) {
            return 0.0;
        }
        target = std::min(target, std::sqrt(2.0 * brake * remaining));
    }

    // cap / segment speed = combined factor of performance, slipstream, dirty air, errors.
    const double topSpeed = kCarTopSpeed * cap / seg.speed;
    const double v = car.speed;
    return v < target ? std::min(target, v + acceleration(v, topSpeed) * kDt)
                      : std::max(target, v - brake * kDt);
}

void Sim::step() {
    if (finished()) {
        if (!pending_.empty()) { // verdicts after the flag still count
            applyCommands();
            buildSnapshot();
        }
        return;
    }
    applyCommands();
    if (state_ != RaceState::Green) {
        neutralisedTime_ += kDt;
    }
    updateHazards();
    const double lapLength = track_.length();
    const int n = static_cast<int>(cars_.size());

    // Cars racing on the track, sorted by track position (not race order).
    // Cars in the pit lane move on their own and do not interact; spun cars stand still.
    std::vector<int> order;
    std::vector<int> inPitLane;
    std::vector<int> spun;
    for (int i = 0; i < n; ++i) {
        if (outOfRace(i)) {
            continue;
        }
        if (cars_[i].spinTimer > 0.0) {
            spun.push_back(i);
        } else {
            (cars_[i].inPit ? inPitLane : order).push_back(i);
        }
    }
    for (int i : spun) {
        Car& car = cars_[i];
        car.speed = 0.0;
        car.spinTimer = std::max(0.0, car.spinTimer - kDt);
        if (car.spinTimer <= 0.0) {
            car.lateral = 0.0; // rejoins from standstill
        }
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const double sa = track_.wrap(cars_[a].distance);
        const double sb = track_.wrap(cars_[b].distance);
        return sa != sb ? sa < sb : a < b;
    });
    const int m = static_cast<int>(order.size());

    std::vector<int> ahead(n, -1);
    std::vector<double> gap(n, std::numeric_limits<double>::infinity());
    if (m > 1) {
        for (int k = 0; k < m; ++k) {
            const int i = order[k];
            const int a = order[(k + 1) % m];
            ahead[i] = a;
            gap[i] = track_.wrap(cars_[a].distance - cars_[i].distance);
        }
    }

    updateBlueFlags(order);

    // Phase A: what each driver wants to do on a free track.
    std::vector<double> cap(n, 0.0);
    std::vector<double> speed(n, 0.0);
    for (int i : order) {
        cap[i] = speedCap(i, gap[i]);
        speed[i] = desiredSpeed(i, cap[i]);
    }

    if (safetyCar_.onTrack) {
        moveSafetyCar();
    }
    auto safetyCarGap = [&](int i) {
        return safetyCar_.onTrack ? track_.wrap(safetyCar_.distance - cars_[i].distance)
                                  : std::numeric_limits<double>::infinity();
    };

    // Phase B: interaction, resolved front to back so a queue follows its leader.
    const auto& segments = track_.segments();
    for (int k = m - 1; k >= 0; --k) {
        const int i = order[k];
        const int a = ahead[i];
        Car& car = cars_[i];
        const double scGap = safetyCarGap(i);
        if (scGap < gap[i]) { // the Safety Car is the next thing on the road
            if (scGap < kFollowGap) {
                speed[i] = std::min(speed[i], safetyCar_.speed);
            }
            continue;
        }
        if (a >= 0 && car.overtakeTarget != a && gap[i] < kFollowGap && yieldsTo(a, i)) {
            car.overtakeTarget = a; // the lapped car moves over: a free pass, no contest
            car.overtakeTimer = kOvertakeTimeout;
            continue;
        }
        if (a < 0 || car.overtakeTarget == a) {
            continue;
        }
        if (gap[i] >= kFollowGap) {
            continue;
        }
        // The chance depends on the speed potential (pace + slipstream), not on the
        // current speed: a follower held up behind a car is at that car's speed.
        const double advantage = cap[i] - cap[a];
        if (advantage > 0.0 && time_ >= car.startDelay) {
            const double chancePerSecond = segments[car.segment].overtakeChance *
                entries_[i].aggression * std::min(1.0, advantage / kOvertakeAdvantageScale);
            // Where overtaking is not allowed, only a rule-breaker still goes for it.
            if (rng_.uniform() < chancePerSecond * kDt &&
                (overtakingAllowed(i) || rng_.uniform() < config_.ruleBreakChance)) {
                car.overtakeTarget = a;
                car.overtakeTimer = kOvertakeTimeout;
                ++stats_.overtakeAttempts;
                continue;
            }
        }
        speed[i] = std::min(speed[i], speed[a]);
    }

    // Phase C: integrate.
    time_ += kDt;
    std::vector<double> previous(n, 0.0);
    for (int i : order) {
        Car& car = cars_[i];
        previous[i] = car.distance;
        car.speed = speed[i];
        car.distance += speed[i] * kDt;
        car.errorTimer = std::max(0.0, car.errorTimer - kDt);
        car.yieldTimer = std::max(0.0, car.yieldTimer - kDt);
    }
    for (int i : inPitLane) {
        previous[i] = cars_[i].distance;
        movePitCar(i);
    }
    // Penalised cars turn into the pit lane (only under green).
    const double pitLaneEntry = lapLength - kPitEntryBeforeLine;
    for (int i : order) {
        Car& car = cars_[i];
        const bool mustServe = car.pendingPenalty == Penalty::DriveThrough || car.pendingPenalty == Penalty::StopAndGo;
        if (mustServe && state_ == RaceState::Green && track_.wrap(previous[i]) < pitLaneEntry &&
            track_.wrap(car.distance) >= pitLaneEntry) {
            car.inPit = true;
            car.stopDone = false;
            car.overtakeTarget = -1;
            car.lateral = kPitLateral;
        }
    }
    if (safetyCar_.onTrack) {
        const double before = track_.wrap(safetyCar_.distance);
        safetyCar_.distance += safetyCar_.speed * kDt;
        const double after = track_.wrap(safetyCar_.distance);
        const double pitEntry = lapLength - kPitEntryBeforeLine;
        if (safetyCar_.inThisLap && before < pitEntry && after >= pitEntry) {
            safetyCar_ = SafetyCar{};
            state_ = RaceState::Green;
            for (Car& car : cars_) {
                car.holdUntilLine = !car.finished; // racing resumes at the line
            }
        }
    }

    // Overtake progress and separation, front to back.
    for (int k = m - 1; k >= 0; --k) {
        const int i = order[k];
        Car& car = cars_[i];
        if (car.inPit) {
            continue; // just turned into the pit lane
        }
        if (car.overtakeTarget >= 0) {
            const Car& target = cars_[car.overtakeTarget];
            const bool passed = track_.wrap(target.distance - car.distance) > lapLength / 2.0;
            if (passed && std::abs(target.distance - car.distance) < lapLength / 2.0) {
                ++stats_.overtakes; // same lap: a real position change
                if (!overtakingAllowed(i)) {
                    recordViolation(i, car.overtakeTarget);
                }
            }
            car.overtakeTimer -= kDt;
            if (passed || car.overtakeTimer <= 0.0) {
                car.overtakeTarget = -1;
            }
        }
        car.lateral = car.overtakeTarget >= 0 ? kOvertakeLateral : 0.0;
        if (car.overtakeTarget < 0) {
            const double g = ahead[i] >= 0 ? track_.wrap(cars_[ahead[i]].distance - car.distance)
                                           : std::numeric_limits<double>::infinity();
            const double scGap = safetyCarGap(i);
            const double nearest = std::min(g, scGap);
            if (nearest < kMinGap) {
                car.distance -= kMinGap - nearest;
            }
        }
    }

    updateOvertakeMode(order, ahead, previous);
    checkHazardPasses(order, previous);
    order.insert(order.end(), inPitLane.begin(), inPitLane.end());
    for (int i : order) {
        processTimingLines(i, previous[i]);
        const int segment = track_.segmentIndexAt(track_.wrap(cars_[i].distance));
        if (segment != cars_[i].segment) {
            cars_[i].segment = segment;
            if (!cars_[i].inPit) {
                onSegmentEntered(i);
            }
        }
    }
    deliverRadio();
    buildSnapshot();
    if (snapshot_.finished) {
        finishTime_ = time_;
        convertUnservedPenalties();
        buildSnapshot(); // final classification with the converted penalties
    }
    recordReplayFrame();
}

void Sim::onSegmentEntered(int i) {
    Car& car = cars_[i];
    const TrackSegment& seg = track_.segments()[car.segment];

    // An attack is decided at corner entry: alongside = contest the corner, else give up.
    // A contested corner can end in contact.
    if (car.overtakeTarget >= 0 && seg.speed < kFastSegment && !yieldsTo(car.overtakeTarget, i)) {
        const int target = car.overtakeTarget;
        Car& defender = cars_[target];
        const double gap = track_.wrap(defender.distance - car.distance);
        const double attack = entries_[i].aggression;
        const double defend = entries_[target].aggression;
        if (gap < kAlongsideGap && rng_.uniform() < kContactChance * (attack + defend)) {
            // Ground truth: the further back the attacker turns in, the more it is his fault.
            const double blame = std::clamp(0.3 + 0.08 * gap + 0.4 * (attack - defend), 0.0, 1.0);
            car.errorTimer = kContactDuration;
            defender.errorTimer = kContactDuration;
            car.overtakeTarget = -1;
            ++stats_.collisions;
            addIncident(Incident::Type::Collision, i, target, blame);
            maybeAccident(target, kCollisionCrashChance, 0.0, kCollisionSpinChance, true);
        } else if (gap < kAlongsideGap && rng_.uniform() < attack / (attack + defend)) {
            defender.yieldTimer = kYieldTime;
        } else {
            car.overtakeTarget = -1;
        }
    }

    if (seg.speed >= kSlowCorner) {
        return;
    }
    const double chance = kErrorChance * (2.0 - entries_[i].skill) * (1.0 + 0.02 * car.tyreWear);
    if (rng_.uniform() < chance) {
        car.errorTimer = kErrorDuration;
        ++stats_.driverErrors;
        maybeAccident(i, kErrorCrashChance, kErrorOffTrackChance, kErrorSpinChance, false);
    }
}

void Sim::processTimingLines(int i, double previousDistance) {
    Car& car = cars_[i];
    while (!car.finished) {
        const double boundary = car.lapsCompleted * track_.length() + boundaries_[car.boundaryIndex];
        if (!(previousDistance < boundary && car.distance >= boundary)) {
            break;
        }
        // Interpolate the exact crossing time inside the tick.
        const double fraction = (boundary - previousDistance) / (car.distance - previousDistance);
        const double t = time_ - kDt + fraction * kDt;
        car.sectorTimes[car.boundaryIndex] = t - car.lastBoundaryTime;
        car.lastBoundaryTime = t;
        car.boundaryTimes.push_back(t);

        if (++car.boundaryIndex < 3) {
            continue;
        }
        car.boundaryIndex = 0;
        car.lastSectorTimes = car.sectorTimes;
        car.lastLapTime = car.sectorTimes[0] + car.sectorTimes[1] + car.sectorTimes[2];
        if (car.bestLapTime == 0.0 || car.lastLapTime < car.bestLapTime) {
            car.bestLapTime = car.lastLapTime;
        }
        ++car.lapsCompleted;
        car.holdUntilLine = false;
        car.tyreWear += 1.0;
        car.form = rollForm();
        if (chequered_ || car.lapsCompleted >= config_.laps) {
            chequered_ = true;
            car.finished = true;
            car.speed = 0.0;
        }
    }
}

void Sim::buildSnapshot() {
    const int n = static_cast<int>(cars_.size());

    // Timing-screen order: more timing lines crossed first, then earlier crossing.
    // Final classification (everyone finished): laps, then race time plus time penalties.
    // Retired cars come last, ordered by laps completed (more laps = better).
    const bool allFinished = std::all_of(cars_.begin(), cars_.end(), [](const Car& c) { return c.finished || c.retired; });
    auto classifiedTime = [&](const Car& c) { return c.lastBoundaryTime + c.timePenalty; };
    std::vector<int> rank(n);
    std::iota(rank.begin(), rank.end(), 0);
    std::sort(rank.begin(), rank.end(), [&](int a, int b) {
        const Car& ca = cars_[a];
        const Car& cb = cars_[b];
        if (ca.retired != cb.retired) {
            return cb.retired;
        }
        if (ca.retired) {
            if (ca.boundaryTimes.size() != cb.boundaryTimes.size()) {
                return ca.boundaryTimes.size() > cb.boundaryTimes.size();
            }
            return a < b;
        }
        if (allFinished) {
            if (ca.lapsCompleted != cb.lapsCompleted) {
                return ca.lapsCompleted > cb.lapsCompleted;
            }
            if (classifiedTime(ca) != classifiedTime(cb)) {
                return classifiedTime(ca) < classifiedTime(cb);
            }
            return a < b;
        }
        if (ca.boundaryTimes.size() != cb.boundaryTimes.size()) {
            return ca.boundaryTimes.size() > cb.boundaryTimes.size();
        }
        if (ca.lastBoundaryTime != cb.lastBoundaryTime) {
            return ca.lastBoundaryTime < cb.lastBoundaryTime;
        }
        if (ca.distance != cb.distance) {
            return ca.distance > cb.distance;
        }
        return a < b;
    });

    const Car& leader = cars_[rank[0]];
    snapshot_.time = time_;
    snapshot_.totalLaps = config_.laps;
    snapshot_.leaderLap = std::min(config_.laps, leader.lapsCompleted + 1);
    snapshot_.chequered = chequered_;
    snapshot_.finished = allFinished;
    snapshot_.hazards = hazards_;
    snapshot_.overtakeEnabled = overtakeEnabled_;
    snapshot_.state = state_;
    snapshot_.sectorFlags = sectorFlags_;
    snapshot_.safetyCarOnTrack = safetyCar_.onTrack;
    snapshot_.safetyCarInThisLap = safetyCar_.inThisLap;
    snapshot_.safetyCarDistance = safetyCar_.distance;
    snapshot_.cars.resize(n);

    for (int r = 0; r < n; ++r) {
        const int i = rank[r];
        const Car& car = cars_[i];
        CarState& cs = snapshot_.cars[r];
        cs.entryIndex = i;
        cs.position = r + 1;
        cs.gridPosition = car.gridPosition;
        cs.lapsCompleted = car.lapsCompleted;
        cs.distance = car.distance;
        cs.trackPosition = track_.wrap(car.distance);
        cs.speed = car.speed;
        cs.lateral = car.lateral;
        cs.lastLapTime = car.lastLapTime;
        cs.bestLapTime = car.bestLapTime;
        cs.lastSectorTimes = car.lastSectorTimes;
        cs.lastLineTime = car.lastBoundaryTime;
        cs.finished = car.finished;
        cs.timePenalty = car.timePenalty;
        cs.pendingPenalty = car.pendingPenalty;
        cs.inPitLane = car.inPit;
        cs.spinning = car.spinTimer > 0.0;
        cs.retired = car.retired;
        cs.blueFlag = car.blueLapper >= 0;
        cs.overtakeActive = car.overtakeActive;
        cs.detectionGap = car.detectionGap;
        cs.gapToLeader = 0.0;
        cs.lapsDown = 0;
        cs.interval = 0.0;

        const std::size_t crossed = car.boundaryTimes.size();
        if (car.retired) {
            cs.lapsDown = leader.lapsCompleted - car.lapsCompleted; // gaps are meaningless for a retired car
        } else if (r > 0 && allFinished) {
            const Car& ahead = cars_[rank[r - 1]];
            cs.lapsDown = leader.lapsCompleted - car.lapsCompleted;
            cs.gapToLeader = classifiedTime(car) - classifiedTime(leader);
            cs.interval = car.lapsCompleted == ahead.lapsCompleted ? classifiedTime(car) - classifiedTime(ahead) : 0.0;
        } else if (r > 0 && crossed > 0) {
            const std::size_t last = crossed - 1;
            cs.lapsDown = static_cast<int>((leader.boundaryTimes.size() - crossed) / 3);
            cs.gapToLeader = car.boundaryTimes[last] - leader.boundaryTimes[last];
            cs.interval = car.boundaryTimes[last] - cars_[rank[r - 1]].boundaryTimes[last];
        }
    }
}

} // namespace uisim
