#include "UISimSession.h"

#include "Modules/ModuleManager.h"

THIRD_PARTY_INCLUDES_START
#include "uisim/DataLoader.h"
#include "uisim/Rating.h"
#include "uisim/Sim.h"
THIRD_PARTY_INCLUDES_END

#include <exception>
#include <filesystem>

IMPLEMENT_MODULE(FDefaultModuleImpl, UISimBridge)

namespace
{
	constexpr double MetresToUnreal = 100.0;
	constexpr double LateralRangeMetres = 4.0; // snapshot lateral 1.0 = 4 m left of the racing line

	FTransform ToUnreal(const uisim::TrackPose& Pose)
	{
		const FVector Location(Pose.x * MetresToUnreal, -Pose.y * MetresToUnreal, 0.0);
		const FRotator Rotation(0.0, -FMath::RadiansToDegrees(Pose.heading), 0.0);
		return FTransform(Rotation, Location);
	}

	FString FromUtf8(const std::string& Text)
	{
		return FString(UTF8_TO_TCHAR(Text.c_str()));
	}
}

FUISimSession::FUISimSession() = default;
FUISimSession::~FUISimSession() = default;

bool FUISimSession::Start(const FString& TrackFile, const FString& EntriesFile, const FString& RulesFile, int32 Laps,
	int64 Seed, FString& OutError, const FString& WeatherFile)
{
	try
	{
		// TCHAR is wchar_t on Windows, so non-ASCII paths survive.
		uisim::Track Track = uisim::loadTrack(std::filesystem::path(*TrackFile));
		std::vector<uisim::Entry> Entries = uisim::loadEntries(std::filesystem::path(*EntriesFile));
		uisim::RaceConfig Config;
		Config.laps = Laps;
		Config.seed = static_cast<std::uint64_t>(Seed);
		Config.rules = uisim::loadRules(std::filesystem::path(*RulesFile));
		if (!WeatherFile.IsEmpty())
		{
			Config.weather = uisim::loadWeather(std::filesystem::path(*WeatherFile));
		}
		Sim = MakeUnique<uisim::Sim>(std::move(Track), std::move(Entries), Config);
	}
	catch (const std::exception& Error)
	{
		OutError = FromUtf8(Error.what());
		Sim.Reset();
		return false;
	}

	EntryList.Reset();
	for (const uisim::Entry& Entry : Sim->entries())
	{
		FUISimEntry& Out = EntryList.AddDefaulted_GetRef();
		Out.Number = Entry.number;
		Out.Name = FromUtf8(Entry.name);
		Out.Code = FromUtf8(Entry.code);
		Out.Team = FromUtf8(Entry.team);
	}
	CopySnapshot();
	return true;
}

void FUISimSession::Step()
{
	if (Sim && !Sim->finished())
	{
		Sim->step();
		CopySnapshot();
	}
}

double FUISimSession::TickSeconds()
{
	return uisim::Sim::kDt;
}

FString FUISimSession::TrackName() const
{
	return Sim ? FromUtf8(Sim->track().name()) : FString();
}

int32 FUISimSession::Overtakes() const
{
	return Sim ? Sim->stats().overtakes : 0;
}

double FUISimSession::TrackLength() const
{
	return Sim ? Sim->track().length() : 0.0;
}

int32 FUISimSession::FlagViolations() const
{
	return Sim ? Sim->stats().flagViolations : 0;
}

int32 FUISimSession::MarshalSectorCount() const
{
	return Sim ? Sim->track().marshalSectorCount() : 0;
}

bool FUISimSession::ReplayAt(double Time, TArray<FUISimReplayCar>& OutCars, double& OutSafetyCarDistance,
	bool& bOutSafetyCarOnTrack) const
{
	if (!Sim || Sim->replayFrames().empty())
	{
		return false;
	}
	const auto& Frames = Sim->replayFrames();
	const std::size_t A = Sim->replayFrameAt(Time);
	const std::size_t B = FMath::Min(A + 1, Frames.size() - 1);
	const double Span = Frames[B].time - Frames[A].time;
	const double Alpha = Span > 0.0 ? FMath::Clamp((Time - Frames[A].time) / Span, 0.0, 1.0) : 0.0;

	const int32 CarCount = EntryList.Num();
	OutCars.SetNum(CarCount);
	for (int32 Entry = 0; Entry < CarCount; ++Entry)
	{
		const uisim::ReplayCar& From = Sim->replayCar(A, Entry);
		const uisim::ReplayCar& To = Sim->replayCar(B, Entry);
		OutCars[Entry].Distance = FMath::Lerp(From.distance, To.distance, Alpha);
		OutCars[Entry].Lateral = FMath::Lerp(static_cast<double>(From.lateral), static_cast<double>(To.lateral), Alpha);
		OutCars[Entry].Speed = FMath::Lerp(static_cast<double>(From.speed), static_cast<double>(To.speed), Alpha);
	}
	OutSafetyCarDistance = FMath::Lerp(Frames[A].safetyCarDistance, Frames[B].safetyCarDistance, Alpha);
	bOutSafetyCarOnTrack = Frames[A].safetyCarOnTrack;
	return true;
}

