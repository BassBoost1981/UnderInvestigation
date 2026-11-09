#include "uisim/Sim.h"

#include <algorithm>
#include <bitset>
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
constexpr double kOvertakeAdvantageScale = 1.35; // m/s of potential for the full chance (M7.4: fresh-tyre passes)
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
// Weather (M9.2).
constexpr double kWeatherShiftMax = 300.0;        // seconds the rain may come earlier or later (variation 1)
constexpr double kWeatherScaleMax = 0.3;          // share the rain may be lighter or heavier (variation 1)
constexpr double kWetTargetPerRain = 1.4;         // wetness the rain drives towards: min(1, rain * this)
constexpr double kWettingPerSecond = 1.0 / 120.0; // soaked in about two minutes
constexpr double kDryingLinePerSecond = 1.0 / 900.0;     // the racing line dries in about 15 minutes
constexpr double kDryingOffLinePerSecond = 1.0 / 1800.0; // off the line in about 30 minutes
constexpr double kSlickWetGripLoss = 0.25;        // grip lost on slicks on a soaked racing line
constexpr double kWetErrorFactor = 3.0;           // driver errors on a soaked racing line: x (1 + this)
constexpr double kSprayRisk = 1.5;                // follow-up risk in spray: x (1 + this * rain)
constexpr double kSprayDistance = 60.0;           // metres behind another car: in its spray
constexpr double kErrorDuration = 1.5;
constexpr double kErrorSpeedFactor = 0.7;

// Race control (M3).
constexpr double kSafetyCarLead = 150.0;      // Safety Car joins this far ahead of the race leader
constexpr double kUnlappedAhead = 50.0;       // a released lapped car this far ahead of the SC is past it
constexpr double kPitEntryBeforeLine = 300.0; // Safety Car leaves the track here when "in this lap"
constexpr double kRedFlagStopOffset = 2.0;    // red flag: cars stop this far before the line

// Penalties (M5). Pit lane runs alongside the main straight, from the pit entry
// (kPitEntryBeforeLine before the line) to the exit after the line.
constexpr double kPitLateral = -5.0;          // snapshot lateral units: 20 m right of the racing line
constexpr double kPitExitAfterLine = 300.0;

// Pit stops (M7.4).
constexpr double kStopTime = 2.4;          // stationary time of a good stop, seconds
constexpr double kStopTimeSpread = 0.6;
constexpr double kSlowStopChance = 0.04;   // wheel gun trouble and the like
constexpr double kSlowStopMin = 3.0;
constexpr double kSlowStopMax = 8.0;
constexpr double kStintMarginMin = 1.0;    // laps before the cliff a team plans to stop
constexpr double kStintMarginMax = 4.0;
constexpr int kNeutralisedStopWindow = 10; // under SC/VSC a stop this many laps early is worth it
constexpr double kBoxSpacing = 12.0;       // metres between team boxes, centred on the line
// Pit lane offences (M7.5).
constexpr double kSpeedingChance = 0.02;   // limiter fault per pit lane visit
constexpr double kSpeedingMinKmh = 1.0;
constexpr double kSpeedingMaxKmh = 20.0;
constexpr double kReleaseWindow = 30.0;    // m: a car this close behind in the fast lane is traffic
constexpr double kUnsafeReleaseChance = 0.25; // the team releases anyway instead of waiting
constexpr double kClosedEntryIgnoreChance = 0.05; // a car due to stop enters the closed pit lane anyway (M8.3)
// Track limits (M7.6).
constexpr double kTrackLimitsChance = 0.02; // per pass of a track-limits corner, times aggression^4 (a few drivers keep running wide)
constexpr double kTrackLimitsTyreWear = 0.02; // worn tyres: more running wide, per lap of age

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
constexpr double kPassRiskLearning = 0.97;    // each car that passed makes the next one more careful
constexpr int kMaxFollowUpsPerSite = 2;        // drivers slow down on their own after that
constexpr double kPunctureChance = 0.02;       // per pass over crash debris at racing speed
constexpr double kPunctureDuration = 20.0;     // limping, seconds

