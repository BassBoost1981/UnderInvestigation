#include "UISimCareer.h"

#include "UISimSession.h"

THIRD_PARTY_INCLUDES_START
#include "uisim/Career.h"
#include "uisim/DataLoader.h"
#include "uisim/Season.h"
#include "uisim/Sim.h"
THIRD_PARTY_INCLUDES_END

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
	FString CareerText(const std::string& Text)
	{
		return FString(UTF8_TO_TCHAR(Text.c_str()));
	}
}

struct FUISimCareer::FState
{
	std::filesystem::path DataDir;
	std::filesystem::path SavePath;
	uisim::SeasonCalendar Calendar;
	std::vector<uisim::Entry> Entries;
	uisim::RaceRules Rules;
	uisim::Career Career;
	std::vector<int> Laps;            // per race of the calendar
	std::vector<std::string> Circuits; // track names
};

FUISimCareer::FUISimCareer() = default;
FUISimCareer::~FUISimCareer() = default;

FUISimCareer& FUISimCareer::Get()
{
	static FUISimCareer Instance;
	return Instance;
}

bool FUISimCareer::Open(const FString& DataDir, const FString& SeasonFile, const FString& EntriesFile, const FString& SaveFile,
	int64 Seed, bool bNew, FString& OutError)
{
	try
	{
		// TCHAR is wchar_t on Windows, so non-ASCII paths survive.
		TUniquePtr<FState> New = MakeUnique<FState>();
		New->DataDir = std::filesystem::path(*DataDir);
		New->SavePath = std::filesystem::path(*SaveFile);
		New->Calendar = uisim::loadSeason(New->DataDir / "seasons" / std::filesystem::path(*SeasonFile));
		New->Entries = uisim::loadEntries(New->DataDir / "entries" / std::filesystem::path(*EntriesFile));
		New->Rules = uisim::loadRules(New->DataDir / "rules" / "fia-f1-2026.json");
		for (const uisim::SeasonRace& Race : New->Calendar.races)
		{
			const uisim::Track Track = uisim::loadTrack(New->DataDir / "tracks" / Race.track);
			New->Laps.push_back(Race.laps > 0 ? Race.laps : uisim::raceLaps(Track.length(), New->Rules, Race.shortDistance));
			New->Circuits.push_back(Track.name());
		}
		if (!bNew && std::filesystem::exists(New->SavePath))
		{
			New->Career = uisim::loadCareer(New->SavePath, New->Calendar, New->Entries);
		}
		else
		{
			New->Career = uisim::newCareer(New->Calendar, New->Entries, static_cast<std::uint64_t>(Seed));
		}
		State = MoveTemp(New);
		return true;
	}
	catch (const std::exception& Error)
	{
		OutError = CareerText(Error.what());
		return false;
	}
}

bool FUISimCareer::IsFinished() const
{
	return State && uisim::careerFinished(State->Career, State->Calendar);
}

FString FUISimCareer::SeasonName() const
{
	return State ? CareerText(State->Calendar.name) : FString();
}

int32 FUISimCareer::RaceCount() const
{
	return State ? static_cast<int32>(State->Calendar.races.size()) : 0;
}

int32 FUISimCareer::NextRace() const
{
	return State ? State->Career.nextRace : 0;
}

int64 FUISimCareer::Seed() const
{
	return State ? static_cast<int64>(State->Career.seed) : 0;
}

double FUISimCareer::Rating() const
{
	return State ? uisim::careerRating(State->Career) : 0.0;
}

TArray<FUISimSeasonRace> FUISimCareer::Calendar() const
{
	TArray<FUISimSeasonRace> Out;
	if (!State)
	{
		return Out;
	}
	for (int32 Index = 0; Index < RaceCount(); ++Index)
	{
		const uisim::SeasonRace& Race = State->Calendar.races[Index];
		FUISimSeasonRace& Line = Out.AddDefaulted_GetRef();
		Line.Name = CareerText(Race.name);
		Line.Circuit = CareerText(State->Circuits[Index]);
		Line.Laps = State->Laps[Index];
		Line.bRain = !Race.weather.empty();
		Line.bDone = Index < static_cast<int32>(State->Career.races.size());
		if (Line.bDone)
		{
			const uisim::CareerRace& Done = State->Career.races[Index];
			Line.Rating = Done.overall;
			if (!Done.podium.empty())
			{
				Line.Winner = CareerText(State->Entries[Done.podium.front()].name);
			}
			for (int Entry : Done.suspended)
			{
				Line.SatOut.Add(CareerText(State->Entries[Entry].name));
			}
		}
	}
	return Out;
}