void FUISimSession::IssueVerdict(int32 IncidentId, int32 Car, EUISimPenalty Penalty)
{
	if (!Sim)
	{
		return;
	}
	uisim::Command Command;
	Command.type = uisim::Command::Type::IssueVerdict;
	Command.incidentId = IncidentId;
	Command.car = Car;
	Command.penalty = static_cast<uisim::Penalty>(Penalty);
	Sim->command(Command);
	if (Sim->finished())
	{
		Sim->step(); // after the flag the sim no longer ticks on its own: apply now
		CopySnapshot();
	}
}

void FUISimSession::InjectCrash(int32 Car)
{
	if (!Sim)
	{
		return;
	}
	uisim::Command Command;
	Command.type = uisim::Command::Type::InjectAccident;
	Command.car = Car;
	Command.accident = uisim::AccidentType::Crash;
	Sim->command(Command);
}

const FUISimVerdict* FUISimSession::VerdictFor(int32 IncidentId) const
{
	return VerdictList.FindByPredicate([IncidentId](const FUISimVerdict& V) { return V.IncidentId == IncidentId; });
}

FUISimRating FUISimSession::Rating() const
{
	FUISimRating Out;
	if (!Sim)
	{
		return Out;
	}
	const uisim::RaceRating In = uisim::rateRace(*Sim);
	Out.Fairness = In.fairness;
	Out.Consistency = In.consistency;
	Out.RaceQuality = In.raceQuality;
	Out.Safety = In.safety;
	Out.FollowUpAccidents = In.followUpAccidents;
	Out.Overall = In.overall;
	for (const uisim::IncidentRating& R : In.incidents)
	{
		Out.Incidents.Add({R.incidentId, R.expected.car, static_cast<EUISimPenalty>(R.expected.penalty), R.decided,
			R.givenCar, static_cast<EUISimPenalty>(R.givenPenalty), R.score});
	}
	return Out;
}

void FUISimSession::Send(int32 Type, int32 Sector, EUISimFlag Flag, bool bWetTyres)
{
	if (!Sim)
	{
		return;
	}
	uisim::Command Command;
	Command.type = static_cast<uisim::Command::Type>(Type);
	Command.sector = Sector;
	Command.flag = static_cast<uisim::Flag>(Flag);
	Command.wetTyres = bWetTyres;
	try
	{
		Sim->command(Command);
	}
	catch (const std::exception& Error)
	{
		UE_LOG(LogTemp, Warning, TEXT("UISim command rejected: %s"), *FromUtf8(Error.what()));
	}
}

using ECommand = uisim::Command::Type;
void FUISimSession::SetSectorFlag(int32 Sector, EUISimFlag Flag) { Send(static_cast<int32>(ECommand::SetSectorFlag), Sector, Flag); }
void FUISimSession::DeployVirtualSafetyCar() { Send(static_cast<int32>(ECommand::DeployVirtualSafetyCar)); }
void FUISimSession::EndVirtualSafetyCar() { Send(static_cast<int32>(ECommand::EndVirtualSafetyCar)); }
void FUISimSession::DeploySafetyCar() { Send(static_cast<int32>(ECommand::DeploySafetyCar)); }
void FUISimSession::RecallSafetyCar() { Send(static_cast<int32>(ECommand::RecallSafetyCar)); }
void FUISimSession::RedFlag() { Send(static_cast<int32>(ECommand::RedFlag)); }
void FUISimSession::Restart(bool bWetTyres) { Send(static_cast<int32>(ECommand::Restart), 0, EUISimFlag::Green, bWetTyres); }
void FUISimSession::SafetyCarStart(bool bWetTyres) { Send(static_cast<int32>(ECommand::SafetyCarStart), 0, EUISimFlag::Green, bWetTyres); }
double FUISimSession::ForecastRain(double Time) const { return Sim ? Sim->forecastRain(Time) : 0.0; }
FString FUISimSession::WeatherName() const { return Sim ? FromUtf8(Sim->weatherName()) : FString(); }
void FUISimSession::LappedCarsMayOvertake() { Send(static_cast<int32>(ECommand::LappedCarsMayOvertake)); }
void FUISimSession::ClosePitEntry() { Send(static_cast<int32>(ECommand::ClosePitEntry)); }
void FUISimSession::OpenPitEntry() { Send(static_cast<int32>(ECommand::OpenPitEntry)); }

TArray<FVector> FUISimSession::CenterlineWorld() const
{
	TArray<FVector> Points;
	if (Sim)
	{
		for (const uisim::TrackPose& Pose : Sim->track().centerline())
		{
			Points.Add(ToUnreal(Pose).GetLocation());
		}
	}
	return Points;
}

FTransform FUISimSession::TrackTransform(double Distance, double LateralMetres) const
{
	return Sim ? ToUnreal(Sim->track().pose(Distance, LateralMetres)) : FTransform::Identity;
}

FTransform FUISimSession::CarTransform(double Distance, double Lateral) const
{
	return TrackTransform(Distance, Lateral * LateralRangeMetres);
}

