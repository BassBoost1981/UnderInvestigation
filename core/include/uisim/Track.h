#pragma once

#include <array>
#include <string>
#include <vector>

namespace uisim {

// One piece of the racing line. Distances in metres, speeds in m/s.
struct TrackSegment {
    double length = 0.0;
    double speed = 0.0;          // apex speed (corner) or top speed (straight)
    double overtakeChance = 0.0; // relative, 0 = no overtaking possible
    int sector = 0;              // timing sector, 0-based
    double turn = 0.0;           // heading change over the segment, radians, positive = left
    double start = 0.0;          // distance from the line, computed by Track
};

// World position on the circuit. Metres in a right-handed x/y plane (the line
// is at the origin, heading 0 = +x); heading in radians, counter-clockwise.
struct TrackPose {
    double x = 0.0;
    double y = 0.0;
    double heading = 0.0;
};

// Closed circuit described as consecutive segments along the distance s.
// M1 has no geometry; world positions are added for the UE layer in M2.
class Track {
public:
    static constexpr int kSectorCount = 3;

    // Throws std::invalid_argument for empty tracks, non-positive values,
    // sectors that do not run 1, 2, 3 in order or marshalSectors < 1.
    Track(std::string name, std::vector<TrackSegment> segments, int marshalSectors = 20);

    // Marshal sectors (flag zones) split the lap into equal parts.
    int marshalSectorCount() const { return marshalSectors_; }
    int marshalSectorAt(double distance) const;

    const std::string& name() const { return name_; }
    double length() const { return length_; }
    const std::vector<TrackSegment>& segments() const { return segments_; }

    // Segment containing s, s in [0, length).
    int segmentIndexAt(double s) const;

    // Distance at which a timing sector starts (sector 0 starts at the line).
    double sectorStart(int sector) const { return sectorStarts_[sector]; }

    // Maps any distance (also negative or beyond one lap) into [0, length).
    double wrap(double distance) const;

    // Geometry: the centre line is built from segment lengths and turns, every
    // ~kSampleSpacing metres. A closure gap (turn data not exactly closing the
    // loop) is spread linearly over the lap.
    static constexpr double kSampleSpacing = 5.0;
    const std::vector<TrackPose>& centerline() const { return centerline_; }
    double closureError() const { return closureError_; } // metres, before correction

    // Pose at distance s (any value, wrapped), offset sideways in metres (positive = left).
    TrackPose pose(double s, double lateralMetres = 0.0) const;

private:
    void buildCenterline();

    std::string name_;
    std::vector<TrackSegment> segments_;
    double length_ = 0.0;
    int marshalSectors_ = 20;
    std::array<double, kSectorCount> sectorStarts_{};
    std::vector<TrackPose> centerline_;
    std::vector<double> sampleDistances_; // s of each centre-line sample
    double closureError_ = 0.0;
};

} // namespace uisim
