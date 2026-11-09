// Command layer of the race director (M8.4): keys, buttons and test scripts all run their
// actions through Execute(), so every way of operating the game behaves the same and is logged.

#include "UIRaceDirector.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Layout/WidgetPath.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogUISim, Log, All); // same category name as UIRaceDirector.cpp

namespace
{
	const TCHAR* FlagName(EUISimFlag Flag)
	{
		switch (Flag)
		{
		case EUISimFlag::Green: return TEXT("green");
		case EUISimFlag::Yellow: return TEXT("yellow");
		case EUISimFlag::DoubleYellow: return TEXT("double");
		}
		return TEXT("?");
	}

	// Script verdict names, in the order of EUISimPenalty.
	const TCHAR* const PenaltyWords[] = {TEXT("none"), TEXT("warning"), TEXT("5s"), TEXT("10s"), TEXT("dt"), TEXT("sg"), TEXT("dsq")};
}

bool AUIRaceDirector::CanExecute(const FUICommand& Command) const
{
	const FUISimSnapshot& Snapshot = SimSession.Snapshot();
	const EUISimRaceState State = Snapshot.State;
	const int32 IncidentCount = SimSession.Incidents().Num();
	switch (Command.Type)
	{
	case EUICommand::SetSectorFlag:
	case EUICommand::SelectSector:
		return Command.Index >= 0 && Command.Index < SimSession.MarshalSectorCount();
	case EUICommand::DeployVsc: return State == EUISimRaceState::Green;
	case EUICommand::EndVsc: return State == EUISimRaceState::VirtualSafetyCar;
	case EUICommand::DeploySafetyCar: return State == EUISimRaceState::Green || State == EUISimRaceState::VirtualSafetyCar;
	case EUICommand::RecallSafetyCar: return State == EUISimRaceState::SafetyCar && !Snapshot.bSafetyCarInThisLap;
	case EUICommand::RedFlag: return State != EUISimRaceState::RedFlag;
	case EUICommand::Restart: return State == EUISimRaceState::RedFlag;
	case EUICommand::SafetyCarStart: return State == EUISimRaceState::Green && Snapshot.Time <= 0.0;
	case EUICommand::LappedCarsMayOvertake:
		return State == EUISimRaceState::SafetyCar && !Snapshot.bSafetyCarInThisLap && !Snapshot.bLappedCarsReleased;
	case EUICommand::ClosePitEntry: return !Snapshot.bPitEntryClosed;
	case EUICommand::OpenPitEntry: return Snapshot.bPitEntryClosed;
	case EUICommand::SetCamera: return !bReviewing && Cameras.IsValidIndex(Command.Index);
	case EUICommand::FollowCar: return Command.Index == -1 || SimSession.Entries().IsValidIndex(Command.Index);
	case EUICommand::SetTimeScale: return Command.Value > 0.0;
	case EUICommand::SelectIncident: return IncidentCount > 0 && Command.Index >= -1 && Command.Index < IncidentCount;
	case EUICommand::StartReview: return SimSession.Incidents().IsValidIndex(Command.Index < 0 ? SelectedIncident() : Command.Index);
	case EUICommand::StopReview:
	case EUICommand::ToggleReviewPause:
	case EUICommand::SeekReview:
	case EUICommand::SetReviewOffset:
	case EUICommand::NextReviewCamera:
		return bReviewing;
	case EUICommand::SetAccused: return bReviewing && (Command.Index == 0 || Command.Index == 1);
	case EUICommand::Verdict:
		return bReviewing && !SimSession.VerdictFor(SimSession.Incidents()[ReviewIncidentIndex].Id);
	}
	return false;
}