// Long-run effects (no pit stops in M1).
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
    std::vector<std::string> teams; // in order of appearance; one pit box each
    std::vector<int> teamIndex(n);
    for (int i = 0; i < n; ++i) {
        auto it = std::find(teams.begin(), teams.end(), entries_[i].team);
        if (it == teams.end()) {
            teams.push_back(entries_[i].team);
            it = teams.end() - 1;
        }
        teamIndex[i] = static_cast<int>(it - teams.begin());
    }
    for (int i = 0; i < n; ++i) {
        cars_[i].boxOffset = (teamIndex[i] - (static_cast<double>(teams.size()) - 1.0) / 2.0) * kBoxSpacing;
    }
    for (int slot = 0; slot < n; ++slot) {
        Car& car = cars_[rating[slot].second];
        car.gridPosition = slot + 1;
        car.distance = -(kGridOffset + kGridSpacing * slot);
        car.segment = track_.segmentIndexAt(track_.wrap(car.distance));
        car.startDelay = rng_.uniform(0.2, 0.5);
        car.form = rollForm();
        // Start tyres (sim strategy): front half on medium, then hard, the last four gamble on soft.
        car.compound = slot < n / 2 ? Compound::Medium : slot < n - 4 ? Compound::Hard : Compound::Soft;
        car.compoundsUsed = 1u << static_cast<int>(car.compound);
    }
    // Strategy errors on their own random stream, so the main sequence stays as it was (M8.3).
    Pcg32 strategyRng(config_.seed, 77u);
    for (Car& car : cars_) {
        car.forgetsCompoundRule = strategyRng.uniform() < config_.compoundRuleMissChance;
    }
    if (!config_.weather.points.empty()) {
        // The weather on its own random stream too: a dry race stays exactly as it was.
        Pcg32 weatherRng(config_.seed, 91u);
        const double variation = std::clamp(config_.weather.variation, 0.0, 1.0);
        weatherShift_ = weatherRng.uniform(-1.0, 1.0) * variation * kWeatherShiftMax;
        weatherScale_ = 1.0 + weatherRng.uniform(-1.0, 1.0) * variation * kWeatherScaleMax;
        rain_ = rainAt(config_.weather, 0.0, weatherShift_, weatherScale_);
        wetness_ = std::min(1.0, rain_ * kWetTargetPerRain); // raining at the start: already wet
        lineWetness_ = wetness_;
    }
    for (int i = 0; i < n; ++i) {
        planNextStop(i);
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
                for (Car& car : cars_) {
                    car.unlapping = false;
                    car.rejoining = false;
                }
                overtakeEnabled_ = false; // re-enabled after the restart behind the Safety Car
                for (Car& car : cars_) {
                    car.overtakeActive = false;
                }
                cancelAttacks();
                if (config_.rules.redFlagFreeTyreChange) { // B5.14.4 a vii
                    for (int k = 0; k < static_cast<int>(cars_.size()); ++k) {
                        if (!outOfRace(k)) {
                            fitTyres(k, chooseCompound(k));
                            planNextStop(k);
                        }
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
        case Type::ClosePitEntry:
            pitEntryClosed_ = true;
            break;
        case Type::OpenPitEntry:
            pitEntryClosed_ = false;
            break;
        case Type::LappedCarsMayOvertake:
            if (state_ == RaceState::SafetyCar && config_.rules.lappedCarsMayOvertake && !safetyCar_.inThisLap &&
                !safetyCar_.lappedCarsReleased) {
                releaseLappedCars();
            }
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
    case Penalty::TenSeconds: {
        const double seconds = c.penalty == Penalty::FiveSeconds ? 5.0 : 10.0;
        car.timePenalty += seconds; // stays race time unless served at a stop
        if (!car.finished && config_.rules.timePenaltyServedAtNextStop) {
            car.servablePenalty += seconds;
        }
        break;
    }
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
    case Penalty::Disqualification:
        car.disqualified = true; // removed from the results
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
            if (siteMemory_.size() <= static_cast<std::size_t>(hazard.id)) {
                siteMemory_.resize(static_cast<std::size_t>(hazard.id) + 1);
            }
            SiteMemory& site = siteMemory_[static_cast<std::size_t>(hazard.id)];
            const double learning = std::pow(kPassRiskLearning, site.passes++);
            double spray = 1.0; // in the rain a car close behind another sees less (M9.2)
            if (rain_ > 0.0) {
                for (int j = 0; j < static_cast<int>(cars_.size()); ++j) {
                    if (j != i && !outOfRace(j) && !cars_[j].inPit &&
                        track_.wrap(cars_[j].distance - car.distance) < kSprayDistance) {
                        spray = 1.0 + kSprayRisk * rain_;
                        break;
                    }
                }
            }
            const double risk = site.followUps >= kMaxFollowUpsPerSite ? 0.0
                : (hazard.onTrack ? kPassRiskCrashSite : kPassRiskOffTrackSite) * flagFactor * speedFactor *
                  learning * config_.followUpChanceScale * spray;
            if (rng_.uniform() < risk) {
                ++site.followUps;
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
    if (!fromCollision) {
        addIncident(Incident::Type::Accident, i, i, 0.0); // contact is already a Collision incident
    }

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

Compound Sim::chooseCompound(int i) const {
    const Car& car = cars_[i];
    const int remaining = config_.laps - car.lapsCompleted;
    const CompoundSpecs& specs = track_.compounds();
    Compound choice = Compound::Hard;
    for (Compound c : {Compound::Soft, Compound::Medium}) {
        if (specs[static_cast<int>(c)].cliffLap - kStintMarginMax >= remaining) {
            choice = c;
            break;
        }
    }
    // B6.3.6: a second, different compound if only one was used so far.
    if (!car.forgetsCompoundRule && car.compoundsUsed == (1u << static_cast<int>(choice))) {
        choice = choice == Compound::Hard ? Compound::Medium : Compound::Hard;
    }
    return choice;
}

void Sim::fitTyres(int i, Compound compound) {
    Car& car = cars_[i];
    car.compound = compound;
    car.tyreWear = 0.0;
    car.compoundsUsed |= 1u << static_cast<int>(compound);
}

void Sim::planNextStop(int i) {
    Car& car = cars_[i];
    const int remaining = config_.laps - car.lapsCompleted;
    const double margin = rng_.uniform(kStintMarginMin, kStintMarginMax);
    const int stint = std::max(1, static_cast<int>(track_.compounds()[static_cast<int>(car.compound)].cliffLap - margin -
                                                   car.tyreWear));
    const bool ruleMet = car.forgetsCompoundRule ||
        std::bitset<3>(car.compoundsUsed).count() >= static_cast<std::size_t>(config_.rules.minDryCompounds);
    if (remaining <= stint) {
        car.plannedStopLap = ruleMet ? std::numeric_limits<int>::max()
                                     : car.lapsCompleted + std::max(1, remaining / 2); // only for B6.3.6
    } else {
        car.plannedStopLap = car.lapsCompleted + stint;
    }
}

bool Sim::wantsTyreStop(int i) const {
    const Car& car = cars_[i];
    if (state_ == RaceState::RedFlag || chequered_ || car.finished || car.inPit ||
        car.plannedStopLap == std::numeric_limits<int>::max()) {
        return false;
    }
    const int inLap = car.lapsCompleted + 1; // the stop ends this lap
    const bool neutralised = state_ == RaceState::SafetyCar || state_ == RaceState::VirtualSafetyCar;
    return inLap >= car.plannedStopLap ||
           (neutralised && car.plannedStopLap - inLap <= kNeutralisedStopWindow && config_.laps - inLap > 3);
}

// End of a stop: a car approaching in the fast lane means wait, unless the team gets it wrong.
void Sim::releaseFromBox(int i) {
    Car& car = cars_[i];
    int traffic = -1;
    double gap = kReleaseWindow;
    for (int j = 0; j < static_cast<int>(cars_.size()); ++j) {
        const Car& other = cars_[j];
        if (j == i || !other.inPit || other.stopTimer > 0.0 || other.speed < 1.0) {
            continue;
        }
        const double behind = track_.wrap(car.distance - other.distance);
        if (behind > 0.0 && behind < gap) {
            gap = behind;
            traffic = j;
        }
    }
    if (traffic < 0) {
        return;
    }
    if (!car.releaseChecked && rng_.uniform() < kUnsafeReleaseChance) {
        car.releaseChecked = true;
        addIncident(Incident::Type::UnsafeRelease, i, traffic, 1.0); // B1.6.2 a: the team's responsibility
        incidents_.back().measure = gap;
        return;
    }
    car.releaseChecked = true;
    car.stopTimer = kDt; // hold the car until the lane is clear
}

void Sim::movePitCar(int i) {
    Car& car = cars_[i];
    const double lapLength = track_.length();
    const double pitSpeed = (config_.rules.pitLaneSpeedKmh + car.pitSpeedExcess) / 3.6;
    const double before = track_.wrap(car.distance);
    if (car.stopTimer > 0.0) {
        car.stopTimer = std::max(0.0, car.stopTimer - kDt);
        if (car.stopTimer == 0.0) {
            releaseFromBox(i);
        }
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
    // The stop is at the team's own box.
    const double boxBefore = track_.wrap(before - car.boxOffset);
    const double boxAfter = track_.wrap(after - car.boxOffset);
    const bool atBox = boxBefore > lapLength / 2.0 && boxAfter < lapLength / 2.0;
    if (car.pendingPenalty == Penalty::StopAndGo && !car.stopDone && atBox) {
        car.stopDone = true;
        car.stopTimer = config_.rules.stopAndGoSeconds;
        car.speed = 0.0;
    }
    // Tyre stop: the box is at the line too; a 5 s / 10 s penalty is served before any work.
    if (car.tyreStop && !car.stopDone && atBox) {
        car.stopDone = true;
        double stationary = kStopTime + rng_.uniform(0.0, kStopTimeSpread);
        if (rng_.uniform() < kSlowStopChance) {
            stationary += rng_.uniform(kSlowStopMin, kSlowStopMax);
        }
        car.stopTimer = car.servablePenalty + stationary;
        car.timePenalty -= car.servablePenalty;
        car.servablePenalty = 0.0;
        car.speed = 0.0;
        fitTyres(i, chooseCompound(i));
        ++car.stops;
        car.tyreStop = false;
        planNextStop(i);
    }
    if (before < kPitExitAfterLine && after >= kPitExitAfterLine) {
        if (car.pitSpeedExcess > 0.0) { // measured over the whole pit lane
            addIncident(Incident::Type::PitLaneSpeeding, i, i, 1.0);
            incidents_.back().measure = car.pitSpeedExcess;
            car.pitSpeedExcess = 0.0;
        }
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

void Sim::releaseLappedCars() {
    // B5.13.4 c: every lapped car in the queue passes the cars on the lead lap and the Safety Car,
    // then rejoins the back. Lapped (sim simplification): a lap or more behind the leader, or on the
    // road between the leader and the Safety Car. Only cars inside the queue need to pass: from the
    // Safety Car back to the last car on the lead lap.
    const double lapLength = track_.length();
    int leader = -1;
    for (int i = 0; i < static_cast<int>(cars_.size()); ++i) {
        if (!outOfRace(i) && (leader < 0 || cars_[i].distance > cars_[leader].distance)) {
            leader = i;
        }
    }
    if (leader < 0) {
        return;
    }
    const double leaderToSafetyCar = track_.wrap(safetyCar_.distance - cars_[leader].distance);
    auto lapped = [&](int i) {
        const double behindLeader = cars_[leader].distance - cars_[i].distance;
        const bool betweenLeaderAndSafetyCar =
            i != leader && track_.wrap(cars_[i].distance - cars_[leader].distance) < leaderToSafetyCar;
        return behindLeader >= lapLength || betweenLeaderAndSafetyCar;
    };
    double queueLength = 0.0; // road distance from the Safety Car back to the last lead-lap car
    for (int i = 0; i < static_cast<int>(cars_.size()); ++i) {
        if (!outOfRace(i) && !cars_[i].inPit && !lapped(i)) {
            queueLength = std::max(queueLength, track_.wrap(safetyCar_.distance - cars_[i].distance));
        }
    }
    for (int i = 0; i < static_cast<int>(cars_.size()); ++i) {
        if (!outOfRace(i) && !cars_[i].inPit && cars_[i].spinTimer <= 0.0 && lapped(i) &&
            track_.wrap(safetyCar_.distance - cars_[i].distance) < queueLength) {
            cars_[i].unlapping = true;
        }
    }
    safetyCar_.lappedCarsReleased = true;
    safetyCar_.linesUntilIn = config_.rules.safetyCarInLapsAfterLappedCars; // B5.13.5 b
}

void Sim::updateWeather() {
    if (config_.weather.points.empty()) {
        return;
    }
    rain_ = rainAt(config_.weather, time_, weatherShift_, weatherScale_);
    const double target = std::min(1.0, rain_ * kWetTargetPerRain);
    auto approach = [&](double& wetness, double dryingPerSecond) {
        wetness = wetness < target ? std::min(target, wetness + kWettingPerSecond * kDt)
                                   : std::max(target, wetness - dryingPerSecond * kDt);
    };
    approach(wetness_, kDryingOffLinePerSecond);
    approach(lineWetness_, kDryingLinePerSecond);
}

double Sim::grip(int /*i*/) const {
    return 1.0 - kSlickWetGripLoss * lineWetness_; // slicks; wet tyres follow in 9.3
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
    if (car.unlapping) {
        return true; // B5.13.4 c: required to pass
    }
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
        if (cars_[i].unlapping || cars_[i].rejoining) {
            return performance(i); // B5.13.4 c: "appropriate speed" to reach the back of the queue in time
        }
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
    const double tyres = tyreFactor(track_.compounds(), car.compound, car.tyreWear);
    return entry.teamPace * entry.skill * car.form * tyres * fuel;
}

double Sim::speedCap(int i, double gapAhead) const {
    const Car& car = cars_[i];
    const TrackSegment& seg = track_.segments()[car.segment];
    double cap = seg.speed * paceFactor(i) * grip(i);
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
    const double pace = paceFactor(i) * grip(i);

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
        if (car.unlapping && !car.inPit) {
            if (a >= 0 && car.overtakeTarget != a && gap[i] < kFollowGap && scGap > gap[i]) {
                car.overtakeTarget = a; // the lead-lap car stays on the racing line
                car.overtakeTimer = kOvertakeTimeout;
            }
            continue; // past the Safety Car on the side
        }
        if (car.rejoining && gap[i] < kFollowGap) {
            car.rejoining = false; // at the back of the queue
        }
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
    updateWeather();
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
        const bool atEntry = track_.wrap(previous[i]) < pitLaneEntry && track_.wrap(car.distance) >= pitLaneEntry;
        if (!atEntry || outOfRace(i) || car.spinTimer > 0.0) {
            continue;
        }
        const bool serve = mustServe && state_ == RaceState::Green && !pitEntryClosed_; // B1.6.4: penalties wait
        bool tyres = !serve && wantsTyreStop(i);
        if (tyres && pitEntryClosed_) {
            tyres = rng_.uniform() < kClosedEntryIgnoreChance; // usually the stop waits
            if (tyres) {
                addIncident(Incident::Type::PitEntryClosed, i, i, 1.0);
            }
        }
        if (serve || tyres) {
            car.tyreStop = tyres;
            car.inPit = true;
            car.stopDone = false;
            car.releaseChecked = false;
            car.pitSpeedExcess = rng_.uniform() < kSpeedingChance ? rng_.uniform(kSpeedingMinKmh, kSpeedingMaxKmh) : 0.0;
            car.overtakeTarget = -1;
            car.unlapping = false;
            car.rejoining = false;
            car.lateral = kPitLateral;
        }
    }
    if (safetyCar_.onTrack) {
        const double before = track_.wrap(safetyCar_.distance);
        safetyCar_.distance += safetyCar_.speed * kDt;
        const double after = track_.wrap(safetyCar_.distance);
        const double pitEntry = lapLength - kPitEntryBeforeLine;
        if (after < before && safetyCar_.linesUntilIn > 0 && --safetyCar_.linesUntilIn == 0) {
            safetyCar_.inThisLap = true; // B5.13.5 b: in at the end of the following lap
        }
        if (safetyCar_.inThisLap && before < pitEntry && after >= pitEntry) {
            safetyCar_ = SafetyCar{};
            for (Car& car : cars_) {
                car.unlapping = false;
                car.rejoining = false;
            }
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
        if (car.unlapping && safetyCar_.onTrack && track_.wrap(car.distance - safetyCar_.distance) < kUnlappedAhead) {
            car.unlapping = false; // past the Safety Car: on to the back of the queue
            car.rejoining = true;
        }
        car.lateral = car.overtakeTarget >= 0 || car.unlapping ? kOvertakeLateral : 0.0;
        if (car.overtakeTarget < 0) {
            const double g = ahead[i] >= 0 ? track_.wrap(cars_[ahead[i]].distance - car.distance)
                                           : std::numeric_limits<double>::infinity();
            const double scGap = car.unlapping ? std::numeric_limits<double>::infinity() : safetyCarGap(i);
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

    // Track limits (B1.8.6): every offence deletes the lap and counts; the warning offence brings
    // the black-and-white flag, every one after it is an incident for the stewards.
    if (seg.trackLimits && state_ == RaceState::Green && !outOfRace(i) && car.spinTimer <= 0.0 &&
        rng_.uniform() < kTrackLimitsChance * std::pow(entries_[i].aggression, 4.0) * (1.0 + kTrackLimitsTyreWear * car.tyreWear)) {
        ++stats_.trackLimitOffences;
        ++car.trackLimitStrikes;
        if (car.trackLimitStrikes >= config_.rules.trackLimitsPenaltyFrom) {
            addIncident(Incident::Type::TrackLimits, i, i, 1.0);
            incidents_.back().measure = car.trackLimitStrikes;
        }
    }

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
    const double chance = kErrorChance * (2.0 - entries_[i].skill) * (1.0 + 0.02 * car.tyreWear) *
                          (1.0 + kWetErrorFactor * lineWetness_);
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
            // B6.3.6: two dry compounds, otherwise disqualification (no wet tyres in the sim yet).
            const auto used = static_cast<int>(std::bitset<3>(car.compoundsUsed).count());
            if (used < config_.rules.minDryCompounds) {
                addIncident(Incident::Type::CompoundRule, i, i, 1.0);
                incidents_.back().measure = used;
            }
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
        if (ca.disqualified != cb.disqualified) {
            return cb.disqualified; // listed after everyone else
        }
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
    snapshot_.rain = rain_;
    snapshot_.trackWetness = wetness_;
    snapshot_.lineWetness = lineWetness_;
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
    snapshot_.pitEntryClosed = pitEntryClosed_;
    snapshot_.lappedCarsReleased = safetyCar_.lappedCarsReleased;
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
        cs.compound = car.compound;
        cs.tyreAge = static_cast<int>(car.tyreWear);
        cs.pitStops = car.stops;
        cs.compoundsUsed = static_cast<int>(std::bitset<3>(car.compoundsUsed).count());
        cs.trackLimitStrikes = car.trackLimitStrikes;
        cs.unlapping = car.unlapping;
        cs.disqualified = car.disqualified;
        cs.blackWhiteFlag = car.trackLimitStrikes >= config_.rules.trackLimitsBlackWhiteAt;
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