void FUISimSession::CopySnapshot()
{
	const uisim::SimSnapshot& In = Sim->snapshot();
	CurrentSnapshot.Time = In.time;
	CurrentSnapshot.LeaderLap = In.leaderLap;
	CurrentSnapshot.TotalLaps = In.totalLaps;
	CurrentSnapshot.bChequered = In.chequered;
	CurrentSnapshot.bFinished = In.finished;
	CurrentSnapshot.State = static_cast<EUISimRaceState>(In.state);
	CurrentSnapshot.SectorFlags.SetNum(static_cast<int32>(In.sectorFlags.size()));
	for (int32 Index = 0; Index < CurrentSnapshot.SectorFlags.Num(); ++Index)
	{
		CurrentSnapshot.SectorFlags[Index] = static_cast<EUISimFlag>(In.sectorFlags[Index]);
	}
	// Incidents only ever grow: copy the new ones.
	for (std::size_t Index = static_cast<std::size_t>(IncidentList.Num()); Index < Sim->incidents().size(); ++Index)
	{
		const uisim::Incident& Incident = Sim->incidents()[Index];
		FUISimIncident& Out = IncidentList.AddDefaulted_GetRef();
		Out.Id = Incident.id;
		Out.Type = static_cast<EUISimIncidentType>(Incident.type);
		Out.Time = Incident.time;
		Out.Car = Incident.car;
		Out.OtherCar = Incident.otherCar;
		Out.Distance = Incident.distance;
		Out.GroundTruthBlame = Incident.blame;
	}
	for (std::size_t Index = static_cast<std::size_t>(VerdictList.Num()); Index < Sim->verdicts().size(); ++Index)
	{
		const uisim::Verdict& Verdict = Sim->verdicts()[Index];
		VerdictList.Add({Verdict.incidentId, Verdict.car, static_cast<EUISimPenalty>(Verdict.penalty), Verdict.afterRace});
	}
	for (std::size_t Index = static_cast<std::size_t>(RadioList.Num()); Index < Sim->radio().size(); ++Index)
	{
		const uisim::RadioMessage& M = Sim->radio()[Index];
		RadioList.Add({M.time, static_cast<EUISimRadio>(M.type), M.marshalPost, M.car, M.accidentId, M.onTrack});
	}
	CurrentSnapshot.Hazards.Reset();
	for (const uisim::HazardState& H : In.hazards)
	{
		CurrentSnapshot.Hazards.Add({H.id, H.car, H.trackPosition, H.onTrack, H.recovering});
	}
	CurrentSnapshot.bSafetyCarOnTrack = In.safetyCarOnTrack;
	CurrentSnapshot.bSafetyCarInThisLap = In.safetyCarInThisLap;
	CurrentSnapshot.bLappedCarsReleased = In.lappedCarsReleased;
	CurrentSnapshot.bPitEntryClosed = In.pitEntryClosed;
	CurrentSnapshot.Rain = In.rain;
	CurrentSnapshot.TrackWetness = In.trackWetness;
	CurrentSnapshot.LineWetness = In.lineWetness;
	CurrentSnapshot.StandingWater = In.standingWater;
	CurrentSnapshot.bWetTyresMandated = In.wetTyresMandated;
	CurrentSnapshot.SafetyCarDistance = In.safetyCarDistance;
	CurrentSnapshot.Cars.SetNum(static_cast<int32>(In.cars.size()));
	for (int32 Index = 0; Index < CurrentSnapshot.Cars.Num(); ++Index)
	{
		const uisim::CarState& Car = In.cars[Index];
		FUISimCar& Out = CurrentSnapshot.Cars[Index];
		Out.EntryIndex = Car.entryIndex;
		Out.Position = Car.position;
		Out.GridPosition = Car.gridPosition;
		Out.LapsCompleted = Car.lapsCompleted;
		Out.Distance = Car.distance;
		Out.Speed = Car.speed;
		Out.Lateral = Car.lateral;
		Out.LastLapTime = Car.lastLapTime;
		Out.BestLapTime = Car.bestLapTime;
		Out.LastLineTime = Car.lastLineTime;
		Out.GapToLeader = Car.gapToLeader;
		Out.Interval = Car.interval;
		Out.LapsDown = Car.lapsDown;
		Out.bFinished = Car.finished;
		Out.TimePenalty = Car.timePenalty;
		Out.PendingPenalty = static_cast<EUISimPenalty>(Car.pendingPenalty);
		Out.bInPitLane = Car.inPitLane;
		Out.bRetired = Car.retired;
		Out.bDisqualified = Car.disqualified;
		Out.bUnlapping = Car.unlapping;
		Out.bBlueFlag = Car.blueFlag;
		Out.bOvertakeActive = Car.overtakeActive;
		Out.Compound = static_cast<int32>(Car.compound);
		Out.TyreAge = Car.tyreAge;
		Out.PitStops = Car.pitStops;
		Out.TrackLimitStrikes = Car.trackLimitStrikes;
		Out.bBlackWhiteFlag = Car.blackWhiteFlag;
	}
}

double FUISimSession::BoxOffset(int32 EntryIndex) const
{
	return Sim ? Sim->boxOffset(EntryIndex) : 0.0;
}
