// Qualifying session (M13.3): three segments with elimination (B2.4.2), run by the clock.
// The driving itself (track, pit lane, timing) is the race code in Sim.cpp; this file plans the runs,
// keeps the clock and the lap times and forms the classification (B2.4.3).

#include "uisim/Sim.h"

#include "SimInternal.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace uisim {
namespace {

using detail::kPitExitAfterLine;
using detail::kPitLateral;
using detail::kQualifyingSlowLapFraction;

constexpr double kReleaseSpacing = 8.0;        // seconds between two cars leaving their garages: room on the flying lap
constexpr double kFinalRunMargin = 15.0;       // the last car of the final run reaches the line this long before the time is up
constexpr double kSecondFlyingLapChance = 0.3; // a run of two flying laps instead of one
constexpr double kBreakSeconds = 30.0;         // between segments, once every car is back (B2.4.2: 7 minutes; compressed)
constexpr double kOutLapLoss = 1.05;           // accelerating and braking on the out lap, on top of the reference time
constexpr double kPitLaneExtraSeconds = 6.0;   // pulling out of the garage

} // namespace

void Sim::startQualifying() {
    if (config_.rules.qualifyingSegmentMinutes.empty()) {
        throw std::invalid_argument("Qualifying needs at least one segment");
    }
    double referenceLap = 0.0;
    for (const TrackSegment& segment : track_.segments()) {
        referenceLap += segment.length / segment.speed;
    }
    outLapEstimate_ = kPitExitAfterLine / (config_.rules.pitLaneSpeedKmh / 3.6) + kPitLaneExtraSeconds +
                      kOutLapLoss * referenceLap / kQualifyingSlowLapFraction;
    for (Car& car : cars_) {
        car.distance = car.boxOffset; // in its garage
        car.segment = track_.segmentIndexAt(track_.wrap(car.distance));
        car.lateral = kPitLateral;
        car.inPit = true;
        car.startDelay = 0.0;
        car.gridPosition = 0;
        car.quali = QualifyingPhase::Garage;
    }
    beginQualifyingSegment();
}

// Two runs per car and segment: one from the start, one timed back from the end. The order of leaving
// is drawn for each run, the cars leave kReleaseSpacing apart.
void Sim::beginQualifyingSegment() {
    const double length = config_.rules.qualifyingSegmentMinutes[static_cast<std::size_t>(qualifyingSegment_)] * 60.0;
    segmentClock_ = length;
    lastReleaseClock_ = length + kReleaseSpacing;
    qualifyingState_ = QualifyingState::Running;
    std::vector<int> active;
    for (int i = 0; i < static_cast<int>(cars_.size()); ++i) {
        if (cars_[i].eliminatedIn == 0 && !cars_[i].retired) {
            active.push_back(i);
        }
    }
    for (std::size_t run = 0; run < 2; ++run) {
        for (std::size_t k = active.size(); k > 1; --k) { // shuffle
            std::swap(active[k - 1], active[static_cast<std::size_t>(rng_.uniform() * static_cast<double>(k))]);
        }
        for (std::size_t slot = 0; slot < active.size(); ++slot) {
            Car& car = cars_[active[slot]];
            const double wait = kReleaseSpacing * static_cast<double>(slot);
            car.runAt[run] = run == 0 ? length - wait : outLapEstimate_ + kFinalRunMargin + wait;
            car.runLaps[run] = rng_.uniform() < kSecondFlyingLapChance ? 2 : 1;
            car.nextRun = 0;
        }
    }
}

void Sim::endQualifyingSegment() {
    const int n = static_cast<int>(cars_.size());
    const int segments = static_cast<int>(config_.rules.qualifyingSegmentMinutes.size());
    if (qualifyingSegment_ + 1 >= segments) {
        qualifyingState_ = QualifyingState::Over;
        return;
    }
    // B2.4.2: the slowest cars take no further part. Ten go into the last segment; the others go out in
    // equal numbers per segment (five each with 20 cars).
    const int out = std::max(0, (n - config_.rules.qualifyingFinalSegmentCars) / (segments - 1));
    std::vector<int> rank(static_cast<std::size_t>(n));
    std::iota(rank.begin(), rank.end(), 0);
    rankQualifying(rank);
    int eliminated = 0;
    for (int r = n - 1; r >= 0 && eliminated < out; --r) {
        Car& car = cars_[rank[static_cast<std::size_t>(r)]];
        if (car.eliminatedIn == 0) {
            car.eliminatedIn = qualifyingSegment_ + 1;
            ++eliminated;
        }
    }
    qualifyingState_ = QualifyingState::Break;
    breakLeft_ = kBreakSeconds;
}

