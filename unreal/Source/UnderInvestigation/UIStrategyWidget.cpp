#include "UIStrategyWidget.h"

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
	// Layout in card coordinates (744 x 276): two columns of ten rows.
	constexpr double ColumnLeft[] = {12.0, 384.0};
	constexpr double ColumnWidth = 348.0;
	constexpr double BarLeft = 66.0, BarWidth = 214.0, ChipLeft = 288.0, ChipWidth = 60.0;
	constexpr double AxisTop = 40.0, RowTop = 60.0, RowStep = 21.0, BarHeight = 14.0;
	constexpr int32 RowsPerColumn = 10;

	const TCHAR* const CompoundLetter[] = {TEXT("S"), TEXT("M"), TEXT("H"), TEXT("I"), TEXT("W")};
	const TCHAR* const CompoundName[] = {TEXT("SOFT"), TEXT("MEDIUM"), TEXT("HARD"), TEXT("INTER"), TEXT("WET")};

	FLinearColor CompoundColor(int32 Compound)
	{
		switch (Compound)
		{
		case 0: return UIStyle::Red;
		case 1: return UIStyle::Yellow;
		case 3: return UIStyle::Green;
		case 4: return UIStyle::Rgb(80, 150, 255);
		default: return UIStyle::Text;
		}
	}
}

TSharedRef<SWidget> UUIStrategyWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		CardArea = WidgetTree->ConstructWidget<UBorder>();
		CardArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f)); // visible, so it takes the clicks
		UCanvasPanelSlot* AreaSlot = Canvas->AddChildToCanvas(CardArea);
		AreaSlot->SetPosition(UIStyle::LowerLeftCard.Min);
		AreaSlot->SetSize(UIStyle::LowerLeftCard.GetSize());
	}
	return Super::RebuildWidget();
}

FBox2D UUIStrategyWidget::RowRect(int32 Row) const
{
	const double Left = ColumnLeft[Row / RowsPerColumn];
	const double Top = RowTop + (Row % RowsPerColumn) * RowStep;
	return FBox2D(FVector2D(Left - 4.0, Top), FVector2D(Left + ColumnWidth, Top + RowStep - 1.0));
}

