#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UISimSession.h"
#include "UIRaceDirector.generated.h"

class ACameraActor;
class APlayerController;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UUIDeskWidget;
class UUITimingTowerWidget;
class UUIIncidentWidget;
class UUIRatingWidget;
class UUIRadioWidget;
class UUITrackMapWidget;
class UUIControlPanelWidget;
class UUIWeatherWidget;
class UUISeasonWidget;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class FSlateVirtualUserHandle;

// One player action (M8.4). Keys, buttons and test scripts all go through AUIRaceDirector::Execute().
enum class EUICommand : uint8
{
	SetSectorFlag,         // Index = marshal sector, Flag
	SelectSector,          // Index
	DeployVsc,
	EndVsc,
	DeploySafetyCar,
	RecallSafetyCar,       // "Safety Car in this lap"; Index 2 = standing start after formation laps (B5.10.8)
	RedFlag,
	Restart,               // after a red flag; Index bit 1 = wet tyres compulsory (B5.15.2 a), bit 2 = standing start (B5.15.4)
	LappedCarsMayOvertake, // under the SC (B5.13.4 c)
	ClosePitEntry,         // B1.6.4
	OpenPitEntry,
	SafetyCarStart,        // before the start; Index 1 = wet tyres compulsory (B5.10.1 a)
	SetCamera,             // Index
	FollowCar,             // Index = entry index for the TV camera, -1 = the leader
	SetTimeScale,          // Value
	SelectIncident,        // Index into the incident list, -1 = follow the latest
	StartReview,           // Index, -1 = the selected incident
	StopReview,
	ToggleReviewPause,
	SeekReview,            // Value = seconds, relative to the replay time
	SetReviewOffset,       // Value = seconds relative to the incident (-5..+5)
	NextReviewCamera,
	SetAccused,            // Index: 0 = car, 1 = other car
	Verdict,               // Penalty, against the accused car of the incident under review
	StartSeasonRace,       // season screen: start the next race of the calendar (M10.6)
	BackToSeason,          // after the flag: book the race and return to the season screen
	NewSeason,             // season screen after the last race: a fresh season
	HoldHearing,           // hear the drivers on the incident under review (M11.7)
	AbortStart,            // before the start signal: begin the procedure again (B5.8)
	ExtraFormationLap      // before the start signal: one more formation lap (B5.9)
};

struct FUICommand
{
	EUICommand Type = EUICommand::SelectSector;
	int32 Index = 0;
	double Value = 0.0;
	EUISimFlag Flag = EUISimFlag::Green;
	EUISimPenalty Penalty = EUISimPenalty::None;
};

// Runs the UISim race at a fixed 20 Hz tick and shows it: blockout track and cars,
// interpolated between sim snapshots, plus switchable cameras and the timing tower.
// All race logic stays in the core; this actor only displays it.
//
// Command line: -UISimSeed=N -UISimLaps=N -UISimEntries=<file in data/entries>
//               -UISimTimeScale=X -UISimQuitWhenDone -UISimNoInput -UISimSafetyCarAt=<seconds>
//               -UISimScreenshotAt=<seconds> -UISimReviewAt=<seconds>
// Incidents: Up/Down select, Enter review, Space pause, Left/Right -/+1 s, C camera, Backspace end.
// Verdict while reviewing: Tab accused car, 0 none, 1 warning, 2 5 s, 3 10 s, 4 drive-through, 5 stop-and-go.
//               -UISimTestVerdicts (5 s for every attacker, automated checks only)
//               -UISimCrashAt=<seconds> (scripted crash, automated checks only)
//               -UISimWeather=<file in data/weather> (rain, M9.7)
//               -UISimCompoundMiss=<0..1> (checks only: teams overlook the two-compound rule)
//               -UISimScript=<file> (commands at race times, see UIRaceCommands.cpp; relative to the project)
// Season (M10.6): -UISimSeason=<file in data/seasons> shows the season screen and runs the races of the calendar
//               as a career; this is also the default when none of -UISimSeed, -UISimTrack, -UISimLaps asks for
//               a single race. -UISimCareer=<name> picks the save file Saved/Career/<name>.json (default career),
//               -UISimNewCareer ignores an existing one. Every race gets a race director of its own.
// Keys: 1-6 cameras (6 = pit lane), +/- time scale.
// Race control: V = VSC on/off, S = Safety Car out / in this lap, R = red flag / restart,
//               L = lapped cars may overtake (SC), P = close / open the pit entry,
//               Q/E = select marshal sector, Y = cycle its flag (green, yellow, double yellow).
UCLASS()
class AUIRaceDirector : public AActor
{
	GENERATED_BODY()

public:
	AUIRaceDirector();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	const FUISimSession& Session() const { return SimSession; }
	FString ActiveCameraName() const;
	double TimeScale() const { return SimTimeScale; }
	int32 ActiveCameraIndex() const { return ActiveCamera; }
	// Next (Direction > 0) or previous step of the time scale (1, 2, 5, 10, 20, 50).
	static double StepTimeScale(double Current, int32 Direction);
	int32 SelectedSector() const { return SelectedMarshalSector; }