void Sim::updateQualifying() {
    switch (qualifyingState_) {
    case QualifyingState::Running:
        segmentClock_ = std::max(0.0, segmentClock_ - kDt);
        for (Car& car : cars_) {
            if (car.eliminatedIn != 0 || car.retired || car.quali != QualifyingPhase::Garage || car.nextRun >= 2 ||
                segmentClock_ > car.runAt[static_cast<std::size_t>(car.nextRun)]) {
                continue;
            }
            if (segmentClock_ < outLapEstimate_) {
                car.nextRun = 2; // too late to begin another lap before the time is up
                continue;
            }
            if (lastReleaseClock_ - segmentClock_ < kReleaseSpacing) {
                continue; // wait for the car that has just left
            }
            lastReleaseClock_ = segmentClock_;
            car.flyingLapsLeft = car.runLaps[static_cast<std::size_t>(car.nextRun)];
            ++car.nextRun;
            car.quali = QualifyingPhase::OutLap;
            car.compound = Compound::Soft; // a new set for every run; sets are not counted (M13 simplification)
            car.tyreWear = 0.0;
            car.stopTimer = kDt; // the pit lane code lets it go when the fast lane is clear
        }
        if (segmentClock_ <= 0.0) {
            qualifyingState_ = QualifyingState::TimeUp;
        }
        break;
    case QualifyingState::TimeUp:
        // B4.1.2 c: laps begun in time are completed. The segment is over when every car is back.
        if (std::all_of(cars_.begin(), cars_.end(), [](const Car& car) {
                return car.eliminatedIn != 0 || car.retired || car.quali == QualifyingPhase::Garage;
            })) {
            endQualifyingSegment();
        }
        break;
    case QualifyingState::Break:
        breakLeft_ -= kDt;
        if (breakLeft_ <= 0.0) {
            ++qualifyingSegment_;
            beginQualifyingSegment();
        }
        break;
    case QualifyingState::Over:
        break;
    }
}

// A car crossed the line and completed a lap. Only laps begun and completed on the track count (B2.4.3);
// a lap may be begun only while the clock runs (B4.1.2 c).
void Sim::onQualifyingLine(int i, double time) {
    Car& car = cars_[i];
    if (car.inPit) {
        return; // through the pit lane
    }
    const bool open = qualifyingState_ == QualifyingState::Running;
    if (car.quali == QualifyingPhase::FlyingLap) {
        qualifyingLaps_.push_back(QualifyingLap{i, qualifyingSegment_, car.lastLapTime, time, true});
        if (--car.flyingLapsLeft <= 0 || !open) {
            car.quali = QualifyingPhase::InLap;
        }
    } else if (car.quali == QualifyingPhase::OutLap) {
        car.quali = open ? QualifyingPhase::FlyingLap : QualifyingPhase::InLap;
    }
}

bool Sim::qualifyingWantsPit(int i) const {
    const Car& car = cars_[i];
    return car.quali == QualifyingPhase::InLap ||
           (car.quali == QualifyingPhase::OutLap && qualifyingState_ != QualifyingState::Running); // no lap left to begin
}

// Full pace on a flying lap, and from the last timing sector of the out lap while a lap can still be begun:
// the timed lap starts at speed.
bool Sim::qualifyingPushing(int i) const {
    const Car& car = cars_[i];
    return car.quali == QualifyingPhase::FlyingLap ||
           (car.quali == QualifyingPhase::OutLap && !car.inPit && car.boundaryIndex == Track::kSectorCount - 1 &&
            qualifyingState_ == QualifyingState::Running);
}

// A car on a slow lap moves over for one that is pushing (B4.1.1: no unnecessary impeding).
bool Sim::qualifyingYields(int slow, int fast) const {
    return !qualifyingPushing(slow) && qualifyingPushing(fast);
}

std::vector<double> Sim::qualifyingBestTimes(std::vector<double>* setAt) const {
    const std::size_t segments = config_.rules.qualifyingSegmentMinutes.size();
    std::vector<double> best(cars_.size() * segments, 0.0);
    if (setAt) {
        setAt->assign(best.size(), 0.0);
    }
    for (const QualifyingLap& lap : qualifyingLaps_) {
        const std::size_t k = static_cast<std::size_t>(lap.car) * segments + static_cast<std::size_t>(lap.segment);
        if (lap.valid && (best[k] == 0.0 || lap.time < best[k])) { // equal times: the earlier lap stays
            best[k] = lap.time;
            if (setAt) {
                (*setAt)[k] = lap.setAt;
            }
        }
    }
    return best;
}

