#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

class FUISimSession;

// One race of the calendar as shown on the season screen.
struct FUISimSeasonRace
{
	FString Name;
	FString Circuit;
	int32 Laps = 0;
	bool bRain = false;  // the calendar gives this race a weather plan
	bool bDone = false;
	double Rating = 0.0; // the player's rating of this race (0..100), when done
	FString Winner;
	TArray<FString> SatOut; // suspended drivers who missed this race
};

// One line of a championship, best first.
struct FUISimStanding
{
	FString Name;
	FString Team; // drivers only
	int32 Points = 0;
	int32 Wins = 0;
	int32 PenaltyPoints = 0;  // drivers only: on the licence now
	bool bSuspended = false;  // drivers only: sits out the next race
};

// The player's career over a season (core Career.h, M10.6): calendar, championships, rating,
// penalty points, and the save file written after every race. One per process (Get()), so it
// outlives the actors of a single race.
class UISIMBRIDGE_API FUISimCareer
{
public:
	FUISimCareer();
	~FUISimCareer();
	FUISimCareer(const FUISimCareer&) = delete;
	FUISimCareer& operator=(const FUISimCareer&) = delete;

	static FUISimCareer& Get();

	// Loads calendar (file below <DataDir>/seasons), entries (below <DataDir>/entries) and rules.
	// Continues from SaveFile if it exists, unless bNew; otherwise starts a new career with Seed.
	bool Open(const FString& DataDir, const FString& SeasonFile, const FString& EntriesFile, const FString& SaveFile,
		int64 Seed, bool bNew, FString& OutError);
	bool IsOpen() const { return State.IsValid(); }
	bool IsFinished() const;

	FString SeasonName() const;
	int32 RaceCount() const;
	int32 NextRace() const; // 0-based index into Calendar(), RaceCount() when the season is over
	int64 Seed() const;
	double Rating() const;  // mean of the race ratings, 0 before the first race
	TArray<FUISimSeasonRace> Calendar() const;
	TArray<FUISimStanding> Drivers() const;
	TArray<FUISimStanding> Teams() const;
	// The standings as the console runner prints them ("Drivers: 1. Name 25, 2. ..."), to compare both.
	FString StandingsLine(bool bTeams) const;

	// Sets up the next race of the calendar in Session (without the suspended drivers).
	bool StartNextRace(FUISimSession& Session, FString& OutError);
	// Books the finished race of Session (points, rating, penalty points, suspensions) and writes the save file.
	bool FinishRace(const FUISimSession& Session, FString& OutError);
	// After the last race: a fresh career on the same calendar with the next seed.
	bool StartNewSeason(FString& OutError);

private:
	struct FState;
	TUniquePtr<FState> State;
};
