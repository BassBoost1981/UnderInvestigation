#include "UITimingTowerWidget.h"

#include "UIRaceDirector.h"
#include "UIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogUISim, Log, All); // same category name as UIRaceDirector.cpp

namespace
{
	constexpr int32 MaxRows = 26;
	constexpr double TowerWidth = 330.0;
	constexpr double HeaderHeight = 80.0; // lap counter, state band and the mode switch
	constexpr double RowHeight = 26.0;
	// Columns of a row (left edges).
	constexpr double PositionWidth = 30.0;
	constexpr double BarLeft = 30.0;
	constexpr double CodeLeft = 44.0;
	constexpr double GapRight = 200.0; // gaps are right-aligned to this edge
	constexpr double TyreLeft = 210.0;
	constexpr double TyreSize = 18.0;
	constexpr double TagLeft = 254.0;
	const FBox2D ModeLabel(FVector2D(GapRight - 140.0, 58.0), FVector2D(GapRight, 76.0)); // switch with two halves

	// Colour of the status tag: what kind of note it is.
	FLinearColor TagColor(const FUISimCar& Car)
	{
		if (Car.bStalled)
		{
			return FLinearColor(0.8f, 0.1f, 0.1f);
		}
		if (Car.bUnlapping)
		{
			return FLinearColor(0.35f, 0.35f, 0.42f);
		}
		if (Car.bBlueFlag)
		{
			return FLinearColor(0.1f, 0.35f, 0.9f);
		}
		if (Car.bOvertakeActive && !Car.bRetired)
		{
			return FLinearColor(0.1f, 0.6f, 0.3f);
		}
		const bool bPenalty = Car.PendingPenalty == EUISimPenalty::DriveThrough || Car.PendingPenalty == EUISimPenalty::StopAndGo
			|| Car.TimePenalty > 0.0 || Car.bBlackWhiteFlag;
		return bPenalty ? FLinearColor(0.85f, 0.4f, 0.05f) : FLinearColor(0.35f, 0.35f, 0.42f);
	}

