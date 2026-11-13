#include "UIControlPanelWidget.h"

#include "UIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogUISim, Log, All); // same category name as UIRaceDirector.cpp

namespace
{
	constexpr double AreaWidth = 590.0;
	// Row 1 (left edge, width) and row 2 time buttons; wide enough for the labels.
	constexpr double VscLeft = 0.0, VscWidth = 100.0;
	constexpr double ScLeft = 106.0, ScWidth = 100.0;
	constexpr double RedLeft = 212.0, RedWidth = 100.0;
	constexpr double LappedLeft = 318.0, LappedWidth = 150.0;
	constexpr double PitLeft = 474.0, PitWidth = 116.0;
	constexpr double TimeWidth = 90.0;
	constexpr double CameraWidth = 50.0;
	constexpr double TimeMinusLeft = 6 * (CameraWidth + 6.0) + 6.0;
	constexpr double TimeLabelLeft = TimeMinusLeft + TimeWidth + 6.0;
	constexpr double TimePlusLeft = TimeLabelLeft + 56.0 + 6.0;
	constexpr double RowHeight = 26.0;
	constexpr double Gap = 6.0;
	constexpr double ConfirmSeconds = 5.0; // the red flag question expires after this many seconds (real time)

	double RowTop(int32 Row) { return Row * (RowHeight + Gap); }

	FBox2D Cell(double Left, int32 Row, double Width)
	{
		return FBox2D(FVector2D(Left, RowTop(Row)), FVector2D(Left + Width, RowTop(Row) + RowHeight));
	}

	FUICommand MakeCommand(EUICommand Type, int32 Index = 0, double Value = 0.0)
	{
		FUICommand Command;
		Command.Type = Type;
		Command.Index = Index;
		Command.Value = Value;
		return Command;
	}
}

TSharedRef<SWidget> UUIControlPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrushColor(FLinearColor::Transparent); // the card of the desk is the background
		Panel->SetPadding(FMargin(8.0f));
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetPosition(UIStyle::Tiles.Min + FVector2D(-6.0, 2.0));

		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(AreaWidth);
		Box->SetHeightOverride(RowTop(3) - Gap);
		Panel->SetContent(Box);
		ButtonArea = WidgetTree->ConstructWidget<UBorder>();
		ButtonArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f)); // visible, so it takes the clicks
		Box->SetContent(ButtonArea);
	}
	return Super::RebuildWidget();
}

bool UUIControlPanelWidget::IsConfirming() const
{
	return FPlatformTime::Seconds() < ConfirmUntil;
}

