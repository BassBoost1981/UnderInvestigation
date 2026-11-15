#include "UIControlPanelWidget.h"

#include "UIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogUISim, Log, All); // same category name as UIRaceDirector.cpp

namespace
{
	// Measures of the design, relative to the card (487 wide).
	constexpr double Pad = 8.0;
	constexpr double StatusTop = 47.0, StatusHeight = 52.0, StatusStep = 58.0;
	constexpr double ToggleTop = 221.0, ToggleHeight = 41.0;
	constexpr double SectorLabelTop = 270.0, SectorTop = 288.0, SectorHeight = 27.0, SectorStep = 30.0;
	constexpr double LineTop = 355.0;
	constexpr double LightsTop = 364.0;
	constexpr double MoreLabelTop = 408.0, MoreTop = 422.0, MoreHeight = 26.0, MoreStep = 32.0;
	constexpr double TileWidth = 117.0, TileHeight = 66.0, TileStep = 123.0; // camera tiles, five in a row
	constexpr double ConfirmSeconds = 5.0; // the red flag question expires after this many seconds (real time)

	const FLinearColor Fill = UIStyle::Rgb(34, 35, 39);   // white at 6 % on the card
	const FLinearColor Edge = UIStyle::Rgb(44, 45, 49);   // white at 10 %
	const FLinearColor DarkYellow = UIStyle::Rgb(168, 135, 15);

	FUICommand MakeCommand(EUICommand Type, int32 Index = 0, EUISimFlag Flag = EUISimFlag::Green)
	{
		FUICommand Command;
		Command.Type = Type;
		Command.Index = Index;
		Command.Flag = Flag;
		return Command;
	}

	// Marshal sector the leader is in (sectors are equally long, as on the track map).
	int32 LeaderSector(const FUISimSession& Session)
	{
		const TArray<FUISimCar>& Cars = Session.Snapshot().Cars;
		const int32 Sectors = FMath::Max(1, Session.MarshalSectorCount());
		if (Cars.Num() == 0 || Session.TrackLength() <= 0.0)
		{
			return 0;
		}
		const double Lap = FMath::Fmod(FMath::Fmod(Cars[0].Distance, Session.TrackLength()) + Session.TrackLength(), Session.TrackLength());
		return FMath::Clamp(FMath::FloorToInt32(Lap / Session.TrackLength() * Sectors), 0, Sectors - 1);
	}
}

TSharedRef<SWidget> UUIControlPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		auto MakeArea = [&](const FBox2D& Rect)
		{
			UBorder* Area = WidgetTree->ConstructWidget<UBorder>();
			Area->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f)); // visible, so it takes the clicks; the desk draws the card
			UCanvasPanelSlot* AreaSlot = Canvas->AddChildToCanvas(Area);
			AreaSlot->SetPosition(Rect.Min);
			AreaSlot->SetSize(Rect.GetSize());
			return Area;
		};
		StatusArea = MakeArea(UIStyle::StatusCard);
		TilesArea = MakeArea(UIStyle::Tiles);
	}
	return Super::RebuildWidget();
}

void UUIControlPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Until M12.6 the review dialog needs the whole right column: the card makes room for it.
	const AUIRaceDirector* Race = Director.Get();
	if (StatusArea)
	{
		StatusArea->SetVisibility(Race && Race->IsReviewing() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
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
	const FUISimSession& Session = Race->Session();
	const FUISimSnapshot& Snapshot = Session.Snapshot();
	const EUISimRaceState State = Snapshot.State;
	const double CardWidth = UIStyle::StatusCard.GetSize().X;
	const double Inner = CardWidth - 2.0 * Pad;
	auto Add = [&](const FBox2D& Rect, ELook Look, const FString& Label, const FString& Sub, const FUICommand& Command, bool bActive,
		const FLinearColor& Color, EKind Kind = EKind::Command) -> FButton&
	{
		FButton& Button = List.AddDefaulted_GetRef();
		Button.Rect = Rect;
		Button.Look = Look;
		Button.Label = Label;
		Button.Sub = Sub;
		Button.Command = Command;
		Button.Kind = Kind;
		Button.bEnabled = Kind != EKind::Command && Kind != EKind::AskRedFlag ? Kind != EKind::Display : Race->CanExecute(Command);
		Button.bActive = bActive;
		Button.Color = Color;
		return Button;
	};

	if (!Race->IsReviewing())
	{
		// The six state buttons, two in a row.
		const double Half = (Inner - 6.0) / 2.0;
		auto StatusRect = [&](int32 Index)
		{
			const FVector2D Min(Pad + (Index % 2) * (Half + 6.0), StatusTop + (Index / 2) * StatusStep);
			return FBox2D(Min, Min + FVector2D(Half, StatusHeight));
		};
		bool bYellow = false;
		for (const EUISimFlag Flag : Snapshot.SectorFlags)
		{
			bYellow |= Flag != EUISimFlag::Green;
		}
		const bool bRed = State == EUISimRaceState::RedFlag;
		const int32 Sector = LeaderSector(Session);
		const FString AtLeader = FString::Printf(TEXT("at leader · sector %d"), Sector + 1);
		if (bRed)
		{
			Add(StatusRect(0), ELook::Status, TEXT("RESTART"), TEXT("rolling start behind the safety car"),
				MakeCommand(EUICommand::Restart, bWetMandate ? 1 : 0), false, UIStyle::Green);
		}
		else
		{
			Add(StatusRect(0), ELook::Status, TEXT("TRACK CLEAR"), TEXT("green · clears all sectors"), FUICommand(),
				State == EUISimRaceState::Green && !bYellow && Snapshot.bRaceStarted, UIStyle::Green, EKind::ClearTrack);
		}
		FButton& Red = Add(StatusRect(1), ELook::Status, IsConfirming() ? TEXT("CONFIRM RED FLAG") : TEXT("RED FLAG"),
			IsConfirming() ? TEXT("click again to show it") : TEXT("suspend session"), MakeCommand(EUICommand::RedFlag), bRed || IsConfirming(),
			UIStyle::Red, EKind::AskRedFlag);
		Red.bDarkInk = false;
		Red.bEnabled |= bRed; // shown bright while the flag is out
		Add(StatusRect(2), ELook::Status, TEXT("YELLOW"), AtLeader, MakeCommand(EUICommand::SetSectorFlag, Sector, EUISimFlag::Yellow), false,
			UIStyle::Yellow);
		Add(StatusRect(3), ELook::Status, TEXT("DOUBLE YELLOW"), AtLeader, MakeCommand(EUICommand::SetSectorFlag, Sector, EUISimFlag::DoubleYellow),
			false, UIStyle::Yellow);
		const bool bVsc = State == EUISimRaceState::VirtualSafetyCar;
		Add(StatusRect(4), ELook::Status, bVsc ? TEXT("VSC ENDING") : TEXT("VSC"), bVsc ? TEXT("return to green") : TEXT("virtual safety car"),
			MakeCommand(bVsc ? EUICommand::EndVsc : EUICommand::DeployVsc), bVsc, UIStyle::Yellow);
		const bool bSc = State == EUISimRaceState::SafetyCar;
		FButton& Sc = Add(StatusRect(5), ELook::Status, bSc ? TEXT("SC IN THIS LAP") : TEXT("SAFETY CAR"),
			!bSc ? TEXT("deploy") : Snapshot.bSafetyCarInThisLap ? TEXT("lights out, pitting") : TEXT("call the car in"),
			MakeCommand(bSc ? EUICommand::RecallSafetyCar : EUICommand::DeploySafetyCar, bSc && bStanding ? 2 : 0), bSc, UIStyle::Yellow);
		Sc.bEnabled |= bSc;

		// Overtake (shown only: the core switches it) and the pit lane.
		const bool bOvertake = State == EUISimRaceState::Green && Snapshot.bRaceStarted && !Snapshot.bChequered;
		Add(FBox2D(FVector2D(Pad, ToggleTop), FVector2D(Pad + Half, ToggleTop + ToggleHeight)), ELook::Toggle, TEXT("OVERTAKE"),
			bOvertake ? TEXT("ENABLED") : TEXT("DISABLED"), FUICommand(), false, bOvertake ? UIStyle::Green : UIStyle::Mute, EKind::Display);
		Add(FBox2D(FVector2D(Pad + Half + 6.0, ToggleTop), FVector2D(Pad + Inner, ToggleTop + ToggleHeight)), ELook::Toggle, TEXT("PIT LANE"),
			Snapshot.bPitEntryClosed ? TEXT("CLOSED") : TEXT("OPEN"),
			MakeCommand(Snapshot.bPitEntryClosed ? EUICommand::OpenPitEntry : EUICommand::ClosePitEntry), false,
			Snapshot.bPitEntryClosed ? UIStyle::Red : UIStyle::Green);

		// Marshal sectors: a click moves the flag on, green - yellow - double yellow.
		const int32 Sectors = Snapshot.SectorFlags.Num();
		const int32 Columns = FMath::Max(9, FMath::DivideAndRoundUp(Sectors, 2));
		const double Cell = (Inner - 3.0 * (Columns - 1)) / Columns;
		for (int32 Index = 0; Index < Sectors; ++Index)
		{
			const EUISimFlag Flag = Snapshot.SectorFlags[Index];
			const EUISimFlag Next = Flag == EUISimFlag::Green ? EUISimFlag::Yellow
				: Flag == EUISimFlag::Yellow ? EUISimFlag::DoubleYellow : EUISimFlag::Green;
			const FVector2D Min(Pad + (Index % Columns) * (Cell + 3.0), SectorTop + (Index / Columns) * SectorStep);
			FButton& Button = Add(FBox2D(Min, Min + FVector2D(Cell, SectorHeight)), ELook::Sector, FString::FromInt(Index + 1), FString(),
				MakeCommand(EUICommand::SetSectorFlag, Index, Next), Flag != EUISimFlag::Green, UIStyle::Yellow);
			Button.Flag = Flag;
		}

		// Start lights (drawn in NativePaint) and the standing restart beside them.
		const FBox2D RestartRect(FVector2D(143.0, LightsTop - 2.0), FVector2D(CardWidth - 11.0, LightsTop + 32.0));
		if (bRed)
		{
			Add(RestartRect, ELook::Capsule, TEXT("START LIGHT SEQUENCE"), FString(), MakeCommand(EUICommand::Restart, (bWetMandate ? 1 : 0) | 2),
				false, UIStyle::Text);
		}
		else
		{
			Add(RestartRect, ELook::Capsule, Snapshot.bGridStart ? TEXT("LIGHTS…") : TEXT("START LIGHTS · STANDBY"), FString(), FUICommand(),
				false, UIStyle::Mute, EKind::Display);
		}

		// What the design does not know: three in a row.
		const double Third = (Inner - 8.0) / 3.0;
		auto MoreRect = [&](int32 Index)
		{
			const FVector2D Min(Pad + (Index % 3) * (Third + 4.0), MoreTop + (Index / 3) * MoreStep);
			return FBox2D(Min, Min + FVector2D(Third, MoreHeight));
		};
		Add(MoreRect(0), ELook::Small, TEXT("LAPPED CARS PASS"), FString(), MakeCommand(EUICommand::LappedCarsMayOvertake),
			Snapshot.bLappedCarsReleased, UIStyle::Yellow);
		Add(MoreRect(1), ELook::Small, TEXT("START BEHIND SC"), FString(), MakeCommand(EUICommand::SafetyCarStart, bWetMandate ? 1 : 0), false,
			UIStyle::Yellow);
		Add(MoreRect(2), ELook::Small, TEXT("ABORT START"), FString(), MakeCommand(EUICommand::AbortStart), false, UIStyle::Yellow);
		Add(MoreRect(3), ELook::Small, TEXT("EXTRA FORMATION LAP"), FString(), MakeCommand(EUICommand::ExtraFormationLap), false, UIStyle::Yellow);
		Add(MoreRect(4), ELook::Small, TEXT("WET TYRES ONLY"), FString(), FUICommand(), bWetMandate, UIStyle::Rgb(80, 150, 255), EKind::ToggleWet);
		Add(MoreRect(5), ELook::Small, TEXT("STANDING AFTER SC"), FString(), FUICommand(), bStanding, UIStyle::Red, EKind::ToggleStanding).bDarkInk = false;
	}

	// Camera tiles under the 3D picture (until M12.5 without the number of cars in view).
	const TArray<FString>& Cameras = Race->CameraNameList();
	for (int32 Index = 0; Index < Cameras.Num() && Index < 10; ++Index)
	{
		const FVector2D Min((Index % 5) * TileStep, (Index / 5) * (TileHeight + 6.0));
		Add(FBox2D(Min, Min + FVector2D(TileWidth, TileHeight)), ELook::Tile, Cameras[Index].ToUpper(), FString(),
			MakeCommand(EUICommand::SetCamera, Index), Race->ActiveCameraIndex() == Index, UIStyle::Text).bTile = true;
	}
	return List;
}

void UUIControlPanelWidget::LogClickTargets() const
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
		const FGeometry& Area = (Button.bTile ? TilesArea : StatusArea)->GetCachedGeometry();
		const FVector2D At = FVector2D(Area.LocalToAbsolute(Button.Rect.GetCenter())) - Origin;
		UE_LOG(LogUISim, Display, TEXT("Track status: '%s%s' at %.0f %.0f"), Button.Look == ELook::Sector ? TEXT("sector ") : TEXT(""),
			*Button.Label, At.X, At.Y);
	}
}