	// Same colours as the cars (UIRaceDirector.cpp): one hue per team.
	FLinearColor TeamColor(const TArray<FUISimEntry>& Entries, int32 EntryIndex)
	{
		TArray<FString> Teams;
		for (const FUISimEntry& Entry : Entries)
		{
			Teams.AddUnique(Entry.Team);
		}
		const float Hue = 360.0f * Teams.IndexOfByKey(Entries[EntryIndex].Team) / FMath::Max(1, Teams.Num());
		return FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue / 360.0f * 255.0f), 200, 230);
	}

	FString StateText(const FUISimSnapshot& Snapshot)
	{
		FString State;
		switch (Snapshot.State)
		{
		case EUISimRaceState::VirtualSafetyCar: State = TEXT("VSC"); break;
		case EUISimRaceState::SafetyCar: State = Snapshot.bSafetyCarInThisLap ? TEXT("SC IN THIS LAP") : TEXT("SAFETY CAR"); break;
		case EUISimRaceState::RedFlag: State = TEXT("RED FLAG"); break;
		case EUISimRaceState::Green: break;
		}
		if (Snapshot.bFinished)
		{
			State = TEXT("FINISHED");
		}
		else if (Snapshot.bChequered)
		{
			State = TEXT("CHEQUERED FLAG");
		}
		if (Snapshot.bLappedCarsReleased && Snapshot.State == EUISimRaceState::SafetyCar)
		{
			State += TEXT("  UEBERRUNDETE VORBEI");
		}
		if (Snapshot.bPitEntryClosed)
		{
			State += TEXT("  BOXENGASSE ZU");
		}
		if (Snapshot.bFormationLap)
		{
			State = TEXT("EINFUEHRUNGSRUNDE"); // not a Safety Car period
		}
		return State;
	}

	// What stands in the gap column for the car in row Index.
	FString GapText(const FUISimSnapshot& Snapshot, int32 Index, bool bInterval, bool& bOutInPit)
	{
		const FUISimCar& Car = Snapshot.Cars[Index];
		bOutInPit = false;
		if (Car.bDisqualified)
		{
			return TEXT("DSQ");
		}
		if (Car.bRetired)
		{
			return TEXT("OUT");
		}
		if (Car.bInPitLane)
		{
			bOutInPit = true; // only while the car is in the pit lane
			return TEXT("IN PIT");
		}
		if (Index == 0)
		{
			return bInterval ? TEXT("Interval") : TEXT("Leader");
		}
		if (!bInterval)
		{
			return Car.LapsDown > 0 ? FString::Printf(TEXT("+%d L"), Car.LapsDown) : FString::Printf(TEXT("+%.3f"), Car.GapToLeader);
		}
		// Interval to the car ahead in the classification.
		const FUISimCar& Ahead = Snapshot.Cars[Index - 1];
		if (Ahead.bRetired || Ahead.bDisqualified)
		{
			return TEXT("-");
		}
		if (Car.LapsDown != Ahead.LapsDown)
		{
			return FString::Printf(TEXT("+%d L"), Car.LapsDown - Ahead.LapsDown);
		}
		return FString::Printf(TEXT("+%.3f"), FMath::Max(0.0, Car.GapToLeader - Ahead.GapToLeader));
	}

	// Status tag behind the tyre: what race control needs to see about this car.
	FString TagText(const FUISimCar& Car)
	{
		FString Tag;
		if (Car.bStalled)
		{
			Tag = TEXT("STEHT"); // immobilised on the grid
		}
		else if (Car.bUnlapping)
		{
			Tag = TEXT("UNLAP"); // lapped car passing the queue under the SC
		}
		else if (Car.bBlueFlag)
		{
			Tag = TEXT("BLUE");
		}
		else if (Car.bOvertakeActive && !Car.bRetired)
		{
			Tag = TEXT("OVT");
		}
		else if (Car.PendingPenalty == EUISimPenalty::DriveThrough)
		{
			Tag = TEXT("DT");
		}
		else if (Car.PendingPenalty == EUISimPenalty::StopAndGo)
		{
			Tag = TEXT("SG");
		}
		if (Car.TimePenalty > 0.0)
		{
			Tag += FString::Printf(TEXT("+%.0fs"), Car.TimePenalty);
		}
		if (Car.bBlackWhiteFlag)
		{
			Tag += FString::Printf(TEXT(" SW%d"), Car.TrackLimitStrikes); // black-and-white flag, track limits
		}
		if (Car.bFinished)
		{
			Tag += TEXT(" F");
		}
		return Tag.TrimStart();
	}
}

TSharedRef<SWidget> UUITimingTowerWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		UCanvasPanelSlot* ColumnSlot = Canvas->AddChildToCanvas(Column);
		ColumnSlot->SetAutoSize(true);
		ColumnSlot->SetPosition(UIStyle::TimingCard.Min + FVector2D(24.0, 14.0));

		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(TowerWidth);
		Box->SetHeightOverride(HeaderHeight + RowHeight * 20);
		TowerArea = WidgetTree->ConstructWidget<UBorder>();
		TowerArea->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.05f, 0.88f)); // visible, so it takes the clicks
		Box->SetContent(TowerArea);
		Column->AddChildToVerticalBox(Box)->SetHorizontalAlignment(HAlign_Left); // not as wide as the footer

		Footer = WidgetTree->ConstructWidget<UTextBlock>();
		Footer->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 10));
		Footer->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)));
		Footer->SetShadowOffset(FVector2D(1.0, 1.0));
		Footer->SetVisibility(ESlateVisibility::HitTestInvisible);
		Column->AddChildToVerticalBox(Footer);
	}
	return Super::RebuildWidget();
}

void UUITimingTowerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted() || !Footer)
	{
		return;
	}
	const FUISimSnapshot& Snapshot = Race->Session().Snapshot();
	const int32 Sector = Race->SelectedSector();
	const TCHAR* FlagNames[] = {TEXT("gruen"), TEXT("GELB"), TEXT("DOPPELT GELB")};
	const TCHAR* FlagName = Snapshot.SectorFlags.IsValidIndex(Sector)
		? FlagNames[static_cast<uint8>(Snapshot.SectorFlags[Sector])] : TEXT("-");
	Footer->SetText(FText::FromString(FString::Printf(
		TEXT("Rennzeit %s   Zeit x%g\n")
		TEXT("Sektor %d/%d: %s [Q/E, Y]\n")
		TEXT("Verstoesse: %d   Kamera: %s [1-6]"),
		*AUIRaceDirector::FormatRaceTime(Snapshot.Time), Race->TimeScale(), Sector + 1, Snapshot.SectorFlags.Num(), FlagName,
		Race->Session().FlagViolations(), *Race->ActiveCameraName())));
}