FString AUIRaceDirector::CommandName(const FUICommand& Command) const
{
	switch (Command.Type)
	{
	case EUICommand::SetSectorFlag: return FString::Printf(TEXT("flag %d %s"), Command.Index + 1, FlagName(Command.Flag));
	case EUICommand::SelectSector: return FString::Printf(TEXT("sector %d"), Command.Index + 1);
	case EUICommand::DeployVsc: return TEXT("vsc");
	case EUICommand::EndVsc: return TEXT("vsc_end");
	case EUICommand::DeploySafetyCar: return TEXT("sc");
	case EUICommand::RecallSafetyCar: return TEXT("sc_in");
	case EUICommand::RedFlag: return TEXT("red");
	case EUICommand::Restart: return Command.Index == 1 ? TEXT("restart wet") : TEXT("restart");
	case EUICommand::SafetyCarStart: return Command.Index == 1 ? TEXT("sc_start wet") : TEXT("sc_start");
	case EUICommand::LappedCarsMayOvertake: return TEXT("lapped");
	case EUICommand::ClosePitEntry: return TEXT("pit_close");
	case EUICommand::OpenPitEntry: return TEXT("pit_open");
	case EUICommand::SetCamera: return FString::Printf(TEXT("camera %d"), Command.Index + 1);
	case EUICommand::FollowCar:
		return SimSession.Entries().IsValidIndex(Command.Index) ? TEXT("follow ") + SimSession.Entries()[Command.Index].Code
			: FString(TEXT("follow leader"));
	case EUICommand::SetTimeScale: return FString::Printf(TEXT("timescale %g"), Command.Value);
	case EUICommand::SelectIncident:
		return Command.Index < 0 ? FString(TEXT("incident latest")) : FString::Printf(TEXT("incident %d"), Command.Index + 1);
	case EUICommand::StartReview:
		return Command.Index < 0 ? FString(TEXT("review selected")) : FString::Printf(TEXT("review %d"), Command.Index + 1);
	case EUICommand::StopReview: return TEXT("review_end");
	case EUICommand::ToggleReviewPause: return TEXT("pause");
	case EUICommand::SeekReview: return FString::Printf(TEXT("seek %g"), Command.Value);
	case EUICommand::SetReviewOffset: return FString::Printf(TEXT("review_at %.1f"), Command.Value);
	case EUICommand::NextReviewCamera: return TEXT("review_camera");
	case EUICommand::SetAccused: return Command.Index == 0 ? TEXT("accused car") : TEXT("accused other");
	case EUICommand::Verdict: return FString::Printf(TEXT("verdict %s"), PenaltyWords[static_cast<uint8>(Command.Penalty)]);
	}
	return TEXT("?");
}

bool AUIRaceDirector::Execute(const FUICommand& Command)
{
	const FString Time = FormatRaceTime(SimSession.Snapshot().Time);
	if (!CanExecute(Command))
	{
		UE_LOG(LogUISim, Display, TEXT("Command at %s: %s (not allowed now)"), *Time, *CommandName(Command));
		return false;
	}
	UE_LOG(LogUISim, Display, TEXT("Command at %s: %s"), *Time, *CommandName(Command));
	switch (Command.Type)
	{
	case EUICommand::SetSectorFlag: SimSession.SetSectorFlag(Command.Index, Command.Flag); break;
	case EUICommand::SelectSector: SelectedMarshalSector = Command.Index; break;
	case EUICommand::DeployVsc: SimSession.DeployVirtualSafetyCar(); break;
	case EUICommand::EndVsc: SimSession.EndVirtualSafetyCar(); break;
	case EUICommand::DeploySafetyCar: SimSession.DeploySafetyCar(); break;
	case EUICommand::RecallSafetyCar: SimSession.RecallSafetyCar(); break;
	case EUICommand::RedFlag: SimSession.RedFlag(); break;
	case EUICommand::Restart: SimSession.Restart(Command.Index == 1); break;
	case EUICommand::SafetyCarStart: SimSession.SafetyCarStart(Command.Index == 1); break;
	case EUICommand::LappedCarsMayOvertake: SimSession.LappedCarsMayOvertake(); break;
	case EUICommand::ClosePitEntry: SimSession.ClosePitEntry(); break;
	case EUICommand::OpenPitEntry: SimSession.OpenPitEntry(); break;
	case EUICommand::SetCamera: ActiveCamera = Command.Index; break;
	case EUICommand::FollowCar:
		FollowedEntry = Command.Index;
		if (!bReviewing)
		{
			ActiveCamera = 0; // the TV camera
		}
		break;
	case EUICommand::SetTimeScale: SimTimeScale = Command.Value; break;
	case EUICommand::SelectIncident: SelectedIncidentIndex = Command.Index; break;
	case EUICommand::StartReview: StartReview(Command.Index < 0 ? SelectedIncident() : Command.Index); break;
	case EUICommand::StopReview: StopReview(); break;
	case EUICommand::ToggleReviewPause: bReviewPaused = !bReviewPaused; break;
	case EUICommand::SeekReview:
	{
		const double IncidentTime = SimSession.Incidents()[ReviewIncidentIndex].Time;
		ReviewTime = FMath::Clamp(ReviewTime + Command.Value, IncidentTime - ReviewWindow, IncidentTime + ReviewWindow);
		break;
	}
	case EUICommand::SetReviewOffset:
		ReviewTime = SimSession.Incidents()[ReviewIncidentIndex].Time + FMath::Clamp(Command.Value, -ReviewWindow, ReviewWindow);
		break;
	case EUICommand::NextReviewCamera: ReviewMainMode = (ReviewMainMode + 1) % ReviewCameraModes; break;
	case EUICommand::SetAccused: Accused = Command.Index; break;
	case EUICommand::Verdict:
	{
		const FUISimIncident& Incident = SimSession.Incidents()[ReviewIncidentIndex];
		const int32 Car = Command.Penalty == EUISimPenalty::None ? -1 : (Accused == 0 ? Incident.Car : Incident.OtherCar);
		SimSession.IssueVerdict(Incident.Id, Car, Command.Penalty);
		UE_LOG(LogUISim, Display, TEXT("Verdict #%d: %s %s"), Incident.Id, *PenaltyName(Command.Penalty),
			Car >= 0 ? *SimSession.Entries()[Car].Code : TEXT(""));
		break;
	}
	}
	return true;
}