int32 UUIControlPanelWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted() || !StatusArea || !TilesArea || TilesArea->GetCachedGeometry().GetLocalSize().X <= 0.0)
	{
		return Layer;
	}
	static const FSlateRoundedBoxBrush Round12(FLinearColor::White, 12.0f);
	static const FSlateRoundedBoxBrush Round11(FLinearColor::White, 11.0f);
	static const FSlateRoundedBoxBrush Round8(FLinearColor::White, 8.0f);
	static const FSlateRoundedBoxBrush Round7(FLinearColor::White, 7.0f);
	static const FSlateRoundedBoxBrush Capsule(FLinearColor::White, 20.0f);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FUISimSnapshot& Snapshot = Race->Session().Snapshot();
	const bool bReviewing = Race->IsReviewing();
	const FGeometry* Area = &StatusArea->GetCachedGeometry();
	++Layer;
	auto Box = [&](const FVector2D& Min, const FVector2D& Max, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Area->ToPaintGeometry(Max - Min, FSlateLayoutTransform(1.0f, Min)), Brush,
			ESlateDrawEffect::None, Color);
	};
	// Fill with a border one unit wide, as the buttons of the design.
	auto Framed = [&](const FBox2D& Rect, const FSlateBrush* Outer, const FSlateBrush* Inner, const FLinearColor& Border, const FLinearColor& Inside)
	{
		Box(Rect.Min, Rect.Max, Outer, Border);
		Box(Rect.Min + FVector2D(1.0, 1.0), Rect.Max - FVector2D(1.0, 1.0), Inner, Inside);
	};
	auto Text = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, Area->ToPaintGeometry(FVector2D(500.0, 30.0), FSlateLayoutTransform(1.0f, At)),
			String, Font, ESlateDrawEffect::None, Color);
	};
	auto TextCentred = [&](const FVector2D& Centre, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		const FVector2D Size = Measure->Measure(String, Font);
		Text(Centre - Size / 2.0, String, Font, Color);
	};
	auto Dim = [](FLinearColor Color, bool bEnabled) // what is not allowed now stands back
	{
		Color.A *= bEnabled ? 1.0f : 0.4f;
		return Color;
	};
	const FSlateFontInfo LabelFont = UIStyle::Font(TEXT("Regular"), 7);
	const double CardWidth = UIStyle::StatusCard.GetSize().X;

	if (!bReviewing)
	{
		Text(FVector2D(17.0, 15.0), TEXT("Track status"), UIStyle::Font(TEXT("Bold"), 10), UIStyle::Text);
		Text(FVector2D(Pad, SectorLabelTop), TEXT("MARSHAL SECTORS"), LabelFont, UIStyle::Mute);
		Box(FVector2D(1.0, LineTop), FVector2D(CardWidth - 1.0, LineTop + 1.0), White, FLinearColor(1.0f, 1.0f, 1.0f, 0.07f));
		Text(FVector2D(Pad, MoreLabelTop), TEXT("MORE COMMANDS"), LabelFont, UIStyle::Mute);
		// Start lights: five in a dark capsule, red one after the other.
		Framed(FBox2D(FVector2D(11.0, LightsTop), FVector2D(133.0, LightsTop + 32.0)), &Capsule, &Capsule, Edge, UIStyle::Rgb(13, 14, 16));
		for (int32 Index = 0; Index < 5; ++Index)
		{
			const FVector2D Min(22.0 + Index * 21.0, LightsTop + 8.0);
			Box(Min, Min + FVector2D(16.0, 16.0), &Round8, Snapshot.bGridStart && Index < Snapshot.StartLights ? UIStyle::Red : UIStyle::Rgb(42, 20, 22));
		}
	}

	for (const FButton& Button : Buttons())
	{
		Area = &(Button.bTile ? TilesArea : StatusArea)->GetCachedGeometry();
		const FBox2D& Rect = Button.Rect;
		const FLinearColor Ink = Button.bActive ? (Button.bDarkInk ? UIStyle::Ink : FLinearColor::White) : Dim(UIStyle::Text, Button.bEnabled);
		switch (Button.Look)
		{
		case ELook::Status:
			Framed(Rect, &Round12, &Round11, Button.bActive ? Button.Color : Edge, Button.bActive ? Button.Color : Fill);
			Box(Rect.Min + FVector2D(10.0, 13.0), Rect.Min + FVector2D(18.0, 21.0), &Round8, Button.bActive ? Ink : Dim(Button.Color, Button.bEnabled));
			Text(Rect.Min + FVector2D(25.0, 9.0), Button.Label, UIStyle::Font(TEXT("Bold"), 10), Ink);
			Text(Rect.Min + FVector2D(10.0, 30.0), Button.Sub, LabelFont, Ink);
			break;
		case ELook::Toggle:
		{
			Framed(Rect, &Round12, &Round11, Edge, Fill);
			Text(Rect.Min + FVector2D(12.0, 14.0), Button.Label, UIStyle::Font(TEXT("Bold"), 9, 40), UIStyle::Text);
			const FSlateFontInfo ChipFont = UIStyle::Font(TEXT("Bold"), 7);
			const double ChipWidth = Measure->Measure(Button.Sub, ChipFont).X + 16.0;
			const FVector2D ChipMin(Rect.Max.X - 12.0 - ChipWidth, Rect.GetCenter().Y - 9.0);
			Box(ChipMin, ChipMin + FVector2D(ChipWidth, 18.0), &Round12, Button.Color);
			TextCentred(ChipMin + FVector2D(ChipWidth / 2.0, 9.0), Button.Sub, ChipFont, UIStyle::Ink);
			break;
		}
		case ELook::Sector:
			Framed(Rect, &Round8, &Round7, Edge, Button.bActive ? UIStyle::Yellow : Fill);
			if (Button.Flag == EUISimFlag::DoubleYellow)
			{
				// Double yellow: darker stripes, as in the design.
				for (double Left = Rect.Min.X + 6.0; Left + 5.0 < Rect.Max.X - 2.0; Left += 10.0)
				{
					Box(FVector2D(Left, Rect.Min.Y + 1.0), FVector2D(Left + 5.0, Rect.Max.Y - 1.0), White, DarkYellow);
				}
			}
			TextCentred(Rect.GetCenter(), Button.Label, UIStyle::Font(TEXT("Bold"), 8), Button.bActive ? UIStyle::Ink : UIStyle::Mute);
			break;
		case ELook::Capsule:
			Framed(Rect, &Capsule, &Capsule, Edge, Fill);
			TextCentred(Rect.GetCenter(), Button.Label, UIStyle::Font(TEXT("Bold"), 9, 40), Button.Color);
			break;
		case ELook::Small:
			Framed(Rect, &Round8, &Round7, Button.bActive ? Button.Color : Edge, Button.bActive ? Button.Color : Fill);
			TextCentred(Rect.GetCenter(), Button.Label, UIStyle::Font(TEXT("Bold"), 7), Ink);
			break;
		case ELook::Tile:
			Framed(Rect, &Round12, &Round11, Button.bActive ? UIStyle::Text : Edge, UIStyle::Rgb(18, 19, 22));
			Text(Rect.Min + FVector2D(8.0, 8.0), Button.Label, UIStyle::Font(TEXT("Bold"), 7), UIStyle::Text);
			break;
		}
	}
	Layer += 2;

	if (!bTargetsLogged && !bReviewing && StatusArea->GetCachedGeometry().GetLocalSize().X > 0.0)
	{
		bTargetsLogged = true;
		LogClickTargets();
	}
	return Layer;
}

