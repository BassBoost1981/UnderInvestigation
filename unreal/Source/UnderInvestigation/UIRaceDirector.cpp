#include "UIRaceDirector.h"

#include "UITimingTowerWidget.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UIIncidentWidget.h"
#include "UIRatingWidget.h"
#include "UIRadioWidget.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"

DEFINE_LOG_CATEGORY_STATIC(LogUISim, Log, All);

namespace
{
	constexpr double RoadWidthMetres = 12.0;
	constexpr int32 SamplesPerRoadPiece = 4; // centre-line samples per spline mesh (~20 m)
	constexpr double RoadHeight = 3.0;       // cm above the ground, avoids z-fighting
	const FVector CarScale(5.0, 1.9, 0.9);   // engine cube is 1 m: 5 m x 1.9 m x 0.9 m
	const double TimeScales[] = {1.0, 2.0, 5.0, 10.0, 20.0, 50.0};

	FLinearColor TeamColor(int32 TeamIndex, int32 TeamCount)
	{
		const float Hue = 360.0f * TeamIndex / FMath::Max(1, TeamCount);
		return FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue / 360.0f * 255.0f), 200, 230);
	}
}

AUIRaceDirector::AUIRaceDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

FString AUIRaceDirector::FormatRaceTime(double Seconds)
{
	const int32 Hours = static_cast<int32>(Seconds / 3600.0);
	const int32 Minutes = static_cast<int32>((Seconds - Hours * 3600.0) / 60.0);
	const double Rest = Seconds - Hours * 3600.0 - Minutes * 60.0;
	return Hours > 0 ? FString::Printf(TEXT("%d:%02d:%06.3f"), Hours, Minutes, Rest)
	                 : FString::Printf(TEXT("%d:%06.3f"), Minutes, Rest);
}

void AUIRaceDirector::BeginPlay()
{
	Super::BeginPlay();

	const TCHAR* CommandLine = FCommandLine::Get();
	int32 Laps = 50;
	int64 Seed = 1;
	FString EntriesFile = TEXT("fictional-grid.json");
	FParse::Value(CommandLine, TEXT("UISimLaps="), Laps);
	FParse::Value(CommandLine, TEXT("UISimSeed="), Seed);
	FParse::Value(CommandLine, TEXT("UISimEntries="), EntriesFile);
	FParse::Value(CommandLine, TEXT("UISimTimeScale="), SimTimeScale);
	bQuitWhenDone = FParse::Param(CommandLine, TEXT("UISimQuitWhenDone"));
	bInputEnabled = !FParse::Param(CommandLine, TEXT("UISimNoInput")); // automated runs ignore the keyboard
	FParse::Value(CommandLine, TEXT("UISimSafetyCarAt="), SafetyCarAtSeconds);           // scripted check
	FParse::Value(CommandLine, TEXT("UISimScreenshotAt="), ScreenshotAtSeconds);         // saves the game viewport
	FParse::Value(CommandLine, TEXT("UISimReviewAt="), ReviewAtSeconds);                 // scripted incident review
	bTestVerdicts = FParse::Param(CommandLine, TEXT("UISimTestVerdicts"));
	FParse::Value(CommandLine, TEXT("UISimCrashAt="), CrashAtSeconds);
	bCrashReact = FParse::Param(CommandLine, TEXT("UISimCrashReact"));

	const FString DataDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../data")));
	FString TrackFile = TEXT("testring.json");
	FParse::Value(CommandLine, TEXT("UISimTrack="), TrackFile); // file in data/tracks, e.g. valle-alta.json
	const FString TrackPath = FPaths::Combine(DataDir, TEXT("tracks"), TrackFile);
	const FString EntriesPath = FPaths::Combine(DataDir, TEXT("entries"), EntriesFile);
	const FString RulesPath = FPaths::Combine(DataDir, TEXT("rules/fia-f1-2026.json"));

	FString Error;
	if (!SimSession.Start(TrackPath, EntriesPath, RulesPath, Laps, Seed, Error))
	{
		UE_LOG(LogUISim, Error, TEXT("Could not start race: %s"), *Error);
		return;
	}
	UE_LOG(LogUISim, Display, TEXT("Race started: %s, %d laps, seed %lld, %d cars"),
		*SimSession.TrackName(), Laps, Seed, SimSession.Entries().Num());

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	// Interpolation state must exist before the first car placement.
	const int32 CarCount = SimSession.Entries().Num();
	PreviousDistance.SetNumZeroed(CarCount);
	PreviousLateral.SetNumZeroed(CarCount);
	for (const FUISimCar& Car : SimSession.Snapshot().Cars)
	{
		PreviousDistance[Car.EntryIndex] = Car.Distance;
		PreviousLateral[Car.EntryIndex] = Car.Lateral;
	}

	BuildTrack();
	SpawnFlagPosts();
	SpawnCars();
	SpawnCameras();
	SpawnReviewView();
	bReady = true;
}

UStaticMeshComponent* AUIRaceDirector::AddMesh(UStaticMesh* Mesh, const FLinearColor& Color)
{
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	Material->SetVectorParameterValue(TEXT("Color"), Color);
	Component->SetMaterial(0, Material);
	Component->SetupAttachment(GetRootComponent());
	Component->RegisterComponent();
	return Component;
}

