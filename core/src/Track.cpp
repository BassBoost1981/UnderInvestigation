#include "uisim/Track.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace uisim {

Track::Track(std::string name, std::vector<TrackSegment> segments, int marshalSectors)
    : name_(std::move(name)), segments_(std::move(segments)), marshalSectors_(marshalSectors) {
    if (segments_.empty()) {
        throw std::invalid_argument("Track '" + name_ + "' has no segments");
    }
    if (marshalSectors_ < 1) {
        throw std::invalid_argument("Track '" + name_ + "' needs at least one marshal sector");
    }

    double s = 0.0;
    int currentSector = 0;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        TrackSegment& seg = segments_[i];
        const std::string where = "Track '" + name_ + "', segment " + std::to_string(i) + ": ";
        if (seg.length <= 0.0 || seg.speed <= 0.0) {
            throw std::invalid_argument(where + "length and speed must be positive");
        }
        const bool sameSector = seg.sector == currentSector;
        const bool nextSector = seg.sector == currentSector + 1 && i > 0;
        if (!(sameSector || nextSector) || seg.sector >= kSectorCount) {
            throw std::invalid_argument(where + "sectors must run 1, 2, 3 in order");
        }
        if (nextSector) {
            currentSector = seg.sector;
            sectorStarts_[currentSector] = s;
        }
        seg.start = s;
        s += seg.length;
    }
    if (currentSector != kSectorCount - 1) {
        throw std::invalid_argument("Track '" + name_ + "' must have exactly 3 sectors");
    }
    length_ = s;
    buildCenterline();
}

void Track::buildCenterline() {
    double x = 0.0;
    double y = 0.0;
    double heading = 0.0;
    for (const TrackSegment& seg : segments_) {
        const int steps = std::max(1, static_cast<int>(std::ceil(seg.length / kSampleSpacing)));
        const double ds = seg.length / steps;
        const double dh = seg.turn / steps;
        for (int k = 0; k < steps; ++k) {
            centerline_.push_back({x, y, heading});
            sampleDistances_.push_back(seg.start + k * ds);
            // Midpoint integration of a constant-curvature arc.
            heading += dh / 2.0;
            x += ds * std::cos(heading);
            y += ds * std::sin(heading);
            heading += dh / 2.0;
        }
    }
    // (x, y) should be back at the origin; spread the remaining gap over the lap.
    closureError_ = std::hypot(x, y);
    for (std::size_t i = 0; i < centerline_.size(); ++i) {
        const double f = sampleDistances_[i] / length_;
        centerline_[i].x -= x * f;
        centerline_[i].y -= y * f;
    }
}

TrackPose Track::pose(double s, double lateralMetres) const {
    s = wrap(s);
    const auto it = std::upper_bound(sampleDistances_.begin(), sampleDistances_.end(), s);
    const std::size_t i = static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, it - sampleDistances_.begin() - 1));
    const std::size_t next = (i + 1) % centerline_.size();
    const double s0 = sampleDistances_[i];
    const double s1 = next == 0 ? length_ : sampleDistances_[next];
    const double f = (s - s0) / (s1 - s0);

    const TrackPose& a = centerline_[i];
    const TrackPose& b = centerline_[next];
    constexpr double kPi = 3.14159265358979323846;
    double dh = std::remainder(b.heading - a.heading, 2.0 * kPi); // shortest way round
    TrackPose p;
    p.x = a.x + (b.x - a.x) * f;
    p.y = a.y + (b.y - a.y) * f;
    p.heading = a.heading + dh * f;
    p.x -= std::sin(p.heading) * lateralMetres;
    p.y += std::cos(p.heading) * lateralMetres;
    return p;
}

int Track::segmentIndexAt(double s) const {
    const auto it = std::upper_bound(segments_.begin(), segments_.end(), s,
        [](double value, const TrackSegment& seg) { return value < seg.start; });
    return std::max(0, static_cast<int>(it - segments_.begin()) - 1);
}

int Track::marshalSectorAt(double distance) const {
    const int sector = static_cast<int>(wrap(distance) / length_ * marshalSectors_);
    return std::min(sector, marshalSectors_ - 1);
}

double Track::wrap(double distance) const {
    double s = std::fmod(distance, length_);
    if (s < 0.0) {
        s += length_;
    }
    return s;
}

} // namespace uisim
