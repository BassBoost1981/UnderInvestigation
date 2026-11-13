#include "UITimingTowerWidget.h"

#include "UIRaceDirector.h"
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
	// Measures of the design, relative to the card (744 x 680).
	constexpr int32 MaxRows = 20;
	constexpr double HeaderTop = 46.0; // column labels
	constexpr double RowsTop = 71.0;
	constexpr double RowHeight = 29.0;
	// Columns: left edges, or right edges for the right-aligned ones.
	constexpr double PositionLeft = 11.0, PositionWidth = 26.0;
	constexpr double BarLeft = 43.0;
	constexpr double CodeLeft = 54.0;
	constexpr double GapRight = 206.0;
	constexpr double IntervalRight = 314.0;
	constexpr double LastRight = 433.0;
	constexpr double BestRight = 551.0;
	constexpr double SectorLeft = 557.0;
	constexpr double TyreLeft = 615.0;
	constexpr double PitLeft = 669.0;
	constexpr double TokenLeft = 699.0;

	const FLinearColor Purple = UIStyle::Rgb(199, 125, 255);     // session best, text
	const FLinearColor PurpleBar = UIStyle::Rgb(185, 110, 245);  // session best, sector bar
	const FLinearColor PurpleChip = UIStyle::Rgb(123, 47, 190);  // fastest lap chip
	const FLinearColor SectorEmpty = UIStyle::Rgb(44, 45, 50);

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

	FString LapText(double Seconds)
	{
		if (Seconds <= 0.0)
		{
			return TEXT("—");
		}
		const int32 Minutes = FMath::FloorToInt32(Seconds / 60.0);
		return FString::Printf(TEXT("%d:%06.3f"), Minutes, Seconds - Minutes * 60.0);
	}

	// Gap column: to the leader. "IN PIT" only while the car is in the pit lane.
	FString GapText(const FUISimSnapshot& Snapshot, int32 Index)
	{
		const FUISimCar& Car = Snapshot.Cars[Index];
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
			return TEXT("IN PIT");
		}
		if (Index == 0)
		{
			return TEXT("LEADER");
		}
		return Car.LapsDown > 0 ? FString::Printf(TEXT("+%d LAP"), Car.LapsDown) : FString::Printf(TEXT("+%.3f"), Car.GapToLeader);
	}

	// Interval column: to the car ahead in the classification; empty where the design leaves it empty.
	FString IntervalText(const FUISimSnapshot& Snapshot, int32 Index)
	{
		const FUISimCar& Car = Snapshot.Cars[Index];
		if (Index == 0 || Car.bRetired || Car.bDisqualified || Car.bInPitLane)
		{
			return FString();
		}
		const FUISimCar& Ahead = Snapshot.Cars[Index - 1];
		if (Ahead.bRetired || Ahead.bDisqualified)
		{
			return FString();
		}
		if (Car.LapsDown != Ahead.LapsDown)
		{
			return FString::Printf(TEXT("+%d LAP"), Car.LapsDown - Ahead.LapsDown);
		}
		return FString::Printf(TEXT("+%.3f"), FMath::Max(0.0, Car.GapToLeader - Ahead.GapToLeader));
	}

	// Last column of the design (PEN): the penalty, or what race control must see about this car.
	FString TokenText(const FUISimCar& Car, FLinearColor& OutColor)
	{
		OutColor = UIStyle::Red;
		if (Car.bStalled)
		{
			return TEXT("STALL"); // immobilised on the grid
		}
		if (Car.PendingPenalty == EUISimPenalty::DriveThrough)
		{
			return TEXT("DT");
		}
		if (Car.PendingPenalty == EUISimPenalty::StopAndGo)
		{
			return TEXT("S&G");
		}
		if (Car.TimePenalty > 0.0)
		{
			return FString::Printf(TEXT("%.0fs"), Car.TimePenalty);
		}
		if (Car.bUnlapping)
		{
			OutColor = UIStyle::Mute;
			return TEXT("UNLAP"); // lapped car passing the queue under the SC
		}
		if (Car.bBlueFlag)
		{
			OutColor = UIStyle::Rgb(80, 150, 255);
			return TEXT("BLUE");
		}
		if (Car.bBlackWhiteFlag)
		{
			OutColor = UIStyle::Text;
			return FString::Printf(TEXT("B/W%d"), Car.TrackLimitStrikes); // black-and-white flag, track limits
		}
		if (Car.bOvertakeActive && !Car.bRetired)
		{
			OutColor = UIStyle::Green;
			return TEXT("OVT"); // Overtake mode usable this lap (B7.2)
		}
		return FString();
	}
}