bool AUIRaceDirector::LoadScript(const FString& File)
{
	const FString Path = FPaths::IsRelative(File) ? FPaths::Combine(FPaths::ProjectDir(), File) : File;
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		UE_LOG(LogUISim, Error, TEXT("Script not found: %s"), *Path);
		return false;
	}
	auto Fail = [&](int32 LineNumber, const FString& Why)
	{
		UE_LOG(LogUISim, Error, TEXT("Script %s line %d: %s"), *File, LineNumber, *Why);
	};
	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		FString Text = Lines[LineIndex];
		int32 Hash = INDEX_NONE;
		if (Text.FindChar(TEXT('#'), Hash))
		{
			Text.LeftInline(Hash);
		}
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		if (Words.Num() == 0)
		{
			continue;
		}
		if (Words.Num() < 2 || !Words[0].IsNumeric())
		{
			Fail(LineIndex + 1, TEXT("expected '<race time in seconds> <command> [arguments]'"));
			continue;
		}
		FScriptLine Line;
		Line.Time = FCString::Atod(*Words[0]);
		const FString& Name = Words[1];
		const FString Arg = Words.Num() > 2 ? Words[2] : FString();
		const int32 Number = FCString::Atoi(*Arg); // 1-based in scripts
		FUICommand& C = Line.Command;
		bool bOk = true;
		if (Name == TEXT("flag") && Words.Num() == 4)
		{
			C.Type = EUICommand::SetSectorFlag;
			C.Index = Number - 1;
			const FString& Flag = Words[3];
			bOk = Flag == TEXT("green") || Flag == TEXT("yellow") || Flag == TEXT("double");
			C.Flag = Flag == TEXT("yellow") ? EUISimFlag::Yellow : Flag == TEXT("double") ? EUISimFlag::DoubleYellow : EUISimFlag::Green;
		}
		else if (Name == TEXT("sector")) { C.Type = EUICommand::SelectSector; C.Index = Number - 1; }
		else if (Name == TEXT("vsc")) { C.Type = EUICommand::DeployVsc; }
		else if (Name == TEXT("vsc_end")) { C.Type = EUICommand::EndVsc; }
		else if (Name == TEXT("sc")) { C.Type = EUICommand::DeploySafetyCar; }
		else if (Name == TEXT("sc_in")) { C.Type = EUICommand::RecallSafetyCar; }
		else if (Name == TEXT("red")) { C.Type = EUICommand::RedFlag; }
		else if (Name == TEXT("restart")) { C.Type = EUICommand::Restart; C.Index = Arg == TEXT("wet") ? 1 : 0; }
		else if (Name == TEXT("sc_start")) { C.Type = EUICommand::SafetyCarStart; C.Index = Arg == TEXT("wet") ? 1 : 0; }
		else if (Name == TEXT("lapped")) { C.Type = EUICommand::LappedCarsMayOvertake; }
		else if (Name == TEXT("pit_close")) { C.Type = EUICommand::ClosePitEntry; }
		else if (Name == TEXT("pit_open")) { C.Type = EUICommand::OpenPitEntry; }
		else if (Name == TEXT("camera")) { C.Type = EUICommand::SetCamera; C.Index = Number - 1; }
		else if (Name == TEXT("follow"))
		{
			C.Type = EUICommand::FollowCar;
			C.Index = SimSession.Entries().IndexOfByPredicate([&](const FUISimEntry& Entry) { return Entry.Code == Arg; });
			bOk = Arg == TEXT("leader") || C.Index >= 0;
		}
		else if (Name == TEXT("timescale")) { C.Type = EUICommand::SetTimeScale; C.Value = FCString::Atod(*Arg); }
		else if (Name == TEXT("incident"))
		{
			C.Type = EUICommand::SelectIncident;
			C.Index = Arg == TEXT("latest") ? -1 : Number - 1;
		}
		else if (Name == TEXT("review"))
		{
			C.Type = EUICommand::StartReview;
			C.Index = Arg.IsEmpty() || Arg == TEXT("selected") ? -1 : Number - 1;
		}
		else if (Name == TEXT("review_end")) { C.Type = EUICommand::StopReview; }
		else if (Name == TEXT("pause")) { C.Type = EUICommand::ToggleReviewPause; }
		else if (Name == TEXT("seek")) { C.Type = EUICommand::SeekReview; C.Value = FCString::Atod(*Arg); }
		else if (Name == TEXT("review_at")) { C.Type = EUICommand::SetReviewOffset; C.Value = FCString::Atod(*Arg); }
		else if (Name == TEXT("review_camera")) { C.Type = EUICommand::NextReviewCamera; }
		else if (Name == TEXT("accused"))
		{
			C.Type = EUICommand::SetAccused;
			bOk = Arg == TEXT("car") || Arg == TEXT("other");
			C.Index = Arg == TEXT("other") ? 1 : 0;
		}
		else if (Name == TEXT("verdict"))
		{
			C.Type = EUICommand::Verdict;
			bOk = false;
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(PenaltyWords); ++Index)
			{
				if (Arg == PenaltyWords[Index])
				{
					C.Penalty = static_cast<EUISimPenalty>(Index);
					bOk = true;
				}
			}
		}
		else if (Name == TEXT("screenshot")) { Line.bScreenshot = true; Line.ShotName = Arg.IsEmpty() ? FString(TEXT("uisim_check")) : Arg; }
		else if (Name == TEXT("click") && Words.Num() == 4)
		{
			Line.bClick = true;
			Line.Click = FVector2D(FCString::Atod(*Words[2]), FCString::Atod(*Words[3]));
		}
		else
		{
			bOk = false;
		}
		if (!bOk)
		{
			Fail(LineIndex + 1, FString::Printf(TEXT("unknown command or arguments: %s"), *Text.TrimStartAndEnd()));
			continue;
		}
		Script.Add(Line);
	}
	Script.StableSort([](const FScriptLine& A, const FScriptLine& B) { return A.Time < B.Time; });
	UE_LOG(LogUISim, Display, TEXT("Script %s: %d commands"), *File, Script.Num());
	return true;
}