void AUIRaceDirector::BuildTrack()
{
	const TArray<FVector> Line = SimSession.CenterlineWorld();
	const int32 Count = Line.Num();

	UMaterialInstanceDynamic* Asphalt = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	Asphalt->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.05f, 0.055f));

	for (int32 Start = 0; Start < Count; Start += SamplesPerRoadPiece)
	{
		const int32 End = FMath::Min(Start + SamplesPerRoadPiece, Count); // last piece closes the loop
		const FVector A = Line[Start];
		const FVector B = Line[End % Count];
		const FVector TangentA = (Line[(Start + 1) % Count] - Line[(Start - 1 + Count) % Count]) * 0.5 * (End - Start);
		const FVector TangentB = (Line[(End + 1) % Count] - Line[(End - 1 + Count) % Count]) * 0.5 * (End - Start);

		USplineMeshComponent* Piece = NewObject<USplineMeshComponent>(this);
		Piece->SetMobility(EComponentMobility::Movable);
		Piece->SetStaticMesh(PlaneMesh);
		Piece->SetMaterial(0, Asphalt);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetStartAndEnd(A + FVector(0, 0, RoadHeight), TangentA, B + FVector(0, 0, RoadHeight), TangentB, false);
		Piece->SetStartScale(FVector2D(RoadWidthMetres, 1.0), false);
		Piece->SetEndScale(FVector2D(RoadWidthMetres, 1.0), false);
		Piece->SetupAttachment(GetRootComponent());
		Piece->RegisterComponent();
		Piece->UpdateMesh();
	}

	// Start/finish line.
	const FTransform LineTransform = SimSession.TrackTransform(0.0, 0.0);
	UStaticMeshComponent* FinishLine = AddMesh(CubeMesh, FLinearColor::White);
	FinishLine->SetWorldTransform(FTransform(LineTransform.GetRotation(),
		LineTransform.GetLocation() + FVector(0, 0, RoadHeight), FVector(0.5, RoadWidthMetres, 0.02)));

	// Pit lane: 20 m right of the racing line, 300 m either side of the line (core Sim.cpp kPit*).
	for (double D = -300.0; D < 300.0; D += 20.0)
	{
		const FTransform At = SimSession.TrackTransform(D + 10.0, -20.0);
		UStaticMeshComponent* Lane = AddMesh(CubeMesh, FLinearColor(0.25f, 0.25f, 0.27f));
		Lane->SetWorldTransform(FTransform(At.GetRotation(), At.GetLocation() + FVector(0, 0, RoadHeight),
			FVector(0.2, 0.08, 0.01)));
	}

	// Ground under the whole circuit.
	const FBox Bounds(Line);
	UStaticMeshComponent* Ground = AddMesh(PlaneMesh, FLinearColor(0.08f, 0.2f, 0.06f));
	const double GroundSize = Bounds.GetSize().GetMax() / 100.0 * 1.5; // plane is 1 m
	Ground->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, 0.0),
		FVector(GroundSize, GroundSize, 1.0)));
}

void AUIRaceDirector::SpawnCars()
{
	const TArray<FUISimEntry>& Entries = SimSession.Entries();
	TArray<FString> Teams;
	for (const FUISimEntry& Entry : Entries)
	{
		Teams.AddUnique(Entry.Team);
	}
	for (const FUISimEntry& Entry : Entries)
	{
		UStaticMeshComponent* Car = AddMesh(CubeMesh, TeamColor(Teams.IndexOfByKey(Entry.Team), Teams.Num()));
		Car->SetWorldScale3D(CarScale);
		CarMeshes.Add(Car);
	}
	UpdateCars(1.0);

	// One garage per team behind its pit box (M7.8).
	TArray<FString> BoxTeams;
	for (int32 Entry = 0; Entry < SimSession.Entries().Num(); ++Entry)
	{
		const FString& Team = SimSession.Entries()[Entry].Team;
		if (BoxTeams.Contains(Team))
		{
			continue;
		}
		BoxTeams.Add(Team);
		const FTransform At = SimSession.TrackTransform(SimSession.BoxOffset(Entry), -28.0);
		UStaticMeshComponent* Garage = AddMesh(CubeMesh, TeamColor(Teams.IndexOfByKey(Team), Teams.Num()));
		Garage->SetWorldTransform(FTransform(At.GetRotation(), At.GetLocation() + FVector(0, 0, 200.0), FVector(0.1, 0.06, 0.04)));
	}
	SafetyCarMesh = AddMesh(CubeMesh, FLinearColor(1.0f, 0.45f, 0.0f));
	SafetyCarMesh->SetWorldScale3D(CarScale * FVector(1.1, 1.05, 1.3));
	SafetyCarMesh->SetVisibility(false);
}

void AUIRaceDirector::SpawnFlagPosts()
{
	const int32 Count = SimSession.MarshalSectorCount();
	const double Length = SimSession.TrackLength();
	for (int32 Sector = 0; Sector < Count; ++Sector)
	{
		// Post on the right-hand side at the start of each marshal sector.
		const FTransform At = SimSession.TrackTransform(Sector * Length / Count, -(RoadWidthMetres / 2.0 + 3.0));
		UStaticMeshComponent* Post = AddMesh(CubeMesh, FLinearColor::Gray);
		Post->SetWorldTransform(FTransform(At.GetRotation(), At.GetLocation() + FVector(0, 0, 150.0), FVector(0.6, 0.6, 3.0)));
		FlagPosts.Add(Post);
		FlagPostMaterials.Add(Cast<UMaterialInstanceDynamic>(Post->GetMaterial(0)));
	}
}