int32 UUIStrategyWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted() || !CardArea || CardArea->GetCachedGeometry().GetLocalSize().X <= 0.0)
	{
		return Layer;
	}
	static const FSlateRoundedBoxBrush Round7(FLinearColor::White, 7.0f);
	static const FSlateRoundedBoxBrush Round4(FLinearColor::White, 4.0f);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FGeometry& Card = CardArea->GetCachedGeometry();
	auto Box = [&](const FBox2D& Rect, const FSlateBrush* Brush, const FLinearColor& Color, int32 BoxLayer)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, BoxLayer, Card.ToPaintGeometry(Rect.GetSize(), FSlateLayoutTransform(1.0f, Rect.Min)), Brush,
			ESlateDrawEffect::None, Color);
	};
	auto Text = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, Card.ToPaintGeometry(FVector2D(400.0, 20.0), FSlateLayoutTransform(1.0f, At)),
			String, Font, ESlateDrawEffect::None, Color);
	};
	auto Width = [&](const FString& String, const FSlateFontInfo& Font) { return Measure->Measure(String, Font).X; };

	const FUISimSession& Session = Race->Session();
	const FUISimSnapshot& Snapshot = Session.Snapshot();
	const TArray<FUISimEntry>& Entries = Session.Entries();
	const double Laps = FMath::Max(1, Snapshot.TotalLaps);
	const double TrackLength = FMath::Max(1.0, Session.TrackLength());
	auto LapX = [&](double Left, double Lap) { return Left + BarLeft + FMath::Clamp(Lap / Laps, 0.0, 1.0) * BarWidth; };
	const FSlateFontInfo Small = UIStyle::Font(TEXT("Regular"), 7);
	const FSlateFontInfo SmallBold = UIStyle::Font(TEXT("Bold"), 7);
	const FSlateFontInfo CodeFont = UIStyle::Font(TEXT("Bold"), 8);

	++Layer;
	// Header and legend.
	Text(FVector2D(16.0, 13.0), TEXT("Strategy · stints"), UIStyle::Font(TEXT("Bold"), 11), UIStyle::Text);
	double LegendRight = Card.GetLocalSize().X - 16.0;
	for (int32 Item = 5; Item >= 0; --Item)
	{
		const FString Name = Item == 5 ? FString(TEXT("PROJECTED")) : FString(CompoundName[Item]);
		const double NameWidth = Width(Name, Small);
		Text(FVector2D(LegendRight - NameWidth, 15.0), Name, Small, UIStyle::Mute);
		const FBox2D Swatch(FVector2D(LegendRight - NameWidth - 19.0, 18.0), FVector2D(LegendRight - NameWidth - 5.0, 26.0));
		if (Item == 5)
		{
			Box(Swatch, &Round4, UIStyle::Mute, Layer);
			Box(Swatch.ExpandBy(-1.0), &Round4, UIStyle::Card, Layer + 1);
		}
		else
		{
			Box(Swatch, &Round4, CompoundColor(Item), Layer);
		}
		LegendRight -= NameWidth + 31.0;
	}

	// Leader's lap: where the "now" line stands.
	const FUISimCar* Leader = Snapshot.Cars.Num() > 0 ? &Snapshot.Cars[0] : nullptr;
	const double LeaderNow = Leader ? FMath::Clamp(Leader->Distance / TrackLength, 0.0, Laps) : 0.0;
	const FString NowLabel = FString::Printf(TEXT("LAP %d"), FMath::Clamp(Snapshot.LeaderLap, 1, Snapshot.TotalLaps));
	for (const double Left : ColumnLeft)
	{
		for (int32 Tick = 10; Tick <= Snapshot.TotalLaps; Tick += 10)
		{
			const FString Label = FString::FromInt(Tick);
			Text(FVector2D(LapX(Left, Tick) - Width(Label, Small) / 2.0, AxisTop), Label, Small, UIStyle::Mute);
		}
		Text(FVector2D(Left, AxisTop), TEXT("DRIVER"), Small, UIStyle::Mute);
		Text(FVector2D(Left + ChipLeft + ChipWidth - Width(TEXT("NEXT STOP"), Small), AxisTop), TEXT("NEXT STOP"), Small, UIStyle::Mute);
	}

	for (int32 Row = 0; Row < Snapshot.Cars.Num() && Row < 2 * RowsPerColumn; ++Row)
	{
		const FUISimCar& Car = Snapshot.Cars[Row];
		const FBox2D Rect = RowRect(Row);
		const double Left = ColumnLeft[Row / RowsPerColumn];
		const double Middle = Rect.GetCenter().Y;
		const bool bOut = Car.bRetired || Car.bDisqualified;
		const float Fade = bOut ? 0.5f : 1.0f;
		auto Faded = [Fade](FLinearColor Color) { Color.A *= Fade; return Color; };
		if (Car.EntryIndex == Race->FollowedCar())
		{
			Box(Rect, &Round7, FLinearColor(1.0f, 1.0f, 1.0f, 0.06f), Layer);
		}
		// Position, team dot, code.
		const FString Pos = FString::FromInt(Car.Position);
		Text(FVector2D(Left + 14.0 - Width(Pos, Small), Middle - 7.0), Pos, Small, Faded(UIStyle::Mute));
		Box(FBox2D(FVector2D(Left + 20.0, Middle - 3.5), FVector2D(Left + 27.0, Middle + 3.5)), &Round4,
			Faded(UIStyle::TeamColor(Entries, Car.EntryIndex)), Layer + 1);
		Text(FVector2D(Left + 32.0, Middle - 8.0), Entries[Car.EntryIndex].Code.ToUpper(), CodeFont, Faded(UIStyle::Text));

		// Bar background with a line every ten laps.
		const FBox2D Bar(FVector2D(Left + BarLeft, Middle - BarHeight / 2.0), FVector2D(Left + BarLeft + BarWidth, Middle + BarHeight / 2.0));
		Box(Bar, &Round7, UIStyle::Rgb(28, 29, 33), Layer);
		for (int32 Tick = 10; Tick < Snapshot.TotalLaps; Tick += 10)
		{
			Box(FBox2D(FVector2D(LapX(Left, Tick) - 0.5, Bar.Min.Y), FVector2D(LapX(Left, Tick) + 0.5, Bar.Max.Y)), White,
				UIStyle::Rgb(44, 45, 50), Layer + 1);
		}

		// Stints driven; the current one ends where the car is now.
		const double Now = Car.bFinished ? Car.LapsCompleted : FMath::Clamp(Car.Distance / TrackLength, 0.0, Laps);
		for (int32 Index = 0; Index < Car.Stints.Num(); ++Index)
		{
			const FUISimStint& Stint = Car.Stints[Index];
			const bool bCurrent = Index == Car.Stints.Num() - 1;
			const double From = Stint.StartLap, To = bCurrent ? FMath::Max(Now, From) : From + Stint.Laps;
			const double X0 = LapX(Left, From) + 1.0, X1 = FMath::Max(LapX(Left, To) - 1.0, X0 + 3.0);
			const FBox2D Piece(FVector2D(X0, Bar.Min.Y), FVector2D(X1, Bar.Max.Y));
			Box(Piece, &Round7, Faded(CompoundColor(Stint.Compound)), Layer + 2);
			FString Label = Piece.GetSize().X > 12.0 ? FString(CompoundLetter[FMath::Clamp(Stint.Compound, 0, 4)]) : FString();
			if (Piece.GetSize().X > 40.0)
			{
				Label += FString::Printf(TEXT("  %d L"), FMath::RoundToInt32(To - From));
			}
			if (!Label.IsEmpty())
			{
				Text(FVector2D(X0 + 5.0, Middle - 7.0), Label, SmallBold, UIStyle::Ink);
			}
		}

		// Projection: the current set up to the planned stop (or the flag); what comes after is not known yet.
		if (!bOut && !Car.bFinished && Car.Stints.Num() > 0)
		{
			const FLinearColor Color = CompoundColor(Car.Stints.Last().Compound);
			const double StopAt = Car.PlannedStopLap > 0 ? FMath::Max<double>(Car.PlannedStopLap, Now) : Laps;
			auto Projected = [&](double From, double To, const FLinearColor& Edge)
			{
				if (To - From < 0.3)
				{
					return;
				}
				const double X0 = LapX(Left, From) + 1.0, X1 = LapX(Left, To) - 1.0;
				Box(FBox2D(FVector2D(X0, Bar.Min.Y), FVector2D(X1, Bar.Max.Y)), &Round7, FLinearColor(Edge.R, Edge.G, Edge.B, 0.15f), Layer + 1);
				for (double X = X0 + 2.0; X < X1 - 2.0; X += 7.0) // dashed edge, as in the design
				{
					const double End = FMath::Min(X + 4.0, X1 - 2.0);
					Box(FBox2D(FVector2D(X, Bar.Min.Y), FVector2D(End, Bar.Min.Y + 1.0)), White, Edge, Layer + 2);
					Box(FBox2D(FVector2D(X, Bar.Max.Y - 1.0), FVector2D(End, Bar.Max.Y)), White, Edge, Layer + 2);
				}
			};
			Projected(Now, StopAt, Color);
			if (Car.PlannedStopLap > 0)
			{
				Projected(StopAt, Laps, UIStyle::Mute);
			}
		}

		// Next stop chip.
		FString Next;
		FLinearColor NextInk = UIStyle::Text, NextDot = FLinearColor::Transparent;
		if (bOut)
		{
			Next = TEXT("OUT");
			NextInk = UIStyle::Red;
		}
		else if (Car.bFinished)
		{
			Next = TEXT("FINISHED");
			NextInk = UIStyle::Mute;
		}
		else if (Car.bInPitLane)
		{
			Next = TEXT("IN PIT");
			NextInk = UIStyle::Green;
		}
		else if (Car.PlannedStopLap > 0)
		{
			Next = FString::Printf(TEXT("L%d"), FMath::Max(Car.PlannedStopLap, Car.LapsCompleted + 1));
			NextDot = UIStyle::Mute; // the next compound is the team's decision at the stop
		}
		else
		{
			Next = TEXT("TO FLAG");
			NextInk = UIStyle::Mute;
		}
		const double ChipRight = Left + ChipLeft + ChipWidth;
		const bool bChip = NextDot.A > 0.0f;
		const double TextWidth = Width(Next, SmallBold);
		if (bChip)
		{
			const FBox2D Chip(FVector2D(ChipRight - TextWidth - 26.0, Middle - 8.0), FVector2D(ChipRight, Middle + 8.0));
			Box(Chip, &Round7, UIStyle::Rgb(34, 35, 39), Layer);
			Box(FBox2D(FVector2D(Chip.Min.X + 8.0, Middle - 3.5), FVector2D(Chip.Min.X + 15.0, Middle + 3.5)), &Round4, NextDot, Layer + 1);
		}
		Text(FVector2D(ChipRight - TextWidth - (bChip ? 8.0 : 0.0), Middle - 7.0), Next, SmallBold, NextInk);
	}

	// The leader's lap as a line through each column, with its label on the axis.
	for (const double Left : ColumnLeft)
	{
		const double X = LapX(Left, LeaderNow);
		Box(FBox2D(FVector2D(X - 1.0, RowTop - 2.0), FVector2D(X + 1.0, RowTop + RowsPerColumn * RowStep)), &Round4, UIStyle::Text, Layer + 2);
		const double PillWidth = Width(NowLabel, SmallBold) + 14.0;
		const double PillLeft = FMath::Clamp(X - PillWidth / 2.0, Left + BarLeft - 4.0, Left + BarLeft + BarWidth + 4.0 - PillWidth);
		Box(FBox2D(FVector2D(PillLeft, AxisTop - 2.0), FVector2D(PillLeft + PillWidth, AxisTop + 14.0)), &Round7, UIStyle::Text, Layer + 2);
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 4, Card.ToPaintGeometry(FVector2D(PillWidth, 16.0),
			FSlateLayoutTransform(1.0f, FVector2D(PillLeft + 7.0, AxisTop))), NowLabel, SmallBold, ESlateDrawEffect::None, UIStyle::Ink);
	}
	Layer += 4;

	if (!bLogged)
	{
		// Viewport pixels of the rows, for -UISimScript checks (click x y).
		bLogged = true;
		const UWorld* World = GetWorld();
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
		if (ViewportWidget.IsValid())
		{
			const FVector2D Origin = FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
			for (int32 Row = 0; Row < 2 * RowsPerColumn; ++Row)
			{
				const FVector2D At = FVector2D(Card.LocalToAbsolute(RowRect(Row).GetCenter())) - Origin;
				UE_LOG(LogUISim, Display, TEXT("Strategy: row %d at %.0f %.0f"), Row + 1, At.X, At.Y);
			}
		}
	}
	return Layer;
}

FReply UUIStrategyWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (!Race || !CardArea || !Race->Session().IsStarted() || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Local = FVector2D(CardArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	const TArray<FUISimCar>& Cars = Race->Session().Snapshot().Cars;
	for (int32 Row = 0; Row < Cars.Num() && Row < 2 * RowsPerColumn; ++Row)
	{
		if (RowRect(Row).IsInside(Local))
		{
			FUICommand Command;
			Command.Type = EUICommand::FollowCar;
			Command.Index = Cars[Row].EntryIndex;
			Race->Execute(Command);
			break;
		}
	}
	return FReply::Handled();
}
