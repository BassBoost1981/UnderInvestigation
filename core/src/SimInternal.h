#pragma once

// Constants shared by the source files of Sim (Sim.cpp, Qualifying.cpp). Not part of the interface.
namespace uisim::detail {

// The pit lane runs alongside the main straight, from the pit entry to the exit after the line (M5).
inline constexpr double kPitLateral = -5.0;       // snapshot lateral units: 20 m right of the racing line
inline constexpr double kPitExitAfterLine = 300.0;

// Qualifying (M13.3).
inline constexpr double kQualifyingSlowLapFraction = 0.75; // out and in laps: this share of the reference speed
inline constexpr double kQualifyingFormSpread = 0.004;     // a driver hits a lap or does not: more spread than in a race stint

} // namespace uisim::detail
