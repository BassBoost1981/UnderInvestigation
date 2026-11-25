#pragma once

// Constants shared by the source files of Sim (Sim.cpp, Qualifying.cpp). Not part of the interface.
namespace uisim::detail {

// The pit lane runs alongside the main straight, from the pit entry to the exit after the line (M5).
inline constexpr double kPitLateral = -5.0;       // snapshot lateral units: 20 m right of the racing line
inline constexpr double kPitExitAfterLine = 300.0;
inline constexpr double kFollowGap = 12.0; // metres: closer than this a car is blocked unless overtaking

// Qualifying (M13.3).
inline constexpr double kQualifyingSlowLapFraction = 0.75; // out and in laps: this share of the reference speed
inline constexpr double kQualifyingFormSpread = 0.004;     // a driver hits a lap or does not: more spread than in a race stint
inline constexpr double kQualifyingErrorFactor = 5.0;      // mistakes per slow corner on a flying lap, times the race rate (M13.4)
inline constexpr double kQualifyingExitHold = 2.0;         // metres before the pit exit where cars wait under the red flag (M13.4)

} // namespace uisim::detail
