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
class UUITimingTowerWidget;
class UUIIncidentWidget;
class UUIRatingWidget;
class UUIRadioWidget;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

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
// Keys: 1-5 cameras, +/- time scale.
// Race control: V = VSC on/off, S = Safety Car out / in this lap, R = red flag / restart,
//               Q/E = select marshal sector, Y = cycle its flag (green, yellow, double yellow).
UCLASS()
class AUIRaceDirector : public AActor
{
	GENERATED_BODY()

public:
	AUIRaceDirector();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	const FUISimSession& Session() const { return SimSession; }
	FString ActiveCameraName() const;
	double TimeScale() const { return SimTimeScale; }
	int32 SelectedSector() const { return SelectedMarshalSector; }

	// Incident review (M4). Index into Session().Incidents(), -1 = none yet.
	int32 SelectedIncident() const;
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

private:
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
	FVector CarLocation(int32 EntryIndex) const;
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FLinearColor& Color);

	FUISimSession SimSession;
	TArray<double> PreviousDistance; // by entry index, state before the last sim step
	TArray<double> PreviousLateral;
	double Accumulator = 0.0;
	double SimTimeScale = 1.0;
	bool bReady = false;
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
	TObjectPtr<UUITimingTowerWidget> TimingTower;
	UPROPERTY()
	TObjectPtr<UUIIncidentWidget> IncidentPanel;

	// Review state.
	int32 SelectedIncidentIndex = -1; // -1 = follow the latest incident
	bool bReviewing = false;
	bool bReviewPaused = false;
	int32 ReviewIncidentIndex = -1;
	double ReviewTime = 0.0;          // race time shown in the replay
	int32 ReviewMainMode = 0;         // 0 marshal post, 1 heli, 2 onboard car, 3 onboard other car
	double ReviewAtSeconds = 0.0;     // > 0: scripted review of the latest incident at this race time
	int32 Accused = 0;
	bool bTestVerdicts = false;       // -UISimTestVerdicts: 5 s for the attacker on every incident (checks only)
	int32 IncidentsHandledByTest = 0;
	UPROPERTY()
	TObjectPtr<UUIRatingWidget> RatingPanel;
	UPROPERTY()
	TObjectPtr<UUIRadioWidget> RadioPanel;
	double CrashAtSeconds = 0.0; // -UISimCrashAt: scripted crash of the car in P9 (checks only)
	bool bCrashReact = false;     // -UISimCrashReact: double yellow 10 s after the scripted crash
	double CrashReactAt = 0.0;
	bool bCrashCameraPending = false;
	TArray<FUISimReplayCar> ReplayCars;

	UPROPERTY()
	TObjectPtr<ACameraActor> ReviewCamera;
	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> PipCapture;
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> PipTarget;
};