TSharedRef<SWidget> UUITimingTowerWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;

		TableArea = WidgetTree->ConstructWidget<UBorder>();
		TableArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f)); // visible, so it takes the clicks; the desk draws the card
		UCanvasPanelSlot* AreaSlot = Canvas->AddChildToCanvas(TableArea);
		AreaSlot->SetPosition(UIStyle::TimingCard.Min);
		AreaSlot->SetSize(UIStyle::TimingCard.GetSize());
	}
	return Super::RebuildWidget();
}

int32 UUITimingTowerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted() || !TableArea || TableArea->GetCachedGeometry().GetLocalSize().X <= 0.0)
	{
		return Layer;
	}
	static const FSlateRoundedBoxBrush Round6(FLinearColor::White, 6.0f);
	static const FSlateRoundedBoxBrush Round2(FLinearColor::White, 2.0f);
	static const FSlateRoundedBoxBrush Circle(FLinearColor::White, 10.0f);
	const FGeometry& Area = TableArea->GetCachedGeometry();
	const double CardWidth = UIStyle::TimingCard.GetSize().X;
	const FUISimSnapshot& Snapshot = Race->Session().Snapshot();
	const TArray<FUISimEntry>& Entries = Race->Session().Entries();
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo LabelFont = UIStyle::Font(TEXT("Regular"), 7);
	const FSlateFontInfo BodyFont = UIStyle::Font(TEXT("Regular"), 9);
	const FSlateFontInfo BoldFont = UIStyle::Font(TEXT("Bold"), 9);
	const FSlateFontInfo CodeFont = UIStyle::Font(TEXT("Bold"), 10);
	const FSlateFontInfo TyreFont = UIStyle::Font(TEXT("Bold"), 7);
	++Layer;
	auto Box = [&](const FVector2D& At, const FVector2D& Size, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Area.ToPaintGeometry(Size, FSlateLayoutTransform(1.0f, At)), Brush,
			ESlateDrawEffect::None, Color);
	};
	auto Text = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, Area.ToPaintGeometry(FVector2D(CardWidth, RowHeight),
			FSlateLayoutTransform(1.0f, At)), String, Font, ESlateDrawEffect::None, Color);
	};
	auto TextRight = [&](double Right, double Top, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		Text(FVector2D(Right - Measure->Measure(String, Font).X, Top), String, Font, Color);
	};
	auto TextCentred = [&](double Centre, double Top, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		Text(FVector2D(Centre - Measure->Measure(String, Font).X / 2.0, Top), String, Font, Color);
	};

	// Bests of the race: fastest lap and its holder, fastest time of each sector.
	double FastestLap = 0.0;
	int32 FastestEntry = -1;
	double SessionSector[3] = {0.0, 0.0, 0.0};
	for (const FUISimCar& Car : Snapshot.Cars)
	{
		if (Car.BestLapTime > 0.0 && (FastestLap == 0.0 || Car.BestLapTime < FastestLap))
		{
			FastestLap = Car.BestLapTime;
			FastestEntry = Car.EntryIndex;
		}
		for (int32 Sector = 0; Sector < 3; ++Sector)
		{
			if (Car.BestSectorTimes[Sector] > 0.0 && (SessionSector[Sector] == 0.0 || Car.BestSectorTimes[Sector] < SessionSector[Sector]))
			{
				SessionSector[Sector] = Car.BestSectorTimes[Sector];
			}
		}
	}

	// Title and the fastest lap chip.
	Text(FVector2D(17.0, 15.0), TEXT("Live timing"), UIStyle::Font(TEXT("Bold"), 10), UIStyle::Text);
	const FSlateFontInfo ChipFont = UIStyle::Font(TEXT("Bold"), 8);
	const FString Fastest = FastestEntry >= 0 ? Entries[FastestEntry].Code + TEXT(" ") + LapText(FastestLap) : FString(TEXT("—"));
	const double ChipWidth = Measure->Measure(Fastest, ChipFont).X + 18.0;
	const double ChipLeft = CardWidth - 17.0 - ChipWidth;
	Box(FVector2D(ChipLeft, 15.0), FVector2D(ChipWidth, 20.0), &Circle, PurpleChip);
	Text(FVector2D(ChipLeft + 9.0, 18.0), Fastest, ChipFont, FLinearColor::White);
	TextRight(ChipLeft - 6.0, 19.0, TEXT("FASTEST"), UIStyle::Font(TEXT("Regular"), 8), UIStyle::Mute);

	// Column labels with the line below.
	const double LabelTop = HeaderTop + 6.0;
	Text(FVector2D(PositionLeft, LabelTop), TEXT("P"), LabelFont, UIStyle::Mute);
	Text(FVector2D(CodeLeft, LabelTop), TEXT("DRV"), LabelFont, UIStyle::Mute);
	TextRight(GapRight, LabelTop, TEXT("GAP"), LabelFont, UIStyle::Mute);
	TextRight(IntervalRight, LabelTop, TEXT("INT"), LabelFont, UIStyle::Mute);
	TextRight(LastRight, LabelTop, TEXT("LAST"), LabelFont, UIStyle::Mute);
	TextRight(BestRight, LabelTop, TEXT("BEST"), LabelFont, UIStyle::Mute);
	Text(FVector2D(SectorLeft, LabelTop), TEXT("SECT"), LabelFont, UIStyle::Mute);
	Text(FVector2D(TyreLeft, LabelTop), TEXT("TYRE"), LabelFont, UIStyle::Mute);
	Text(FVector2D(PitLeft, LabelTop), TEXT("PIT"), LabelFont, UIStyle::Mute);
	Text(FVector2D(TokenLeft, LabelTop), TEXT("PEN"), LabelFont, UIStyle::Mute);
	Box(FVector2D(1.0, RowsTop - 1.0), FVector2D(CardWidth - 2.0, 1.0), White, FLinearColor(1.0f, 1.0f, 1.0f, 0.07f));

	static const TCHAR* const CompoundLetters[] = {TEXT("S"), TEXT("M"), TEXT("H"), TEXT("I"), TEXT("W")};
	static const FLinearColor CompoundColors[] = {UIStyle::Red, UIStyle::Yellow, UIStyle::Text, UIStyle::Green, UIStyle::Rgb(80, 150, 255)};
	for (int32 Index = 0; Index < Snapshot.Cars.Num() && Index < MaxRows; ++Index)
	{
		const FUISimCar& Car = Snapshot.Cars[Index];
		const double Top = RowsTop + Index * RowHeight;
		const bool bOut = Car.bRetired || Car.bDisqualified;
		auto Faded = [&](FLinearColor Color) // cars that are out stand back, as in the design
		{
			Color.A *= bOut ? 0.45f : 1.0f;
			return Color;
		};
		if (Car.EntryIndex == Race->FollowedCar()) // the car the TV camera follows
		{
			Box(FVector2D(1.0, Top), FVector2D(CardWidth - 2.0, RowHeight - 1.0), White, FLinearColor(1.0f, 1.0f, 1.0f, 0.09f));
		}
		Box(FVector2D(1.0, Top + RowHeight - 1.0), FVector2D(CardWidth - 2.0, 1.0), White, FLinearColor(1.0f, 1.0f, 1.0f, 0.05f));

		Box(FVector2D(PositionLeft, Top + 4.0), FVector2D(PositionWidth, 21.0), &Round6, Faded(UIStyle::Text));
		TextCentred(PositionLeft + PositionWidth / 2.0, Top + 7.0, FString::FromInt(Car.Position), BoldFont, UIStyle::Ink);
		Box(FVector2D(BarLeft, Top + 5.0), FVector2D(5.0, 19.0), &Round2, Faded(TeamColor(Entries, Car.EntryIndex)));
		Text(FVector2D(CodeLeft, Top + 6.0), Entries[Car.EntryIndex].Code, CodeFont, Faded(UIStyle::Text));

		const FString Gap = GapText(Snapshot, Index);
		TextRight(GapRight, Top + 7.0, Gap, Car.bInPitLane && !bOut ? BoldFont : BodyFont,
			Faded(Car.bInPitLane && !bOut ? UIStyle::Green : UIStyle::Text));
		// Within a second of the car ahead in the classification and Overtake available: green, as the design marks it.
		const bool bClose = Index > 0 && Car.bOvertakeActive && !bOut && Car.LapsDown == Snapshot.Cars[Index - 1].LapsDown
			&& Car.GapToLeader - Snapshot.Cars[Index - 1].GapToLeader < 1.0;
		TextRight(IntervalRight, Top + 7.0, IntervalText(Snapshot, Index), BodyFont, Faded(bClose ? UIStyle::Green : UIStyle::Text));

		const bool bPersonalBest = Car.LastLapTime > 0.0 && Car.LastLapTime <= Car.BestLapTime;
		const bool bSessionBest = bPersonalBest && Car.LastLapTime <= FastestLap;
		TextRight(LastRight, Top + 7.0, LapText(Car.LastLapTime), BodyFont,
			Faded(bSessionBest ? Purple : bPersonalBest ? UIStyle::Green : UIStyle::Text));
		TextRight(BestRight, Top + 7.0, LapText(Car.BestLapTime), BodyFont, Faded(Car.EntryIndex == FastestEntry ? Purple : UIStyle::Mute));

		for (int32 Sector = 0; Sector < 3; ++Sector)
		{
			// As in the design: the bars of a new lap fill again sector by sector; before the first one the last lap stays.
			const double Time = Car.SectorsDone > 0 && Sector >= Car.SectorsDone ? 0.0 : Car.SectorTimes[Sector];
			const FLinearColor Color = Time <= 0.0 ? SectorEmpty : Time <= SessionSector[Sector] ? PurpleBar
				: Time <= Car.BestSectorTimes[Sector] ? UIStyle::Green : UIStyle::Yellow;
			Box(FVector2D(SectorLeft + Sector * 17.0, Top + 10.0), FVector2D(15.0, 8.0), &Round2, Faded(Color));
		}

		const int32 Compound = FMath::Clamp(Car.Compound, 0, 4);
		Box(FVector2D(TyreLeft, Top + 6.0), FVector2D(16.0, 16.0), &Circle, Faded(CompoundColors[Compound]));
		Box(FVector2D(TyreLeft + 2.0, Top + 8.0), FVector2D(12.0, 12.0), &Circle, UIStyle::Card);
		const FVector2D LetterSize = Measure->Measure(CompoundLetters[Compound], TyreFont);
		Text(FVector2D(TyreLeft + (16.0 - LetterSize.X) / 2.0, Top + 6.0 + (16.0 - LetterSize.Y) / 2.0), CompoundLetters[Compound], TyreFont,
			Faded(UIStyle::Text));
		Text(FVector2D(TyreLeft + 21.0, Top + 7.0), FString::FromInt(Car.TyreAge), BodyFont, Faded(UIStyle::Mute));
		Text(FVector2D(PitLeft, Top + 7.0), FString::FromInt(Car.PitStops), BodyFont, Faded(UIStyle::Mute));
		FLinearColor TokenColor;
		const FString Token = TokenText(Car, TokenColor);
		if (!Token.IsEmpty())
		{
			Text(FVector2D(TokenLeft, Top + 7.0), Token, BoldFont, Faded(TokenColor));
		}
	}

	// Legend of the lap and sector colours.
	const double LegendTop = RowsTop + MaxRows * RowHeight + 10.0;
	double LegendLeft = 13.0;
	const TPair<const TCHAR*, FLinearColor> Legend[] = {{TEXT("SESSION BEST"), PurpleBar}, {TEXT("PERSONAL BEST"), UIStyle::Green},
		{TEXT("NO IMPROVEMENT"), UIStyle::Yellow}};
	for (const TPair<const TCHAR*, FLinearColor>& Entry : Legend)
	{
		Box(FVector2D(LegendLeft, LegendTop + 2.0), FVector2D(10.0, 8.0), &Round2, Entry.Value);
		Text(FVector2D(LegendLeft + 15.0, LegendTop), Entry.Key, LabelFont, UIStyle::Mute);
		LegendLeft += 15.0 + Measure->Measure(Entry.Key, LabelFont).X + 12.0;
	}
	Layer += 2;

	if (FVector2D(Area.GetAbsolutePosition()) != LoggedAt)
	{
		// Viewport pixels for -UISimScript checks (click x y): the second row.
		LoggedAt = FVector2D(Area.GetAbsolutePosition());
		const UWorld* World = GetWorld();
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
		if (ViewportWidget.IsValid())
		{
			const FVector2D Origin = FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
			const FVector2D Row = FVector2D(Area.LocalToAbsolute(FVector2D(CodeLeft + 20.0, RowsTop + RowHeight * 1.5))) - Origin;
			const FVector2D Next = FVector2D(Area.LocalToAbsolute(FVector2D(CodeLeft + 20.0, RowsTop + RowHeight * 2.5))) - Origin;
			UE_LOG(LogUISim, Display, TEXT("Live timing: row 2 at %.0f %.0f, row 3 at %.0f %.0f"), Row.X, Row.Y, Next.X, Next.Y);
		}
	}
	return Layer;
}

FReply UUITimingTowerWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (!Race || !TableArea || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !Race->Session().IsStarted())
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	// A row: the TV camera follows that car (the leader's row: back to following the leader).
	const FVector2D Local = FVector2D(TableArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	const int32 Row = FMath::FloorToInt32((Local.Y - RowsTop) / RowHeight);
	const TArray<FUISimCar>& Cars = Race->Session().Snapshot().Cars;
	if (Local.Y >= RowsTop && Row < MaxRows && Cars.IsValidIndex(Row))
	{
		FUICommand Command;
		Command.Type = EUICommand::FollowCar;
		Command.Index = Row == 0 ? -1 : Cars[Row].EntryIndex;
		Race->Execute(Command);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
