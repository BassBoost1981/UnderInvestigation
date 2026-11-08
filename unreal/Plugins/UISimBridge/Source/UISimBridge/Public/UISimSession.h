#pragma once

#include "CoreMinimal.h"

namespace uisim { class Sim; }

enum class EUISimPenalty : uint8 { None, Warning, FiveSeconds, TenSeconds, DriveThrough, StopAndGo };

// Unreal-side copies of the core snapshot (see core/include/uisim/Snapshot.h).
struct FUISimCar
{
	double TimePenalty = 0.0;
	EUISimPenalty PendingPenalty = EUISimPenalty::None;
	bool bInPitLane = false;
	bool bRetired = false;
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
	bool bSafetyCarOnTrack = false;
	bool bSafetyCarInThisLap = false;
	double SafetyCarDistance = 0.0;
	TArray<FUISimCar> Cars; // sorted by position
};

enum class EUISimIncidentType : uint8
{
	Collision, OvertakeUnderYellow, OvertakeUnderVsc, OvertakeUnderSafetyCar, OvertakeUnderRedFlag, OvertakeBeforeRestartLine
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
		FString& OutError);
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
	void RecallSafetyCar();
	void RedFlag();
	void Restart();

	// Centre line in Unreal world space (cm), one point every ~5 m.
	TArray<FVector> CenterlineWorld() const;
	// Point on the track: distance along the lap, sideways offset in metres (positive = left).
	FTransform TrackTransform(double Distance, double LateralMetres) const;
	// Car placement from snapshot values (Lateral in -1..1).
	FTransform CarTransform(double Distance, double Lateral) const;

private:
	void CopySnapshot();
	void Send(int32 Type, int32 Sector = 0, EUISimFlag Flag = EUISimFlag::Green);

	TUniquePtr<uisim::Sim> Sim;
	FUISimSnapshot CurrentSnapshot;
	TArray<FUISimEntry> EntryList;
	TArray<FUISimIncident> IncidentList;
	TArray<FUISimVerdict> VerdictList;
	TArray<FUISimRadio> RadioList;
};