// Classification (B2.4.3): the cars still in by their time in the current segment, behind them the cars
// that went out, later segments first, by their time in the segment they went out in. Equal times: the
// one set first. Cars without a time there come behind those with one; among themselves the sim orders
// them by their times in the segments before, then by entry (the regulations name a fixed order; sim stand-in).
void Sim::rankQualifying(std::vector<int>& rank) const {
    const int segments = static_cast<int>(config_.rules.qualifyingSegmentMinutes.size());
    std::vector<double> setAt;
    const std::vector<double> best = qualifyingBestTimes(&setAt);
    std::sort(rank.begin(), rank.end(), [&](int a, int b) {
        const int stageA = cars_[a].eliminatedIn == 0 ? segments + 1 : cars_[a].eliminatedIn;
        const int stageB = cars_[b].eliminatedIn == 0 ? segments + 1 : cars_[b].eliminatedIn;
        if (stageA != stageB) {
            return stageA > stageB;
        }
        const int from = cars_[a].eliminatedIn == 0 ? qualifyingSegment_ : cars_[a].eliminatedIn - 1;
        for (int segment = from; segment >= 0; --segment) {
            const std::size_t ka = static_cast<std::size_t>(a * segments + segment);
            const std::size_t kb = static_cast<std::size_t>(b * segments + segment);
            if ((best[ka] > 0.0) != (best[kb] > 0.0)) {
                return best[ka] > 0.0;
            }
            if (best[ka] != best[kb]) {
                return best[ka] < best[kb];
            }
            if (setAt[ka] != setAt[kb]) {
                return setAt[ka] < setAt[kb];
            }
        }
        return a < b;
    });
}

std::vector<int> Sim::qualifyingOrder() const {
    std::vector<int> rank(cars_.size());
    std::iota(rank.begin(), rank.end(), 0);
    rankQualifying(rank);
    return rank;
}

void Sim::fillQualifyingSnapshot() {
    const int segments = static_cast<int>(config_.rules.qualifyingSegmentMinutes.size());
    const std::vector<double> best = qualifyingBestTimes();
    const std::vector<int> rank = qualifyingOrder();
    std::vector<CarState> byEntry(snapshot_.cars.size()); // buildSnapshot filled them in race order
    for (CarState& cs : snapshot_.cars) {
        byEntry[static_cast<std::size_t>(cs.entryIndex)] = std::move(cs);
    }
    for (std::size_t r = 0; r < rank.size(); ++r) {
        snapshot_.cars[r] = std::move(byEntry[static_cast<std::size_t>(rank[r])]);
        snapshot_.cars[r].position = static_cast<int>(r) + 1;
    }
    snapshot_.qualifying = true;
    snapshot_.qualifyingSegment = qualifyingSegment_ + 1;
    snapshot_.segmentTimeLeft = qualifyingState_ == QualifyingState::Running ? segmentClock_ : 0.0;
    snapshot_.finished = qualifyingState_ == QualifyingState::Over;
    snapshot_.chequered = false;
    snapshot_.totalLaps = 0;
    snapshot_.leaderLap = 0;
    auto countingSegment = [&](int i) { return cars_[i].eliminatedIn == 0 ? qualifyingSegment_ : cars_[i].eliminatedIn - 1; };
    for (std::size_t r = 0; r < rank.size(); ++r) {
        const int i = rank[r];
        CarState& cs = snapshot_.cars[r];
        cs.qualifyingPhase = cars_[i].quali;
        cs.eliminatedIn = cars_[i].eliminatedIn;
        cs.qualifyingTime = best[static_cast<std::size_t>(i * segments + countingSegment(i))];
        cs.bestLapTime = 0.0;
        for (int segment = 0; segment < segments; ++segment) {
            const double time = best[static_cast<std::size_t>(i * segments + segment)];
            if (time > 0.0 && (cs.bestLapTime == 0.0 || time < cs.bestLapTime)) {
                cs.bestLapTime = time;
            }
        }
        // Gaps: to the fastest and to the next car with a time in the same segment.
        cs.lapsDown = 0;
        cs.gapToLeader = 0.0;
        cs.interval = 0.0;
        if (r > 0 && cs.qualifyingTime > 0.0) {
            const CarState& first = snapshot_.cars[0];
            const CarState& ahead = snapshot_.cars[r - 1];
            if (first.qualifyingTime > 0.0 && countingSegment(first.entryIndex) == countingSegment(i)) {
                cs.gapToLeader = cs.qualifyingTime - first.qualifyingTime;
            }
            if (ahead.qualifyingTime > 0.0 && countingSegment(ahead.entryIndex) == countingSegment(i)) {
                cs.interval = cs.qualifyingTime - ahead.qualifyingTime;
            }
        }
    }
}

} // namespace uisim