void AUIRaceDirector::UpdateFlagPosts()
{
	const TArray<EUISimFlag>& Flags = SimSession.Snapshot().SectorFlags;
	for (int32 Sector = 0; Sector < FlagPosts.Num() && Sector < Flags.Num(); ++Sector)
	{
		FLinearColor Color(0.25f, 0.25f, 0.25f);
		if (Flags[Sector] == EUISimFlag::Yellow)
		{
			Color = FLinearColor(1.0f, 0.85f, 0.0f);
		}
		else if (Flags[Sector] == EUISimFlag::DoubleYellow)
		{
			Color = FLinearColor(1.0f, 0.45f, 0.0f);
		}
		FlagPostMaterials[Sector]->SetVectorParameterValue(TEXT("Color"), Color);
		// The selected sector's post is taller so the operator can find it.
		const bool bSelected = Sector == SelectedMarshalSector;
		FlagPosts[Sector]->SetWorldScale3D(FVector(0.6, 0.6, bSelected ? 6.0 : 3.0));
	}
}

void AUIRaceDirector::UpdateAccidentSites()
{
	const TArray<FUISimHazard>& Sites = SimSession.Snapshot().Hazards;
	int32 Debris = 0;
	int32 Recovery = 0;
	// Debris pieces scattered around a crash site on the track (fixed pattern: along, sideways, in metres).
	const FVector2D Pattern[] = {{-6.0, 2.5}, {4.0, -2.0}, {9.0, 1.0}};
	for (const FUISimHazard& Site : Sites)
	{
		if (Site.bOnTrack)
		{
			for (const FVector2D& Offset : Pattern)
			{
				if (!DebrisMeshes.IsValidIndex(Debris))
				{
					UStaticMeshComponent* Piece = AddMesh(CubeMesh, FLinearColor(0.02f, 0.02f, 0.02f));
					Piece->SetWorldScale3D(FVector(0.6, 0.4, 0.15));
					DebrisMeshes.Add(Piece);
				}
				const FTransform At = SimSession.TrackTransform(Site.TrackPosition + Offset.X, Offset.Y);
				DebrisMeshes[Debris]->SetWorldLocationAndRotation(At.GetLocation() + FVector(0, 0, RoadHeight + 8.0),
					At.GetRotation() * FQuat(FVector::UpVector, Offset.X));
				DebrisMeshes[Debris++]->SetVisibility(true);
			}
		}
		if (Site.bRecovering)
		{
			if (!RecoveryMeshes.IsValidIndex(Recovery))
			{
				UStaticMeshComponent* Truck = AddMesh(CubeMesh, FLinearColor(1.0f, 0.75f, 0.0f));
				Truck->SetWorldScale3D(FVector(7.0, 2.5, 3.0)); // recovery truck with crane (blockout)
				RecoveryMeshes.Add(Truck);
			}
			const FTransform At = SimSession.TrackTransform(Site.TrackPosition, -(RoadWidthMetres / 2.0 + 6.0));
			RecoveryMeshes[Recovery]->SetWorldLocationAndRotation(At.GetLocation() + FVector(0, 0, 150.0), At.GetRotation());
			RecoveryMeshes[Recovery++]->SetVisibility(true);
		}
	}
	for (int32 Index = Debris; Index < DebrisMeshes.Num(); ++Index)
	{
		DebrisMeshes[Index]->SetVisibility(false);
	}
	for (int32 Index = Recovery; Index < RecoveryMeshes.Num(); ++Index)
	{
		RecoveryMeshes[Index]->SetVisibility(false);
	}
}

void AUIRaceDirector::UpdateSafetyCar(double Alpha)
{
	const FUISimSnapshot& Snapshot = SimSession.Snapshot();
	SafetyCarMesh->SetVisibility(Snapshot.bSafetyCarOnTrack);
	if (!Snapshot.bSafetyCarOnTrack)
	{
		return;
	}
	const double Distance = FMath::Lerp(PreviousSafetyCarDistance, Snapshot.SafetyCarDistance, Alpha);
	const FTransform Transform = SimSession.CarTransform(Distance, 0.0);
	SafetyCarMesh->SetWorldLocationAndRotation(Transform.GetLocation() + FVector(0, 0, RoadHeight + CarScale.Z * 65.0),
		Transform.GetRotation());
}

void AUIRaceDirector::SpawnCameras()
{
	struct FFixedCamera { const TCHAR* Name; double Distance; double Lateral; double Height; };
	// Trackside positions (distance along the lap, metres beside the track, height).
	const FFixedCamera Fixed[] = {
		{TEXT("T1"), 950.0, -45.0, 10.0},
		{TEXT("Gegengerade"), 2650.0, 40.0, 8.0},
		{TEXT("T10"), 5050.0, -40.0, 10.0},
	};

	auto Spawn = [this](const FString& Name, const FVector& Location, const FRotator& Rotation)
	{
		ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(Location, Rotation);
		Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
		Cameras.Add(Camera);
		CameraNames.Add(Name);
	};

	Spawn(TEXT("TV Leader"), FVector::ZeroVector, FRotator::ZeroRotator); // positioned every frame
	for (const FFixedCamera& Camera : Fixed)
	{
		const FVector Location = SimSession.TrackTransform(Camera.Distance, Camera.Lateral).GetLocation()
			+ FVector(0, 0, Camera.Height * 100.0);
		Spawn(Camera.Name, Location, FRotator::ZeroRotator);
	}
	const FBox Bounds(SimSession.CenterlineWorld());
	Spawn(TEXT("Uebersicht"), Bounds.GetCenter() + FVector(0, 0, Bounds.GetSize().GetMax() * 0.95), FRotator(-90.0, 0.0, 0.0));
	Cameras.Last()->GetCameraComponent()->SetFieldOfView(60.0f);
}