int32 UUITimingTowerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted() || !TowerArea || TowerArea->GetCachedGeometry().GetLocalSize().X <= 0.0)
	{
		return Layer;
	}
	const FGeometry& Area = TowerArea->GetCachedGeometry();
	const FUISimSnapshot& Snapshot = Race->Session().Snapshot();
	const TArray<FUISimEntry>& Entries = Race->Session().Entries();
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo LapFont = UIStyle::Font(TEXT("Bold"), 20);
	const FSlateFontInfo SmallFont = UIStyle::Font(TEXT("Regular"), 8);
	const FSlateFontInfo CodeFont = UIStyle::Font(TEXT("Bold"), 13);
	const FSlateFontInfo GapFont = FCoreStyle::GetDefaultFontStyle("Mono", 11);
	const FSlateFontInfo TagFont = FCoreStyle::GetDefaultFontStyle("Mono", 9);
	++Layer;
	auto Box = [&](const FVector2D& At, const FVector2D& Size, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Area.ToPaintGeometry(Size, FSlateLayoutTransform(1.0f, At)), White,
			ESlateDrawEffect::None, Color);
	};
	auto Text = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, Area.ToPaintGeometry(FVector2D(TowerWidth, RowHeight),
			FSlateLayoutTransform(1.0f, At)), String, Font, ESlateDrawEffect::None, Color);
	};
	auto TextRight = [&](double Right, double Top, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		Text(FVector2D(Right - Measure->Measure(String, Font).X, Top), String, Font, Color);
	};

	// Header: lap counter, state, and the mode of the gap column.
	static const FLinearColor CompoundColors[] = {FLinearColor(1.0f, 0.2f, 0.2f), FLinearColor(1.0f, 0.85f, 0.1f),
		FLinearColor(0.95f, 0.95f, 0.95f), FLinearColor(0.2f, 0.85f, 0.3f), FLinearColor(0.3f, 0.55f, 1.0f)};
	static const TCHAR* const CompoundLetters[] = {TEXT("S"), TEXT("M"), TEXT("H"), TEXT("I"), TEXT("W")};
	// Rounded shapes: plate for tags and the switch, disc for the tyre ring.
	static const FSlateRoundedBoxBrush Plate(FLinearColor::White, 4.0f);
	static const FSlateRoundedBoxBrush Disc(FLinearColor::White, 9.0f);
	auto Shape = [&](const FVector2D& At, const FVector2D& Size, const FSlateBrush& Brush, const FLinearColor& Tint)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Area.ToPaintGeometry(Size, FSlateLayoutTransform(1.0f, At)), &Brush,
			ESlateDrawEffect::None, Tint);
	};
	auto TextCentred = [&](double Centre, double Top, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		Text(FVector2D(Centre - Measure->Measure(String, Font).X / 2.0, Top), String, Font, Color);
	};
	const FLinearColor Dim(0.62f, 0.64f, 0.72f);
	const FLinearColor Dark(0.03f, 0.03f, 0.06f);

	// Lap counter, centred as one line.
	const FSlateFontInfo LabelFont = UIStyle::Font(TEXT("Regular"), 12);
	const FString Lap = FString::FromInt(Snapshot.LeaderLap);
	const FString Total = FString::Printf(TEXT("/ %d"), Snapshot.TotalLaps);
	const double LapLabelWidth = Measure->Measure(TEXT("LAP"), LabelFont).X;
	const double LapWidth = Measure->Measure(Lap, LapFont).X;
	const double LapLeft = (TowerWidth - LapLabelWidth - LapWidth - Measure->Measure(Total, LabelFont).X - 14.0) / 2.0;
	Box(FVector2D(0.0, 0.0), FVector2D(TowerWidth, 36.0), FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	Text(FVector2D(LapLeft, 12.0), TEXT("LAP"), LabelFont, Dim);
	Text(FVector2D(LapLeft + LapLabelWidth + 7.0, 3.0), Lap, LapFont, FLinearColor::White);
	Text(FVector2D(LapLeft + LapLabelWidth + LapWidth + 14.0, 12.0), Total, LabelFont, Dim);

	// State band in the colour of the flag.
	FString State = StateText(Snapshot);
	FLinearColor Band(0.08f, 0.5f, 0.22f);
	FLinearColor BandInk = FLinearColor::White;
	if (Snapshot.State == EUISimRaceState::RedFlag)
	{
		Band = FLinearColor(0.8f, 0.06f, 0.08f);
	}
	else if (Snapshot.bFinished || Snapshot.bChequered)
	{
		Band = FLinearColor(0.9f, 0.9f, 0.92f);
		BandInk = Dark;
	}
	else if (!State.IsEmpty())
	{
		Band = FLinearColor(1.0f, 0.8f, 0.1f);
		BandInk = Dark;
	}
	else
	{
		State = TEXT("GRUEN");
	}
	Box(FVector2D(0.0, 36.0), FVector2D(TowerWidth, 18.0), Band);
	TextCentred(TowerWidth / 2.0, 38.0, State, SmallFont, BandInk);

	// Mode switch above the gap column: the active half is bright.
	const FVector2D Half(ModeLabel.GetSize().X / 2.0, ModeLabel.GetSize().Y);
	Shape(ModeLabel.Min, ModeLabel.GetSize(), Plate, FLinearColor(1.0f, 1.0f, 1.0f, 0.12f));
	Shape(ModeLabel.Min + FVector2D(bInterval ? 0.0 : Half.X, 0.0), Half, Plate, FLinearColor(0.92f, 0.92f, 0.95f));
	TextCentred(ModeLabel.Min.X + Half.X * 0.5, ModeLabel.Min.Y + 2.0, TEXT("INTERVALL"), SmallFont, bInterval ? Dark : Dim);
	TextCentred(ModeLabel.Min.X + Half.X * 1.5, ModeLabel.Min.Y + 2.0, TEXT("ABSTAND"), SmallFont, bInterval ? Dim : Dark);
	Text(FVector2D(TyreLeft, ModeLabel.Min.Y + 2.0), TEXT("REIFEN"), SmallFont, Dim);

	const FSlateFontInfo TyreFont = UIStyle::Font(TEXT("Bold"), 9);
	for (int32 Index = 0; Index < Snapshot.Cars.Num() && Index < MaxRows; ++Index)
	{
		const FUISimCar& Car = Snapshot.Cars[Index];
		const double Top = HeaderHeight + Index * RowHeight;
		const bool bSelected = Car.EntryIndex == Race->FollowedCar(); // the car the TV camera follows
		const bool bOut = Car.bRetired || Car.bDisqualified;
		const FLinearColor Ink = bOut ? Dim : FLinearColor::White;
		if (bSelected)
		{
			Box(FVector2D(0.0, Top), FVector2D(TowerWidth, RowHeight - 1.0), FLinearColor(0.16f, 0.22f, 0.38f));
		}
		else if (Index % 2 == 1)
		{
			Box(FVector2D(0.0, Top), FVector2D(TowerWidth, RowHeight - 1.0), FLinearColor(1.0f, 1.0f, 1.0f, 0.04f));
		}
		Box(FVector2D(0.0, Top), FVector2D(PositionWidth, RowHeight - 1.0),
			Index == 0 ? FLinearColor(0.85f, 0.05f, 0.08f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
		TextCentred(PositionWidth / 2.0, Top + 3.0, FString::FromInt(Car.Position), CodeFont, Ink);
		Box(FVector2D(BarLeft, Top), FVector2D(5.0, RowHeight - 1.0), TeamColor(Entries, Car.EntryIndex));
		Text(FVector2D(CodeLeft, Top + 3.0), Entries[Car.EntryIndex].Code, CodeFont, Ink);
		bool bInPit = false;
		const FString Gap = GapText(Snapshot, Index, bInterval, bInPit);
		if (bInPit)
		{
			const double Width = Measure->Measure(Gap, GapFont).X + 12.0;
			Shape(FVector2D(GapRight - Width + 4.0, Top + 4.0), FVector2D(Width, RowHeight - 9.0), Plate, FLinearColor(0.1f, 0.65f, 0.3f));
		}
		TextRight(GapRight - (bInPit ? 2.0 : 0.0), Top + 5.0, Gap, GapFont, bInPit || !bOut ? FLinearColor::White : Dim);
		const int32 Compound = FMath::Clamp(Car.Compound, 0, 4);
		const FVector2D LetterSize = Measure->Measure(CompoundLetters[Compound], TyreFont);
		Shape(FVector2D(TyreLeft, Top + 3.5), FVector2D(TyreSize, TyreSize), Disc, CompoundColors[Compound]);
		Shape(FVector2D(TyreLeft + 2.5, Top + 6.0), FVector2D(TyreSize - 5.0, TyreSize - 5.0), Disc, FLinearColor(0.05f, 0.05f, 0.09f));
		Text(FVector2D(TyreLeft + (TyreSize - LetterSize.X) / 2.0, Top + 3.5 + (TyreSize - LetterSize.Y) / 2.0), CompoundLetters[Compound],
			TyreFont, FLinearColor::White);
		Text(FVector2D(TyreLeft + TyreSize + 4.0, Top + 7.0), FString::FromInt(Car.TyreAge), TagFont, Dim);
		const FString Tag = TagText(Car);
		if (!Tag.IsEmpty())
		{
			const double Width = FMath::Min(Measure->Measure(Tag, TagFont).X + 10.0, TowerWidth - TagLeft - 3.0);
			Shape(FVector2D(TagLeft, Top + 4.5), FVector2D(Width, RowHeight - 10.0), Plate, TagColor(Car));
			Text(FVector2D(TagLeft + 5.0, Top + 6.5), Tag, TagFont, FLinearColor::White);
		}
		if (bSelected)
		{
			Box(FVector2D(0.0, Top), FVector2D(TowerWidth, 1.5), FLinearColor::White);
			Box(FVector2D(0.0, Top + RowHeight - 2.5), FVector2D(TowerWidth, 1.5), FLinearColor::White);
		}
	}
	Layer += 2;

	if (FVector2D(Area.GetAbsolutePosition()) != LoggedAt)
	{
		// Viewport pixels for -UISimScript checks (click x y): the mode label and the first two rows.
		LoggedAt = FVector2D(Area.GetAbsolutePosition());
		const UWorld* World = GetWorld();
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
		if (ViewportWidget.IsValid())
		{
			const FVector2D Origin = FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
			const FVector2D Mode = FVector2D(Area.LocalToAbsolute(ModeLabel.GetCenter())) - Origin;
			const FVector2D Row = FVector2D(Area.LocalToAbsolute(FVector2D(CodeLeft + 20.0, HeaderHeight + RowHeight * 1.5))) - Origin;
			UE_LOG(LogUISim, Display, TEXT("Tower: mode label at %.0f %.0f, row 2 at %.0f %.0f, row height %.0f"), Mode.X, Mode.Y, Row.X, Row.Y,
				RowHeight);
		}
	}
	return Layer;
}

FReply UUITimingTowerWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (!Race || !TowerArea || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !Race->Session().IsStarted())
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Local = FVector2D(TowerArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	if (ModeLabel.IsInside(Local))
	{
		bInterval = !bInterval;
		UE_LOG(LogUISim, Display, TEXT("Tower: gap column shows %s"), bInterval ? TEXT("the interval to the car ahead") : TEXT("the gap to the leader"));
		return FReply::Handled();
	}
	// A row: the TV camera follows that car (the leader's row: back to following the leader).
	const int32 Row = FMath::FloorToInt32((Local.Y - HeaderHeight) / RowHeight);
	const TArray<FUISimCar>& Cars = Race->Session().Snapshot().Cars;
	if (Local.X >= 0.0 && Local.X <= TowerWidth && Local.Y >= HeaderHeight && Cars.IsValidIndex(Row))
	{
		FUICommand Command;
		Command.Type = EUICommand::FollowCar;
		Command.Index = Row == 0 ? -1 : Cars[Row].EntryIndex;
		Race->Execute(Command);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