void AUIRaceDirector::RunScript()
{
	const FUISimSnapshot& Snapshot = SimSession.Snapshot();
	if (Snapshot.bFinished && ScriptNext < Script.Num())
	{
		// The race clock stops at the flag. Later lines run one per frame from half a second after
		// the finish, so screens that appear at the end (rating) are laid out before they are clicked.
		const double Now = FPlatformTime::Seconds();
		FinishedAt = FinishedAt > 0.0 ? FinishedAt : Now;
		if (Now - FinishedAt < 0.5)
		{
			return;
		}
		RunScriptLine(Script[ScriptNext++]);
		return;
	}
	while (ScriptNext < Script.Num() && Snapshot.Time >= Script[ScriptNext].Time)
	{
		RunScriptLine(Script[ScriptNext++]);
	}
}

void AUIRaceDirector::RunScriptLine(const FScriptLine& Line)
{
	if (Line.bScreenshot)
	{
		const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), Line.ShotName + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(File, true, false); // with UI
		UE_LOG(LogUISim, Display, TEXT("Screenshot requested: %s"), *File);
	}
	else if (Line.bClick)
	{
		SyntheticClick(Line.Click);
	}
	else
	{
		Execute(Line.Command);
	}
}

void AUIRaceDirector::SyntheticClick(const FVector2D& ViewportPixel)
{
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
	if (!FSlateApplication::IsInitialized() || !ViewportWidget.IsValid())
	{
		UE_LOG(LogUISim, Warning, TEXT("Click at %.0f,%.0f: no game viewport"), ViewportPixel.X, ViewportPixel.Y);
		return;
	}
	FSlateApplication& Slate = FSlateApplication::Get();
	if (!ClickUser.IsValid())
	{
		// A virtual Slate user of its own: its pointer exists only inside Slate, so the real
		// mouse cursor is never moved or captured.
		ClickUser = Slate.FindOrCreateVirtualUser(0);
	}
	const int32 UserIndex = ClickUser->GetUserIndex();
	const FVector2D Screen = FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition()) + ViewportPixel;
	const FWidgetPath Path = Slate.LocateWindowUnderMouse(Screen, Slate.GetInteractiveTopLevelWindows(), false, UserIndex);
	if (!Path.IsValid())
	{
		UE_LOG(LogUISim, Display, TEXT("Click at %.0f,%.0f hits nothing"), ViewportPixel.X, ViewportPixel.Y);
		return;
	}
	const FPointerEvent Down(UserIndex, 0, Screen, Screen, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.0f,
		FModifierKeysState());
	const FPointerEvent Up(UserIndex, 0, Screen, Screen, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
	Slate.RoutePointerDownEvent(Path, Down);
	Slate.RoutePointerUpEvent(Path, Up);
	UE_LOG(LogUISim, Display, TEXT("Click at %.0f,%.0f hits %s"), ViewportPixel.X, ViewportPixel.Y,
		*Path.Widgets.Last().Widget->GetTypeAsString());
}