FVector AUIRaceDirector::CarLocation(int32 EntryIndex) const
{
	return CarMeshes[EntryIndex]->GetComponentLocation();
}

void AUIRaceDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bReady)
	{
		return;
	}

	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (Player && !TimingTower)
	{
		TimingTower = CreateWidget<UUITimingTowerWidget>(Player, UUITimingTowerWidget::StaticClass());
		TimingTower->SetRaceDirector(this);
		TimingTower->AddToViewport();
		IncidentPanel = CreateWidget<UUIIncidentWidget>(Player, UUIIncidentWidget::StaticClass());
		IncidentPanel->SetRaceDirector(this);
		IncidentPanel->AddToViewport();
		RatingPanel = CreateWidget<UUIRatingWidget>(Player, UUIRatingWidget::StaticClass());
		RatingPanel->SetRaceDirector(this);
		RatingPanel->AddToViewport();
		RadioPanel = CreateWidget<UUIRadioWidget>(Player, UUIRadioWidget::StaticClass());
		RadioPanel->SetRaceDirector(this);
		RadioPanel->AddToViewport();

		// Free mouse: visible cursor, never locked to the window.
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		Player->SetInputMode(InputMode);
		Player->SetShowMouseCursor(true);
	}
	HandleInput();
	if (SafetyCarAtSeconds > 0.0 && SimSession.Snapshot().Time >= SafetyCarAtSeconds)
	{
		SafetyCarAtSeconds = 0.0;
		SimSession.DeploySafetyCar();
		UE_LOG(LogUISim, Display, TEXT("Race control at %s: Safety Car deployed (scripted)"),
			*FormatRaceTime(SimSession.Snapshot().Time));
	}

	if (ReviewAtSeconds > 0.0 && SimSession.Snapshot().Time >= ReviewAtSeconds)
	{
		ReviewAtSeconds = 0.0;
		const int32 Latest = SimSession.Incidents().Num() - 1;
		if (Latest >= 0)
		{
			StartReview(Latest);
			bReviewPaused = true; // freeze at the moment of the incident for the check
			ReviewTime = SimSession.Incidents()[Latest].Time;
		}
		UE_LOG(LogUISim, Display, TEXT("Scripted review: %d incidents so far"), Latest + 1);
	}

	if (CrashAtSeconds > 0.0 && SimSession.Snapshot().Time >= CrashAtSeconds && SimSession.Snapshot().Cars.Num() > 8)
	{
		CrashAtSeconds = 0.0;
		SimSession.InjectCrash(SimSession.Snapshot().Cars[8].EntryIndex);
		UE_LOG(LogUISim, Display, TEXT("Scripted crash at %s"), *FormatRaceTime(SimSession.Snapshot().Time));
		bCrashCameraPending = true;
		CrashReactAt = bCrashReact ? SimSession.Snapshot().Time + 10.0 : 0.0;
	}
	if (bCrashCameraPending && SimSession.Snapshot().Hazards.Num() > 0)
	{
		// Put the first trackside camera at the marshal post nearest to the scripted crash.
		bCrashCameraPending = false;
		const FUISimHazard& Site = SimSession.Snapshot().Hazards.Last();
		const double SectorLength = SimSession.TrackLength() / SimSession.MarshalSectorCount();
		const double Post = FMath::RoundToDouble(Site.TrackPosition / SectorLength) * SectorLength;
		Cameras[1]->SetActorLocation(SimSession.TrackTransform(Post, -(RoadWidthMetres / 2.0 + 3.0)).GetLocation() + FVector(0, 0, 600.0));
		ActiveCamera = 1;
	}
	if (CrashReactAt > 0.0 && SimSession.Snapshot().Time >= CrashReactAt && SimSession.Snapshot().Hazards.Num() > 0)
	{
		CrashReactAt = 0.0;
		const double SectorLength = SimSession.TrackLength() / SimSession.MarshalSectorCount();
		const int32 Sector = FMath::Clamp(static_cast<int32>(SimSession.Snapshot().Hazards.Last().TrackPosition / SectorLength), 0,
			SimSession.MarshalSectorCount() - 1);
		SimSession.SetSectorFlag(Sector, EUISimFlag::DoubleYellow);
		UE_LOG(LogUISim, Display, TEXT("Scripted reaction: double yellow in sector %d"), Sector + 1);
	}

	while (bTestVerdicts && IncidentsHandledByTest < SimSession.Incidents().Num())
	{
		const FUISimIncident& Incident = SimSession.Incidents()[IncidentsHandledByTest++];
		if (Incident.Type == EUISimIncidentType::Accident)
		{
			continue; // no one to penalise
		}
		SimSession.IssueVerdict(Incident.Id, Incident.Car, EUISimPenalty::FiveSeconds);
	}

	// The race clock stops at the flag, so a screenshot time beyond the finish fires at the end.
	if (ScreenshotAtSeconds > 0.0 && (SimSession.Snapshot().Time >= ScreenshotAtSeconds || SimSession.Snapshot().bFinished))
	{
		ScreenshotAtSeconds = 0.0;
		const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/uisim_check.png"));
		FScreenshotRequest::RequestScreenshot(File, true, false); // with UI
		UE_LOG(LogUISim, Display, TEXT("Screenshot requested: %s"), *File);
	}

	// Fixed-step simulation, independent of the frame rate.
	const double Dt = FUISimSession::TickSeconds();
	Accumulator += DeltaSeconds * SimTimeScale;
	int32 Steps = 0;
	while (Accumulator >= Dt && !SimSession.Snapshot().bFinished && Steps < 5000)
	{
		for (const FUISimCar& Car : SimSession.Snapshot().Cars)
		{
			PreviousDistance[Car.EntryIndex] = Car.Distance;
			PreviousLateral[Car.EntryIndex] = Car.Lateral;
		}
		PreviousSafetyCarDistance = SimSession.Snapshot().SafetyCarDistance;
		bSafetyCarWasOnTrack = SimSession.Snapshot().bSafetyCarOnTrack;
		SimSession.Step();
		if (!bSafetyCarWasOnTrack)
		{
			PreviousSafetyCarDistance = SimSession.Snapshot().SafetyCarDistance; // just deployed: no lerp from 0
		}
		Accumulator -= Dt;
		++Steps;
	}
	if (SimSession.Snapshot().bFinished)
	{
		Accumulator = Dt;
	}
	const double Alpha = FMath::Clamp(Accumulator / Dt, 0.0, 1.0);
	UpdateCars(Alpha);
	UpdateSafetyCar(Alpha);
	UpdateAccidentSites();
	UpdateFlagPosts();
	UpdateCameras();
	if (bReviewing)
	{
		UpdateReview(DeltaSeconds); // replay positions override the live ones
	}

	AActor* ViewTarget = bReviewing ? static_cast<AActor*>(ReviewCamera) : static_cast<AActor*>(Cameras[ActiveCamera]);
	if (Player && Player->GetViewTarget() != ViewTarget)
	{
		Player->SetViewTarget(ViewTarget);
	}

	if (SimSession.Snapshot().bFinished && !bResultLogged)
	{
		bResultLogged = true;
		LogResult();
		if (bQuitWhenDone)
		{
			FPlatformMisc::RequestExit(false, TEXT("UISimQuitWhenDone"));
		}
	}
}

