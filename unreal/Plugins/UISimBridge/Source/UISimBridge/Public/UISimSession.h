#pragma once

#include "CoreMinimal.h"

namespace uisim { class Sim; }

enum class EUISimPenalty : uint8 { None, Warning, FiveSeconds, TenSeconds, DriveThrough, StopAndGo, Disqualification };

// Unreal-side copies of the core snapshot (see core/include/uisim/Snapshot.h).
struct FUISimCar
{
	double TimePenalty = 0.0;
	EUISimPenalty PendingPenalty = EUISimPenalty::None;
	bool bInPitLane = false;
	bool bRetired = false;
	bool bDisqualified = false;
	bool bUnlapping = false; // lapped car passing the queue and the SC (B5.13.4 c)
	bool bBlueFlag = false;
	bool bOvertakeActive = false; // Overtake mode (B7.2) usable this lap
	int32 Compound = 1;      // 0 soft, 1 medium, 2 hard, 3 intermediate, 4 wet
	int32 TyreAge = 0;       // laps on the current set
	int32 PitStops = 0;
	int32 TrackLimitStrikes = 0;
	bool bBlackWhiteFlag = false;
	int32 EntryIndex = 0;
	int32 Position = 0;
	int32 GridPosition = 0;
	int32 LapsCompleted = 0;
	double Distance = 0.0; // metres since the line
	double Speed = 0.0;    // m/s
	double Lateral = 0.0;  // -1..1, 0 = racing line
	double LastLapTime = 0.0;
	double BestLapTime = 0.0;
	double LastLineTime = 0.0;
	double GapToLeader = 0.0;
	double Interval = 0.0;
	int32 LapsDown = 0;
	bool bFinished = false;
};

enum class EUISimFlag : uint8 { Green, Yellow, DoubleYellow };

// Stranded car / accident site (core HazardState).
struct FUISimHazard
{
	int32 Id = 0;
	int32 Car = 0;
	double TrackPosition = 0.0;
	bool bOnTrack = false;
	bool bRecovering = false;
};
enum class EUISimRaceState : uint8 { Green, VirtualSafetyCar, SafetyCar, RedFlag };

struct FUISimSnapshot
{
	double Time = 0.0;
	int32 LeaderLap = 0;
	int32 TotalLaps = 0;
	bool bChequered = false;
	bool bFinished = false;
	EUISimRaceState State = EUISimRaceState::Green;
	TArray<EUISimFlag> SectorFlags; // one per marshal sector
	TArray<FUISimHazard> Hazards;   // not yet recovered
	bool bSafetyCarOnTrack = false;
	bool bSafetyCarInThisLap = false;
	bool bLappedCarsReleased = false; // "LAPPED CARS MAY NOW OVERTAKE" sent
	bool bPitEntryClosed = false;
	double Rain = 0.0;          // 0 dry .. 1 downpour (M9.2)
	double TrackWetness = 0.0;  // off the racing line
	double LineWetness = 0.0;   // on the racing line
	double StandingWater = 0.0; // aquaplaning risk (M9.4)
	bool bWetTyresMandated = false; // B6.3.7
	bool bGridStart = false; // standing start: cars forming on the grid or waiting for the signal (M10.3)
	int32 StartLights = 0;   // red lights lit on the gantry, all out = start signal (B5.7.1)
	bool bFormationLap = false; // the formation lap before the start is being driven (B5.6, M11.3)
	double SafetyCarDistance = 0.0;
	TArray<FUISimCar> Cars; // sorted by position
};

enum class EUISimIncidentType : uint8
{
	Collision, OvertakeUnderYellow, OvertakeUnderVsc, OvertakeUnderSafetyCar, OvertakeUnderRedFlag, OvertakeBeforeRestartLine, IgnoringBlueFlags,
	Accident, // spin/off-track/crash without contact: Car == OtherCar, review only
	PitLaneSpeeding, // Car == OtherCar
	UnsafeRelease,
	TrackLimits, // Car == OtherCar, offence after the black-and-white flag
	CompoundRule, // Car == OtherCar, finished without two dry compounds
	PitEntryClosed, // Car == OtherCar, entered the closed pit lane
	WetTyreRule, // Car == OtherCar, left the mandated wet tyres behind the SC
	FalseStart,  // Car == OtherCar, moved before the start signal (B5.11), Measure = seconds early
	GridPosition // Car == OtherCar, outside the grid box at the start signal (B5.11), Measure = metres
};

struct FUISimIncident
{
	int32 Id = 0;
	EUISimIncidentType Type = EUISimIncidentType::Collision;
	double Time = 0.0;
	int32 Car = 0;      // entry index: attacker / overtaking car
	int32 OtherCar = 0; // entry index: defender / car passed
	double Distance = 0.0;
	double GroundTruthBlame = 0.0; // never shown to the player; for rating verdicts (M5)
};

struct FUISimVerdict
{
	int32 IncidentId = 0;
	int32 Car = -1; // -1 = no further action
	EUISimPenalty Penalty = EUISimPenalty::None;
	bool bAfterRace = false;
};

struct FUISimIncidentRating
{
	int32 IncidentId = 0;
	int32 ExpectedCar = -1;
	EUISimPenalty ExpectedPenalty = EUISimPenalty::None;
	bool bDecided = false;
	int32 GivenCar = -1;
	EUISimPenalty GivenPenalty = EUISimPenalty::None;
	double Score = 0.0;
};

