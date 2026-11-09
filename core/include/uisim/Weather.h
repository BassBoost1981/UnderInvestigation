#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace uisim {

// Rain over the race (M9.2), from data/weather/*.json. Rain intensity 0 (dry) .. 1 (downpour),
// linear between the points; before the first and after the last point the end values hold.
// No points = a dry race (the sim is then unchanged).
struct WeatherPoint {
    double time = 0.0; // race time in seconds
    double rain = 0.0; // 0..1
};

struct WeatherPlan {
    std::string name = "dry";
    std::vector<WeatherPoint> points; // sorted by time
    double variation = 0.0;           // 0..1: random shift in time and intensity, from the race seed
};

// Rain at race time t, after the race's random shift (seconds) and intensity scale.
inline double rainAt(const WeatherPlan& plan, double t, double shift = 0.0, double scale = 1.0) {
    if (plan.points.empty()) {
        return 0.0;
    }
    const double at = t - shift;
    double rain = plan.points.back().rain;
    if (at <= plan.points.front().time) {
        rain = plan.points.front().rain;
    } else {
        for (std::size_t k = 1; k < plan.points.size(); ++k) {
            const WeatherPoint& a = plan.points[k - 1];
            const WeatherPoint& b = plan.points[k];
            if (at <= b.time) {
                const double f = b.time > a.time ? (at - a.time) / (b.time - a.time) : 1.0;
                rain = a.rain + (b.rain - a.rain) * f;
                break;
            }
        }
    }
    return std::clamp(rain * scale, 0.0, 1.0);
}

} // namespace uisim