void AUIRaceDirector::UpdateCars(double Alpha)
{
	for (const FUISimCar& Car : SimSession.Snapshot().Cars)
	{
		UStaticMeshComponent* Mesh = CarMeshes[Car.EntryIndex];
		// Finished cars leave the track (parc fermé); retired cars stay at their accident site
		// until the marshals have recovered them.
		const FUISimHazard* Site = SimSession.Snapshot().Hazards.FindByPredicate(
			[&Car](const FUISimHazard& H) { return H.Car == Car.EntryIndex; });
		Mesh->SetVisibility(!Car.bFinished && (!Car.bRetired || Site != nullptr));
		const double Distance = FMath::Lerp(PreviousDistance[Car.EntryIndex], Car.Distance, Alpha);
		const double Lateral = FMath::Lerp(PreviousLateral[Car.EntryIndex], Car.Lateral, Alpha);
		const FTransform Transform = SimSession.CarTransform(Distance, Lateral);
		FQuat Rotation = Transform.GetRotation();
		if (Site)
		{
			Rotation = Rotation * FQuat(FVector::UpVector, FMath::DegreesToRadians(Site->bOnTrack ? 70.0 : 30.0));
		}
		Mesh->SetWorldLocationAndRotation(Transform.GetLocation() + FVector(0, 0, RoadHeight + CarScale.Z * 50.0), Rotation);
	}
}

void AUIRaceDirector::UpdateCameras()
{
	// Leader still on track (finished cars are hidden).
	const FUISimCar* Leader = nullptr;
	for (const FUISimCar& Car : SimSession.Snapshot().Cars)
	{
		if (!Car.bFinished)
		{
			Leader = &Car;
			break;
		}
	}
	if (!Leader)
	{
		return;
	}

	// TV camera: behind and above the leader.
	{
		const FTransform Car = SimSession.CarTransform(Leader->Distance, Leader->Lateral);
		const FVector Target = CarLocation(Leader->EntryIndex);
		const FVector Location = Target - Car.GetRotation().GetForwardVector() * 2500.0 + FVector(0, 0, 800.0);
		Cameras[0]->SetActorLocationAndRotation(Location, (Target - Location).Rotation());
	}

	// Trackside cameras: follow the nearest car, zoom so it stays roughly the same size.
	for (int32 Index = 1; Index <= 3; ++Index)
	{
		ACameraActor* Camera = Cameras[Index];
		const FVector From = Camera->GetActorLocation();
		double Best = TNumericLimits<double>::Max();
		FVector Target = From + FVector(1, 0, 0);
		for (const FUISimCar& Car : SimSession.Snapshot().Cars)
		{
			if (Car.bFinished)
			{
				continue;
			}
			const FVector Location = CarLocation(Car.EntryIndex);
			const double Distance = FVector::Dist(From, Location);
			if (Distance < Best)
			{
				Best = Distance;
				Target = Location;
			}
		}
		Camera->SetActorRotation((Target - From).Rotation());
		const double Fov = FMath::RadiansToDegrees(2.0 * FMath::Atan(1500.0 / FMath::Max(Best, 1.0)));
		Camera->GetCameraComponent()->SetFieldOfView(FMath::Clamp(Fov, 5.0, 70.0));
	}
}

