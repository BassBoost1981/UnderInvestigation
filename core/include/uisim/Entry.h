#pragma once

#include <string>

namespace uisim {

// One car/driver combination in the race.
struct Entry {
    int number = 0;
    std::string name;
    std::string code; // three-letter timing abbreviation
    std::string team;
    double teamPace = 1.0;   // car performance multiplier, 1.0 = best
    double skill = 1.0;      // driver performance multiplier, 1.0 = best
    double aggression = 0.5; // 0..1, willingness to attempt overtakes
};

} // namespace uisim