TArray<UUIControlPanelWidget::FButton> UUIControlPanelWidget::Buttons() const
{
	TArray<FButton> List;
	const AUIRaceDirector* Race = Director.Get();
	if (!Race)
	{
		return List;
	}
	const FUISimSnapshot& Snapshot = Race->Session().Snapshot();
	const EUISimRaceState State = Snapshot.State;
	auto Add = [&](const FBox2D& Rect, const FString& Label, const FUICommand& Command, bool bActive,
		EKind Kind = EKind::Command, FLinearColor Color = FLinearColor(1.0f, 0.85f, 0.2f))
	{
		FButton Button;
		Button.Rect = Rect;
		Button.Label = Label;
		Button.Command = Command;
		Button.Kind = Kind;
		Button.bEnabled = Kind == EKind::ConfirmNo || Race->CanExecute(Command);
		Button.bActive = bActive;
		Button.ActiveColor = Color;
		List.Add(Button);
	};

	// Row 0: the red flag question while it is open.
	if (IsConfirming())
	{
		Add(Cell(250.0, 0, 110.0), TEXT("JA, ROT"), MakeCommand(EUICommand::RedFlag), true, EKind::ConfirmYes,
			FLinearColor(0.9f, 0.1f, 0.1f));
		Add(Cell(366.0, 0, 110.0), TEXT("NEIN"), MakeCommand(EUICommand::RedFlag), false, EKind::ConfirmNo);
	}

	// Row 0 (right): start behind the SC and the wet tyre mandate for it and for a restart (M9.7).
	if (!IsConfirming())
	{
		Add(Cell(PitLeft - 116.0, 0, 110.0), TEXT("SC-START"), MakeCommand(EUICommand::SafetyCarStart, bWetMandate ? 1 : 0), false);
		FButton Wet;
		Wet.Rect = Cell(PitLeft, 0, PitWidth);
		Wet.Label = TEXT("REGEN-PFLICHT");
		Wet.Kind = EKind::ToggleWet;
		Wet.bActive = bWetMandate;
		Wet.ActiveColor = FLinearColor(0.3f, 0.6f, 1.0f);
		List.Add(Wet);
		// Standing start from the grid when the SC comes in after formation laps or a red flag (M10.7).
		FButton Standing;
		Standing.Rect = Cell(PitLeft - 116.0 - 106.0, 0, 100.0);
		Standing.Label = TEXT("STEHEND");
		Standing.Kind = EKind::ToggleStanding;
		Standing.bActive = bStanding;
		Standing.ActiveColor = FLinearColor(0.9f, 0.1f, 0.1f);
		List.Add(Standing);
		// Before the start signal (M11.7): call the start off (B5.8) or send the field round again (B5.9).
		Add(Cell(0.0, 0, 120.0), TEXT("STARTABBRUCH"), MakeCommand(EUICommand::AbortStart), false);
		Add(Cell(126.0, 0, 120.0), TEXT("EXTRA-RUNDE"), MakeCommand(EUICommand::ExtraFormationLap), false);
	}

	// Row 1: race control. Toggle buttons stand for the command that fits the state, like the keys.
	const bool bVsc = State == EUISimRaceState::VirtualSafetyCar;
	Add(Cell(VscLeft, 1, VscWidth), bVsc ? TEXT("VSC ENDE [V]") : TEXT("VSC [V]"),
		MakeCommand(bVsc ? EUICommand::EndVsc : EUICommand::DeployVsc), bVsc);
	const bool bSc = State == EUISimRaceState::SafetyCar;
	Add(Cell(ScLeft, 1, ScWidth), bSc ? TEXT("SC REIN [S]") : TEXT("SC [S]"),
		MakeCommand(bSc ? EUICommand::RecallSafetyCar : EUICommand::DeploySafetyCar, bSc && bStanding ? 2 : 0), bSc,
		EKind::Command, FLinearColor(1.0f, 0.55f, 0.1f));
	const bool bRed = State == EUISimRaceState::RedFlag;
	if (bRed)
	{
		Add(Cell(RedLeft, 1, RedWidth), TEXT("NEUSTART [R]"), MakeCommand(EUICommand::Restart, (bWetMandate ? 1 : 0) | (bStanding ? 2 : 0)), true, EKind::Command,
			FLinearColor(0.9f, 0.1f, 0.1f));
	}
	else
	{
		Add(Cell(RedLeft, 1, RedWidth), TEXT("ROT [R]"), MakeCommand(EUICommand::RedFlag), IsConfirming(), EKind::AskRedFlag,
			FLinearColor(0.9f, 0.1f, 0.1f));
	}
	Add(Cell(LappedLeft, 1, LappedWidth), TEXT("UEBERRUNDETE [L]"), MakeCommand(EUICommand::LappedCarsMayOvertake),
		Snapshot.bLappedCarsReleased);
	Add(Cell(PitLeft, 1, PitWidth), Snapshot.bPitEntryClosed ? TEXT("BOX AUF [P]") : TEXT("BOX ZU [P]"),
		MakeCommand(Snapshot.bPitEntryClosed ? EUICommand::OpenPitEntry : EUICommand::ClosePitEntry), Snapshot.bPitEntryClosed);

	// Row 2: cameras and time scale.
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Add(Cell(Index * (CameraWidth + Gap), 2, CameraWidth), FString::Printf(TEXT("KAM %d"), Index + 1), MakeCommand(EUICommand::SetCamera, Index),
			Race->ActiveCameraIndex() == Index);
	}
	Add(Cell(TimeMinusLeft, 2, TimeWidth), TEXT("ZEIT - [-]"), MakeCommand(EUICommand::SetTimeScale, 0, AUIRaceDirector::StepTimeScale(Race->TimeScale(), -1)),
		false);
	Add(Cell(TimePlusLeft, 2, TimeWidth), TEXT("ZEIT + [+]"),
		MakeCommand(EUICommand::SetTimeScale, 0, AUIRaceDirector::StepTimeScale(Race->TimeScale(), 1)), false);
	return List;
}