void AUIRaceDirector::HandleInput()
{
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (!Player || !bInputEnabled)
	{
		return;
	}
	const FKey CameraKeys[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five};
	for (int32 Index = 0; !bReviewing && Index < UE_ARRAY_COUNT(CameraKeys) && Index < Cameras.Num(); ++Index)
	{
		if (Player->WasInputKeyJustPressed(CameraKeys[Index]))
		{
			ActiveCamera = Index;
		}
	}

	int32 Current = 0;
	while (Current + 1 < UE_ARRAY_COUNT(TimeScales) && TimeScales[Current] < SimTimeScale)
	{
		++Current;
	}
	if (Player->WasInputKeyJustPressed(EKeys::Add) || Player->WasInputKeyJustPressed(EKeys::Equals))
	{
		SimTimeScale = TimeScales[FMath::Min(Current + 1, static_cast<int32>(UE_ARRAY_COUNT(TimeScales)) - 1)];
	}
	if (Player->WasInputKeyJustPressed(EKeys::Subtract) || Player->WasInputKeyJustPressed(EKeys::Hyphen))
	{
		SimTimeScale = TimeScales[FMath::Max(Current - 1, 0)];
	}
	HandleRaceControlInput(Player);
	HandleReviewInput(Player);
}

int32 AUIRaceDirector::SelectedIncident() const
{
	const int32 Count = SimSession.Incidents().Num();
	if (Count == 0)
	{
		return -1;
	}
	return SelectedIncidentIndex < 0 ? Count - 1 : FMath::Min(SelectedIncidentIndex, Count - 1);
}

double AUIRaceDirector::ReviewOffset() const
{
	return bReviewing ? ReviewTime - SimSession.Incidents()[ReviewIncidentIndex].Time : 0.0;
}

void AUIRaceDirector::SpawnReviewView()
{
	ReviewCamera = GetWorld()->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	ReviewCamera->GetCameraComponent()->SetConstraintAspectRatio(false);

	// Second camera as picture-in-picture: scene capture into a render target, shown by the incident panel.
	PipTarget = NewObject<UTextureRenderTarget2D>(this);
	PipTarget->InitAutoFormat(640, 360);
	PipTarget->UpdateResourceImmediate(true);
	PipCapture = NewObject<USceneCaptureComponent2D>(this);
	PipCapture->TextureTarget = PipTarget;
	PipCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	PipCapture->bCaptureEveryFrame = false; // only while reviewing
	PipCapture->SetupAttachment(GetRootComponent());
	PipCapture->RegisterComponent();
}

void AUIRaceDirector::HandleReviewInput(APlayerController* Player)
{
	const int32 Count = SimSession.Incidents().Num();
	if (Count == 0)
	{
		return;
	}
	const int32 Selected = SelectedIncident();
	if (Player->WasInputKeyJustPressed(EKeys::Up))
	{
		SelectedIncidentIndex = FMath::Max(Selected - 1, 0);
	}
	if (Player->WasInputKeyJustPressed(EKeys::Down))
	{
		SelectedIncidentIndex = Selected + 1 >= Count ? -1 : Selected + 1; // past the newest: follow the latest
	}
	if (Player->WasInputKeyJustPressed(EKeys::Enter))
	{
		StartReview(SelectedIncident());
	}
	if (!bReviewing)
	{
		return;
	}
	if (Player->WasInputKeyJustPressed(EKeys::BackSpace))
	{
		StopReview();
		return;
	}
	if (Player->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		bReviewPaused = !bReviewPaused;
	}
	const double IncidentTime = SimSession.Incidents()[ReviewIncidentIndex].Time;
	if (Player->WasInputKeyJustPressed(EKeys::Left))
	{
		ReviewTime = FMath::Max(ReviewTime - 1.0, IncidentTime - ReviewWindow);
	}
	if (Player->WasInputKeyJustPressed(EKeys::Right))
	{
		ReviewTime = FMath::Min(ReviewTime + 1.0, IncidentTime + ReviewWindow);
	}
	if (Player->WasInputKeyJustPressed(EKeys::C))
	{
		ReviewMainMode = (ReviewMainMode + 1) % 4;
	}

	// Verdict on the incident under review (one per incident).
	const FUISimIncident& Incident = SimSession.Incidents()[ReviewIncidentIndex];
	if (Player->WasInputKeyJustPressed(EKeys::Tab))
	{
		Accused = 1 - Accused;
	}
	const FKey PenaltyKeys[] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(PenaltyKeys); ++Index)
	{
		if (Player->WasInputKeyJustPressed(PenaltyKeys[Index]) && !SimSession.VerdictFor(Incident.Id))
		{
			const EUISimPenalty Penalty = static_cast<EUISimPenalty>(Index);
			const int32 Car = Penalty == EUISimPenalty::None ? -1 : (Accused == 0 ? Incident.Car : Incident.OtherCar);
			SimSession.IssueVerdict(Incident.Id, Car, Penalty);
			UE_LOG(LogUISim, Display, TEXT("Verdict #%d: %s %s"), Incident.Id, *PenaltyName(Penalty),
				Car >= 0 ? *SimSession.Entries()[Car].Code : TEXT(""));
		}
	}
}

FString AUIRaceDirector::PenaltyName(EUISimPenalty Penalty)
{
	switch (Penalty)
	{
	case EUISimPenalty::None: return TEXT("keine Aktion");
	case EUISimPenalty::Warning: return TEXT("Verwarnung");
	case EUISimPenalty::FiveSeconds: return TEXT("5 s");
	case EUISimPenalty::TenSeconds: return TEXT("10 s");
	case EUISimPenalty::DriveThrough: return TEXT("Drive-Through");
	case EUISimPenalty::StopAndGo: return TEXT("Stop-and-Go");
	case EUISimPenalty::Disqualification: return TEXT("Disqualifikation");
	}
	return FString();
}