TArray<FUISimStanding> FUISimCareer::Drivers() const
{
	TArray<FUISimStanding> Out;
	if (!State)
	{
		return Out;
	}
	const uisim::Career& Career = State->Career;
	for (int Index : uisim::standings(Career.championship.drivers))
	{
		const uisim::ChampionshipLine& Driver = Career.championship.drivers[Index];
		FUISimStanding& Line = Out.AddDefaulted_GetRef();
		Line.Name = CareerText(Driver.name);
		Line.Team = CareerText(State->Entries[Index].team);
		Line.Points = Driver.points;
		Line.Wins = Driver.places.empty() ? 0 : Driver.places.front();
		Line.PenaltyPoints = Career.penaltyPoints[Index];
		Line.bSuspended = std::find(Career.suspended.begin(), Career.suspended.end(), Index) != Career.suspended.end();
	}
	return Out;
}

TArray<FUISimStanding> FUISimCareer::Teams() const
{
	TArray<FUISimStanding> Out;
	if (!State)
	{
		return Out;
	}
	const std::vector<uisim::ChampionshipLine>& Teams = State->Career.championship.teams;
	for (int Index : uisim::standings(Teams))
	{
		FUISimStanding& Line = Out.AddDefaulted_GetRef();
		Line.Name = CareerText(Teams[Index].name);
		Line.Points = Teams[Index].points;
		Line.Wins = Teams[Index].places.empty() ? 0 : Teams[Index].places.front();
	}
	return Out;
}

FString FUISimCareer::StandingsLine(bool bTeams) const
{
	// Same layout as printStandings in core/tools/runner/main.cpp.
	FString Line = bTeams ? TEXT("Teams:") : TEXT("Drivers:");
	const TArray<FUISimStanding> Lines = bTeams ? Teams() : Drivers();
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		Line += FString::Printf(TEXT("%s %d. %s %d"), Index == 0 ? TEXT("") : TEXT(","), Index + 1, *Lines[Index].Name,
			Lines[Index].Points);
	}
	return Line;
}

bool FUISimCareer::StartNextRace(FUISimSession& Session, FString& OutError)
{
	if (!State)
	{
		OutError = TEXT("No career is open");
		return false;
	}
	try
	{
		Session.Adopt(MakeUnique<uisim::Sim>(
			uisim::makeCareerRace(State->Career, State->Calendar, State->DataDir, State->Entries, State->Rules)));
		return true;
	}
	catch (const std::exception& Error)
	{
		OutError = CareerText(Error.what());
		return false;
	}
}

bool FUISimCareer::FinishRace(const FUISimSession& Session, FString& OutError)
{
	if (!State || !Session.Sim)
	{
		OutError = TEXT("No career race is running");
		return false;
	}
	try
	{
		uisim::finishCareerRace(State->Career, State->Calendar, *Session.Sim, State->Entries, State->Rules);
		std::filesystem::create_directories(State->SavePath.parent_path());
		uisim::saveCareer(State->SavePath, State->Career);
		return true;
	}
	catch (const std::exception& Error)
	{
		OutError = CareerText(Error.what());
		return false;
	}
}

bool FUISimCareer::StartNewSeason(FString& OutError)
{
	if (!State)
	{
		OutError = TEXT("No career is open");
		return false;
	}
	try
	{
		State->Career = uisim::newCareer(State->Calendar, State->Entries, State->Career.seed + 1);
		std::filesystem::create_directories(State->SavePath.parent_path());
		uisim::saveCareer(State->SavePath, State->Career);
		return true;
	}
	catch (const std::exception& Error)
	{
		OutError = CareerText(Error.what());
		return false;
	}
}