FReply UUIControlPanelWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (!Race || !StatusArea || !TilesArea || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Screen = InMouseEvent.GetScreenSpacePosition();
	const FVector2D InStatus = FVector2D(StatusArea->GetCachedGeometry().AbsoluteToLocal(Screen));
	const FVector2D InTiles = FVector2D(TilesArea->GetCachedGeometry().AbsoluteToLocal(Screen));
	for (const FButton& Button : Buttons())
	{
		if (!Button.Rect.IsInside(Button.bTile ? InTiles : InStatus))
		{
			continue;
		}
		switch (Button.Kind)
		{
		case EKind::AskRedFlag:
			if (IsConfirming())
			{
				ConfirmUntil = 0.0;
				Race->Execute(Button.Command);
			}
			else if (Race->CanExecute(Button.Command))
			{
				ConfirmUntil = FPlatformTime::Seconds() + ConfirmSeconds; // a red flag only after a second click
				UE_LOG(LogUISim, Display, TEXT("Track status: red flag? waiting for the second click"));
			}
			break;
		case EKind::ClearTrack:
			for (int32 Sector = 0; Sector < Race->Session().Snapshot().SectorFlags.Num(); ++Sector)
			{
				if (Race->Session().Snapshot().SectorFlags[Sector] != EUISimFlag::Green)
				{
					Race->Execute(MakeCommand(EUICommand::SetSectorFlag, Sector, EUISimFlag::Green));
				}
			}
			break;
		case EKind::Command:
			Race->Execute(Button.Command); // logs and refuses what is not allowed now
			break;
		case EKind::ToggleStanding:
			bStanding = !bStanding;
			UE_LOG(LogUISim, Display, TEXT("Track status: standing start after the SC %s"), bStanding ? TEXT("on") : TEXT("off"));
			break;
		case EKind::ToggleWet:
			bWetMandate = !bWetMandate;
			UE_LOG(LogUISim, Display, TEXT("Track status: wet tyres compulsory %s"), bWetMandate ? TEXT("on") : TEXT("off"));
			break;
		case EKind::Display:
			break;
		}
		return FReply::Handled();
	}
	return FReply::Handled(); // a click between the buttons
}