struct FUISimRating
{
	double Fairness = 1.0;
	double Consistency = 1.0;
	double RaceQuality = 1.0;
	double Safety = 1.0;
	int32 FollowUpAccidents = 0;
	double Overall = 100.0;
	TArray<FUISimIncidentRating> Incidents;
};

enum class EUISimRadio : uint8 { Spin, CarOffTrack, CarOnTrack, RecoveryNeedsFlag, TrackClear };

// Marshal radio message (core/include/uisim/Sim.h RadioMessage).
struct FUISimRadio
{
	double Time = 0.0;
	EUISimRadio Type = EUISimRadio::Spin;
	int32 MarshalPost = 1;
	int32 Car = 0;
	int32 AccidentId = 0;
	bool bOnTrack = false;
};

struct FUISimReplayCar
{
	double Distance = 0.0;
	double Lateral = 0.0;
	double Speed = 0.0;
};

struct FUISimEntry
{
	int32 Number = 0;
	FString Name;
	FString Code;
	FString Team;
};

// Owns one race of the core simulation and converts its state to Unreal types.
// World mapping: core metres (x, y) -> Unreal cm (X = x, Y = -y), so the layout
// keeps its handedness in Unreal's left-handed frame.
class UISIMBRIDGE_API FUISimSession
{
public:
	FUISimSession();
	~FUISimSession();
	FUISimSession(const FUISimSession&) = delete;
	FUISimSession& operator=(const FUISimSession&) = delete;

	// Loads track, entries and rules and sets up the grid. On failure returns false with a message.
	bool Start(const FString& TrackFile, const FString& EntriesFile, const FString& RulesFile, int32 Laps, int64 Seed,
		FString& OutError, const FString& WeatherFile = FString());
	bool IsStarted() const { return Sim.IsValid(); }

	// Advances the race by one fixed tick (TickSeconds()).
	void Step();
	static double TickSeconds();

	const FUISimSnapshot& Snapshot() const { return CurrentSnapshot; }
	const TArray<FUISimEntry>& Entries() const { return EntryList; }
	FString TrackName() const;
	double TrackLength() const;
	int32 Overtakes() const;
	int32 FlagViolations() const;
	int32 MarshalSectorCount() const;
	// Metres from the line of the pit box of an entry's team.
	double BoxOffset(int32 EntryIndex) const;

	// Incidents so far, in order of occurrence.
	const TArray<FUISimIncident>& Incidents() const { return IncidentList; }

	// Marshal radio log, oldest first.
	const TArray<FUISimRadio>& Radio() const { return RadioList; }
	// Scenario/test tool: `Car` (entry index) crashes on the racing line at the next step.
	void InjectCrash(int32 Car);

	// Steward decisions (one per incident) and the rating against the ground truth.
	void IssueVerdict(int32 IncidentId, int32 Car, EUISimPenalty Penalty);
	const FUISimVerdict* VerdictFor(int32 IncidentId) const;
	FUISimRating Rating() const;

	// Replay of the recorded race, interpolated between ticks. OutCars is indexed by entry.
	// Returns false before the race has started.
	bool ReplayAt(double Time, TArray<FUISimReplayCar>& OutCars, double& OutSafetyCarDistance,
		bool& bOutSafetyCarOnTrack) const;

	// Race control: applied at the start of the next Step().
	void SetSectorFlag(int32 Sector, EUISimFlag Flag);
	void DeployVirtualSafetyCar();
	void EndVirtualSafetyCar();
	void DeploySafetyCar();
	void RecallSafetyCar(bool bStanding = false); // after formation laps: standing start from the grid (B5.10.8)
	void RedFlag();
	void Restart(bool bWetTyres = false, bool bStanding = false); // B5.15.2 a: wet tyres compulsory; B5.15.4: standing start
	void SafetyCarStart(bool bWetTyres);  // before the start, B5.10
	// Checks only (M9.8): chance per team to overlook the two-compound rule; set before Start, < 0 = default.
	void SetCompoundRuleMissChance(double Chance) { CompoundRuleMissChance = Chance; }
	// Weather (M9.7): rain the race will bring at a race time, and the plan's name.
	double ForecastRain(double Time) const;
	FString WeatherName() const;
	void LappedCarsMayOvertake(); // under the SC (B5.13.4 c)
	void ClosePitEntry();         // B1.6.4
	void OpenPitEntry();

	// Centre line in Unreal world space (cm), one point every ~5 m.
	TArray<FVector> CenterlineWorld() const;
	// Point on the track: distance along the lap, sideways offset in metres (positive = left).
	FTransform TrackTransform(double Distance, double LateralMetres) const;
	// Car placement from snapshot values (Lateral in -1..1).
	FTransform CarTransform(double Distance, double Lateral) const;

private:
	friend class FUISimCareer;
	void Adopt(TUniquePtr<uisim::Sim> NewSim); // takes over a race built elsewhere (career, M10.6)
	void CopySnapshot();
	void Send(int32 Type, int32 Sector = 0, EUISimFlag Flag = EUISimFlag::Green, bool bWetTyres = false, bool bStanding = false);

	TUniquePtr<uisim::Sim> Sim;
	double CompoundRuleMissChance = -1.0;
	FUISimSnapshot CurrentSnapshot;
	TArray<FUISimEntry> EntryList;
	TArray<FUISimIncident> IncidentList;
	TArray<FUISimVerdict> VerdictList;
	TArray<FUISimRadio> RadioList;
};