void AUIRaceDirector::StartReview(int32 IncidentIndex)
{
	if (!SimSession.Incidents().IsValidIndex(IncidentIndex))
	{
		return;
	}
	bReviewing = true;
	bReviewPaused = false;
	ReviewIncidentIndex = IncidentIndex;
	ReviewTime = SimSession.Incidents()[IncidentIndex].Time - ReviewWindow;
	PipCapture->bCaptureEveryFrame = true;
	UE_LOG(LogUISim, Display, TEXT("Review of incident #%d"), SimSession.Incidents()[IncidentIndex].Id);
}

void AUIRaceDirector::StopReview()
{
	bReviewing = false;
	PipCapture->bCaptureEveryFrame = false;
	SafetyCarMesh->SetVisibility(SimSession.Snapshot().bSafetyCarOnTrack);
}

void AUIRaceDirector::UpdateReview(float DeltaSeconds)
{
	const FUISimIncident& Incident = SimSession.Incidents()[ReviewIncidentIndex];
	if (!bReviewPaused)
	{
		ReviewTime += DeltaSeconds; // real time, independent of the live time scale
		if (ReviewTime > Incident.Time + ReviewWindow)
		{
			ReviewTime = Incident.Time - ReviewWindow; // loop
		}
	}

	double SafetyCarDistance = 0.0;
	bool bSafetyCar = false;
	if (!SimSession.ReplayAt(ReviewTime, ReplayCars, SafetyCarDistance, bSafetyCar))
	{
		return;
	}
	for (int32 Entry = 0; Entry < ReplayCars.Num(); ++Entry)
	{
		const FTransform Transform = SimSession.CarTransform(ReplayCars[Entry].Distance, ReplayCars[Entry].Lateral);
		CarMeshes[Entry]->SetVisibility(true);
		CarMeshes[Entry]->SetWorldLocationAndRotation(
			Transform.GetLocation() + FVector(0, 0, RoadHeight + CarScale.Z * 50.0), Transform.GetRotation());
	}
	SafetyCarMesh->SetVisibility(bSafetyCar);
	if (bSafetyCar)
	{
		const FTransform Transform = SimSession.CarTransform(SafetyCarDistance, 0.0);
		SafetyCarMesh->SetWorldLocationAndRotation(
			Transform.GetLocation() + FVector(0, 0, RoadHeight + CarScale.Z * 65.0), Transform.GetRotation());
	}

	double Fov = 60.0;
	const FTransform Main = ReviewViewTransform(ReviewMainMode, Fov);
	ReviewCamera->SetActorTransform(Main);
	ReviewCamera->GetCameraComponent()->SetFieldOfView(Fov);
	const FTransform Pip = ReviewViewTransform((ReviewMainMode + 1) % 4, Fov);
	PipCapture->SetWorldLocationAndRotation(Pip.GetLocation(), Pip.GetRotation());
	PipCapture->FOVAngle = Fov;
}

FTransform AUIRaceDirector::ReviewViewTransform(int32 Mode, double& OutFov) const
{
	const FUISimIncident& Incident = SimSession.Incidents()[ReviewIncidentIndex];
	const FVector A = CarLocation(Incident.Car);
	const FVector B = CarLocation(Incident.OtherCar);
	const FVector Middle = (A + B) * 0.5;
	OutFov = 60.0;

	switch (Mode)
	{
	case 0: // camera at the nearest marshal post
	{
		const int32 Sectors = SimSession.MarshalSectorCount();
		const double Length = SimSession.TrackLength();
		const double SectorLength = Length / Sectors;
		const double Along = FMath::Fmod(FMath::Fmod(Incident.Distance, Length) + Length, Length);
		const double PostDistance = FMath::RoundToDouble(Along / SectorLength) * SectorLength;
		const FVector Post = SimSession.TrackTransform(PostDistance, -(RoadWidthMetres / 2.0 + 3.0)).GetLocation()
			+ FVector(0, 0, 600.0);
		const double Range = FVector::Dist(Post, Middle);
		OutFov = FMath::Clamp(FMath::RadiansToDegrees(2.0 * FMath::Atan(1500.0 / FMath::Max(Range, 1.0))), 5.0, 70.0);
		return FTransform((Middle - Post).Rotation(), Post);
	}
	case 1: // helicopter above the incident
	{
		const FVector Forward = SimSession.TrackTransform(Incident.Distance, 0.0).GetRotation().GetForwardVector();
		const FVector Heli = Middle - Forward * 3000.0 + FVector(0, 0, 4500.0);
		OutFov = 45.0;
		return FTransform((Middle - Heli).Rotation(), Heli);
	}
	default: // onboard behind one of the two cars
	{
		const int32 Entry = Mode == 2 ? Incident.Car : Incident.OtherCar;
		const FUISimReplayCar& Car = ReplayCars.IsValidIndex(Entry) ? ReplayCars[Entry] : FUISimReplayCar{};
		const FTransform Pose = SimSession.CarTransform(Car.Distance, Car.Lateral);
		const FVector Forward = Pose.GetRotation().GetForwardVector();
		const FVector Eye = CarLocation(Entry) - Forward * 900.0 + FVector(0, 0, 300.0);
		OutFov = 75.0;
		return FTransform((CarLocation(Entry) + Forward * 2000.0 - Eye).Rotation(), Eye);
	}
	}
}

FString AUIRaceDirector::ReviewCameraName(bool bPictureInPicture) const
{
	if (!bReviewing)
	{
		return FString();
	}
	const int32 Mode = bPictureInPicture ? (ReviewMainMode + 1) % 4 : ReviewMainMode;
	const FUISimIncident& Incident = SimSession.Incidents()[ReviewIncidentIndex];
	switch (Mode)
	{
	case 0: return TEXT("Streckenposten");
	case 1: return TEXT("Heli");
	case 2: return FString::Printf(TEXT("Onboard %s"), *SimSession.Entries()[Incident.Car].Code);
	default: return FString::Printf(TEXT("Onboard %s"), *SimSession.Entries()[Incident.OtherCar].Code);
	}
}