void UUIControlPanelWidget::LogClickTargets(const FGeometry& Area) const
{
	// Viewport pixels of the buttons, for -UISimScript checks (click x y).
	const UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
	if (!ViewportWidget.IsValid())
	{
		return;
	}
	const FVector2D Origin = FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
	for (const FButton& Button : Buttons())
	{
		const FVector2D At = FVector2D(Area.LocalToAbsolute(Button.Rect.GetCenter())) - Origin;
		UE_LOG(LogUISim, Display, TEXT("Control panel: '%s' at %.0f %.0f"), *Button.Label, At.X, At.Y);
	}
	const FVector2D Yes = FVector2D(Area.LocalToAbsolute(Cell(250.0, 0, 110.0).GetCenter())) - Origin;
	const FVector2D No = FVector2D(Area.LocalToAbsolute(Cell(366.0, 0, 110.0).GetCenter())) - Origin;
	UE_LOG(LogUISim, Display, TEXT("Control panel: 'JA, ROT' at %.0f %.0f, 'NEIN' at %.0f %.0f (while asking)"), Yes.X, Yes.Y, No.X, No.Y);
}

int32 UUIControlPanelWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted() || !ButtonArea)
	{
		return Layer;
	}
	const FGeometry& Area = ButtonArea->GetCachedGeometry();
	if (Area.GetLocalSize().X <= 0.0)
	{
		return Layer;
	}
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Mono", 9);
	auto Text = [&](const FVector2D& TopLeft, const FString& String, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, Area.ToPaintGeometry(FVector2D(200.0, RowHeight),
			FSlateLayoutTransform(1.0f, TopLeft)), String, Font, ESlateDrawEffect::None, Color);
	};

	++Layer;
	if (IsConfirming())
	{
		Text(FVector2D(0.0, 6.0), TEXT("Rote Flagge zeigen?"), FLinearColor(1.0f, 0.3f, 0.3f));
	}
	Text(FVector2D(TimeLabelLeft + 8.0, RowTop(2) + 6.0), FString::Printf(TEXT("x%g"), Race->TimeScale()), FLinearColor::White);
	for (const FButton& Button : Buttons())
	{
		const FLinearColor Fill = !Button.bEnabled ? FLinearColor(0.08f, 0.08f, 0.08f, 0.9f)
			: Button.bActive ? Button.ActiveColor : FLinearColor(0.22f, 0.22f, 0.24f, 0.95f);
		const FLinearColor Ink = !Button.bEnabled ? FLinearColor(0.4f, 0.4f, 0.4f)
			: Button.bActive ? FLinearColor::Black : FLinearColor::White;
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Area.ToPaintGeometry(Button.Rect.GetSize(), FSlateLayoutTransform(1.0f, Button.Rect.Min)),
			White, ESlateDrawEffect::None, Fill);
		Text(Button.Rect.Min + FVector2D(6.0, 6.0), Button.Label, Ink);
	}
	++Layer;

	if (!bTargetsLogged)
	{
		bTargetsLogged = true;
		LogClickTargets(Area);
	}
	return Layer;
}

FReply UUIControlPanelWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (!Race || !ButtonArea || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Local = FVector2D(ButtonArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	for (const FButton& Button : Buttons())
	{
		if (!Button.Rect.IsInside(Local))
		{
			continue;
		}
		switch (Button.Kind)
		{
		case EKind::AskRedFlag:
			if (Button.bEnabled)
			{
				ConfirmUntil = FPlatformTime::Seconds() + ConfirmSeconds; // a red flag only after "yes"
				UE_LOG(LogUISim, Display, TEXT("Control panel: red flag? waiting for confirmation"));
			}
			break;
		case EKind::ConfirmYes:
			ConfirmUntil = 0.0;
			Race->Execute(Button.Command);
			break;
		case EKind::ConfirmNo:
			ConfirmUntil = 0.0;
			UE_LOG(LogUISim, Display, TEXT("Control panel: red flag cancelled"));
			break;
		case EKind::Command:
			Race->Execute(Button.Command); // logs and refuses what is not allowed now
			break;
		case EKind::ToggleStanding:
			bStanding = !bStanding;
			UE_LOG(LogUISim, Display, TEXT("Control panel: standing start %s"), bStanding ? TEXT("on") : TEXT("off"));
			break;
		case EKind::ToggleWet:
			bWetMandate = !bWetMandate;
			UE_LOG(LogUISim, Display, TEXT("Control panel: wet tyres compulsory %s"), bWetMandate ? TEXT("on") : TEXT("off"));
			break;
		}
		return FReply::Handled();
	}
	return FReply::Handled(); // a click between the buttons
}