	// Incident review (M4). Index into Session().Incidents(), -1 = none yet.
	int32 SelectedIncident() const;
	int32 ReviewedIncident() const { return ReviewIncidentIndex; } // valid while reviewing
	bool IsReviewing() const { return bReviewing; }
	bool IsReviewPaused() const { return bReviewPaused; }
	double ReviewOffset() const; // replay time relative to the incident, seconds
	static constexpr double ReviewWindow = 5.0; // replay runs from -5 s to +5 s
	FString ReviewCameraName(bool bPictureInPicture) const;
	// Verdict input while reviewing: which car is accused (0 = car, 1 = other car).
	int32 AccusedSide() const { return Accused; }
	static FString PenaltyName(EUISimPenalty Penalty);
	UTextureRenderTarget2D* PictureInPicture() const { return PipTarget; }

	// "1:23.456" or "1:02:03.456".
	static FString FormatRaceTime(double Seconds);

	// Command layer (M8.4, UIRaceCommands.cpp). Execute() logs every command and refuses
	// (returns false) what is not allowed in the current state.
	bool CanExecute(const FUICommand& Command) const;
	bool Execute(const FUICommand& Command);
	FString CommandName(const FUICommand& Command) const; // script syntax, e.g. "flag 3 yellow"
	int32 FollowedCar() const { return FollowedEntry; } // entry index, -1 = the leader
	bool IsSeasonMode() const { return bSeasonMode; }       // the races belong to a career (M10.6)
	bool IsOnSeasonScreen() const { return bSeasonScreen; } // between the races

private:
	void SetupRace();        // world and state for the race in SimSession
	void TickSeasonScreen();
	void StartSeasonRace();
	void BackToSeason();
	void BuildTrack();
	void SpawnCars();
	void SpawnCameras();
	void UpdateCars(double Alpha);
	void UpdateCameras();
	void HandleInput();
	void HandleRaceControlInput(APlayerController* Player);
	void SpawnFlagPosts();
	void UpdateFlagPosts();
	void UpdateSafetyCar(double Alpha);
	void UpdateAccidentSites();
	void SpawnReviewView();
	void HandleReviewInput(APlayerController* Player);
	void StartReview(int32 IncidentIndex);
	void StopReview();
	void UpdateReview(float DeltaSeconds);
	FTransform ReviewViewTransform(int32 Mode, double& OutFov) const;
	void LogResult() const;
	bool LoadScript(const FString& File);
	void RunScript();
	void SyntheticClick(const FVector2D& ViewportPixel); // Slate event at a viewport pixel, real cursor untouched
	FVector CarLocation(int32 EntryIndex) const;
	FVector PitCameraLocation() const;
	FVector PitLaneCentre() const;
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FLinearColor& Color);

	FUISimSession SimSession;
	TArray<double> PreviousDistance; // by entry index, state before the last sim step
	TArray<double> PreviousLateral;
	double Accumulator = 0.0;
	double SimTimeScale = 1.0;
	bool bReady = false;
	bool bSeasonMode = false;
	bool bSeasonScreen = false;
	double SeasonLineAt = 0.0; // real time from which the next script line may run on the season screen
	UPROPERTY()
	TObjectPtr<UUISeasonWidget> SeasonPanel;
	bool bResultLogged = false;
	bool bQuitWhenDone = false;
	bool bInputEnabled = true;
	double SafetyCarAtSeconds = 0.0; // > 0: deploy the Safety Car at this race time
	double ScreenshotAtSeconds = 0.0; // > 0: save Saved/Screenshots/uisim_check.png at this race time

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> CarMeshes; // by entry index
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SprayMeshes; // by entry index: spray behind a car on a wet track (M10.7)

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SafetyCarMesh;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> DebrisMeshes;   // pooled, 3 per site on the track
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> RecoveryMeshes; // pooled, one per site being recovered
	double PreviousSafetyCarDistance = 0.0;
	bool bSafetyCarWasOnTrack = false;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> FlagPosts; // by marshal sector
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FlagPostMaterials;
	int32 SelectedMarshalSector = 0;

	UPROPERTY()
	TArray<TObjectPtr<ACameraActor>> Cameras;
	TArray<FString> CameraNames;
	int32 ActiveCamera = 0;

	UPROPERTY()
	TObjectPtr<UUIDeskWidget> Desk; // background, cards and top bar behind the panels (M12.1)
	UPROPERTY()
	TObjectPtr<UUITimingTowerWidget> TimingTower;
	UPROPERTY()
	TObjectPtr<UUIIncidentWidget> IncidentPanel;

	// Review state.
	int32 SelectedIncidentIndex = -1; // -1 = follow the latest incident
	bool bReviewing = false;
	bool bReviewPaused = false;
	int32 ReviewIncidentIndex = -1;
	double ReviewTime = 0.0;          // race time shown in the replay
	int32 ReviewMainMode = 0;         // 0 marshal post, 1 heli, 2 onboard car, 3 onboard other car, 4 pit lane
	static constexpr int32 ReviewCameraModes = 5;
	double ReviewAtSeconds = 0.0;     // > 0: scripted review of the latest incident at this race time
	int32 Accused = 0;
	bool bTestVerdicts = false;       // -UISimTestVerdicts: 5 s for the attacker on every incident (checks only)
	int32 IncidentsHandledByTest = 0;
	UPROPERTY()
	TObjectPtr<UUIRatingWidget> RatingPanel;
	UPROPERTY()
	TObjectPtr<UUIRadioWidget> RadioPanel;
	UPROPERTY()
	TObjectPtr<UUITrackMapWidget> TrackMap;
	UPROPERTY()
	TObjectPtr<UUIControlPanelWidget> ControlPanel;
	UPROPERTY()
	TObjectPtr<UUIWeatherWidget> WeatherPanel;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> RoadMaterial; // darker when wet
	int32 FollowedEntry = -1; // TV camera target, -1 = the leader
	FString LoggedUnlapping;  // lapped cars passing under the SC, last logged
	bool bLoggedGridStart = false; // standing start state last logged (checks, M10.7)
	int32 IncidentsLogged = 0;      // incidents named in the log so far (checks, M10.7)
	double CrashAtSeconds = 0.0; // -UISimCrashAt: scripted crash of the car in P9 (checks only)
	bool bCrashReact = false;     // -UISimCrashReact: double yellow 10 s after the scripted crash
	double CrashReactAt = 0.0;
	bool bCrashCameraPending = false;
	TArray<FUISimReplayCar> ReplayCars;

	struct FScriptLine
	{
		double Time = 0.0;
		FUICommand Command;
		bool bClick = false;
		bool bScreenshot = false; // save Saved/Screenshots/<ShotName>.png
		FString ShotName;
		FVector2D Click = FVector2D::ZeroVector;
	};
	TArray<FScriptLine> Script; // -UISimScript, sorted by time
	void RunScriptLine(const FScriptLine& Line);
	int32 ScriptNext = 0;
	double FinishedAt = 0.0; // real time of the finish, for script lines after the flag
	TSharedPtr<FSlateVirtualUserHandle> ClickUser;

	UPROPERTY()
	TObjectPtr<ACameraActor> ReviewCamera;
	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> PipCapture;
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> PipTarget;
};