void AUIRaceDirector::HandleRaceControlInput(APlayerController* Player)
{
	const FUISimSnapshot& Snapshot = SimSession.Snapshot();
	const EUISimRaceState State = Snapshot.State;
	auto Log = [&Snapshot](const TCHAR* Action)
	{
		UE_LOG(LogUISim, Display, TEXT("Race control at %s: %s"), *FormatRaceTime(Snapshot.Time), Action);
	};

	if (Player->WasInputKeyJustPressed(EKeys::V))
	{
		if (State == EUISimRaceState::Green)
		{
			SimSession.DeployVirtualSafetyCar();
			Log(TEXT("VSC deployed"));
		}
		else if (State == EUISimRaceState::VirtualSafetyCar)
		{
			SimSession.EndVirtualSafetyCar();
			Log(TEXT("VSC ending"));
		}
	}
	if (Player->WasInputKeyJustPressed(EKeys::S))
	{
		if (State == EUISimRaceState::SafetyCar)
		{
			SimSession.RecallSafetyCar();
			Log(TEXT("Safety Car in this lap"));
		}
		else if (State != EUISimRaceState::RedFlag)
		{
			SimSession.DeploySafetyCar();
			Log(TEXT("Safety Car deployed"));
		}
	}
	if (Player->WasInputKeyJustPressed(EKeys::R))
	{
		if (State == EUISimRaceState::RedFlag)
		{
			SimSession.Restart();
			Log(TEXT("Restart behind the Safety Car"));
		}
		else
		{
			SimSession.RedFlag();
			Log(TEXT("Red flag"));
		}
	}

	const int32 Sectors = SimSession.MarshalSectorCount();
	if (Sectors > 0)
	{
		if (Player->WasInputKeyJustPressed(EKeys::Q))
		{
			SelectedMarshalSector = (SelectedMarshalSector + Sectors - 1) % Sectors;
		}
		if (Player->WasInputKeyJustPressed(EKeys::E))
		{
			SelectedMarshalSector = (SelectedMarshalSector + 1) % Sectors;
		}
		if (Player->WasInputKeyJustPressed(EKeys::Y) && Snapshot.SectorFlags.IsValidIndex(SelectedMarshalSector))
		{
			const uint8 Next = (static_cast<uint8>(Snapshot.SectorFlags[SelectedMarshalSector]) + 1) % 3;
			SimSession.SetSectorFlag(SelectedMarshalSector, static_cast<EUISimFlag>(Next));
			const TCHAR* Names[] = {TEXT("green"), TEXT("yellow"), TEXT("double yellow")};
			Log(*FString::Printf(TEXT("sector %d %s"), SelectedMarshalSector + 1, Names[Next]));
		}
	}
}

FString AUIRaceDirector::ActiveCameraName() const
{
	return CameraNames.IsValidIndex(ActiveCamera) ? CameraNames[ActiveCamera] : FString();
}

void AUIRaceDirector::LogResult() const
{
	// Same layout as core/tools/runner so both outputs can be compared line by line.
	UE_LOG(LogUISim, Display, TEXT("RESULT %-4s %-4s %-4s %-18s %-20s %-5s %-5s %-12s %s"),
		TEXT("Pos"), TEXT("No"), TEXT("Code"), TEXT("Driver"), TEXT("Team"), TEXT("Grid"), TEXT("Laps"), TEXT("Time/Gap"), TEXT("Best lap"));
	for (const FUISimCar& Car : SimSession.Snapshot().Cars)
	{
		const FUISimEntry& Entry = SimSession.Entries()[Car.EntryIndex];
		FString Result;
		if (Car.Position == 1)
		{
			Result = FormatRaceTime(Car.LastLineTime);
		}
		else if (Car.bRetired)
		{
			Result = TEXT("DNF");
		}
		else if (Car.LapsDown > 0)
		{
			Result = FString::Printf(TEXT("+%d %s"), Car.LapsDown, Car.LapsDown == 1 ? TEXT("lap") : TEXT("laps"));
		}
		else
		{
			Result = FString::Printf(TEXT("+%.3f"), Car.GapToLeader);
		}
		UE_LOG(LogUISim, Display, TEXT("RESULT %-4d %-4d %-4s %-18s %-20s %-5d %-5d %-12s %s"),
			Car.Position, Entry.Number, *Entry.Code, *Entry.Name, *Entry.Team, Car.GridPosition, Car.LapsCompleted,
			*Result, *FormatRaceTime(Car.BestLapTime));
	}
	UE_LOG(LogUISim, Display, TEXT("RESULT Overtakes: %d"), SimSession.Overtakes());
	UE_LOG(LogUISim, Display, TEXT("Flag violations: %d"), SimSession.FlagViolations());
	const FUISimRating Rating = SimSession.Rating();
	UE_LOG(LogUISim, Display,
		TEXT("Rating: %.0f/100 (fairness %.0f%%, consistency %.0f%%, safety %.0f%%, race quality %.0f%%), %d incidents"),
		Rating.Overall, Rating.Fairness * 100.0, Rating.Consistency * 100.0, Rating.Safety * 100.0,
		Rating.RaceQuality * 100.0, Rating.Incidents.Num());
}
