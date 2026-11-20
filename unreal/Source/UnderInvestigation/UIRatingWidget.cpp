#include "UIRatingWidget.h"

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
	constexpr int32 RowsPerPage = 16;
	// Card and its parts, in design units (the panel covers the whole 1920 x 1080 screen).
	const FBox2D CardRect(FVector2D(400.0, 140.0), FVector2D(1520.0, 940.0));
	constexpr double RowsTop = 196.0, RowHeight = 30.0, ButtonTop = 740.0, ButtonHeight = 30.0;
	const FLinearColor Fill = UIStyle::Rgb(34, 35, 39);
	const FLinearColor Edge = UIStyle::Rgb(44, 45, 49);

	// Buttons in card coordinates, from the right edge: TO SEASON (season only), NEXT, PREV.
	struct FButtons
	{
		FBox2D Season, Next, Prev;
		double PageRight = 0.0; // right edge of the page label
	};
	FButtons LayoutButtons(bool bSeason)
	{
		FButtons B;
		double Right = 1096.0;
		if (bSeason)
		{
			B.Season = FBox2D(FVector2D(Right - 130.0, ButtonTop), FVector2D(Right, ButtonTop + ButtonHeight));
			Right -= 140.0;
		}
		B.Next = FBox2D(FVector2D(Right - 84.0, ButtonTop), FVector2D(Right, ButtonTop + ButtonHeight));
		Right -= 94.0;
		B.Prev = FBox2D(FVector2D(Right - 84.0, ButtonTop), FVector2D(Right, ButtonTop + ButtonHeight));
		B.PageRight = B.Prev.Min.X - 16.0;
		return B;
	}

	// Incidents that are rated: accidents nobody decided are not (core Rating.cpp).
	TArray<int32> RatedIncidents(const AUIRaceDirector& Race)
	{
		TArray<int32> Rated;
		const FUISimRating Rating = Race.Session().Rating();
		const TArray<FUISimIncident>& Incidents = Race.Session().Incidents();
		for (int32 Index = 0; Index < Rating.Incidents.Num(); ++Index)
		{
			if (!(Incidents[Index].Type == EUISimIncidentType::Accident && !Rating.Incidents[Index].bDecided))
			{
				Rated.Add(Index);
			}
		}
		return Rated;
	}

	FLinearColor Threshold(double Percent)
	{
		return Percent >= 75.0 ? UIStyle::Green : Percent >= 50.0 ? UIStyle::Yellow : UIStyle::Red;
	}

	FString Decision(const TArray<FUISimEntry>& Entries, int32 Car, EUISimPenalty Penalty)
	{
		if (Penalty == EUISimPenalty::None || Car < 0)
		{
			return TEXT("NO FURTHER ACTION");
		}
		return AUIRaceDirector::PenaltyName(Penalty).ToUpper() + TEXT(" ") + Entries[Car].Code;
	}
}

TSharedRef<SWidget> UUIRatingWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;

		Panel = WidgetTree->ConstructWidget<UBorder>(); // dims the screen, takes (and swallows) the clicks
		Panel->SetBrushColor(FLinearColor(UIStyle::Background.R, UIStyle::Background.G, UIStyle::Background.B, 0.72f));
		Panel->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetPosition(FVector2D::ZeroVector);
		PanelSlot->SetSize(UIStyle::Screen);
	}
	return Super::RebuildWidget();
}

void UUIRatingWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted() || !Race->Session().Snapshot().bFinished || Race->IsReviewing())
	{
		Panel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	Panel->SetVisibility(ESlateVisibility::Visible); // takes all clicks: nothing below reacts
	PageCount = FMath::Max(1, (RatedIncidents(*Race).Num() + RowsPerPage - 1) / RowsPerPage);
	Page = FMath::Clamp(Page, 0, PageCount - 1);
}

int32 UUIRatingWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Panel || !Panel->IsVisible() || Panel->GetCachedGeometry().GetLocalSize().X <= 0.0)
	{
		return Layer;
	}
	static const FSlateRoundedBoxBrush Round16(FLinearColor::White, 16.0f);
	static const FSlateRoundedBoxBrush Round15(FLinearColor::White, 15.0f);
	static const FSlateRoundedBoxBrush Round12(FLinearColor::White, 12.0f);
	static const FSlateRoundedBoxBrush Round11(FLinearColor::White, 11.0f);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FGeometry& Area = Panel->GetCachedGeometry(); // design units of the screen
	const FVector2D Origin = CardRect.Min;
	auto Box = [&](const FBox2D& Rect, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Area.ToPaintGeometry(Rect.GetSize(), FSlateLayoutTransform(1.0f, Rect.Min + Origin)),
			Brush, ESlateDrawEffect::None, Color);
	};
	auto Text = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, Area.ToPaintGeometry(FVector2D(600.0, 80.0),
			FSlateLayoutTransform(1.0f, At + Origin)), String, Font, ESlateDrawEffect::None, Color);
	};
	auto TextRight = [&](double RightEdge, double Y, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		Text(FVector2D(RightEdge - Measure->Measure(String, Font).X, Y), String, Font, Color);
	};
	const FSlateFontInfo Small = UIStyle::Font(TEXT("Regular"), 7);
	const FSlateFontInfo SmallBold = UIStyle::Font(TEXT("Bold"), 7);
	const FSlateFontInfo Label = UIStyle::Font(TEXT("Regular"), 8);
	const FSlateFontInfo LabelBold = UIStyle::Font(TEXT("Bold"), 8);

	const FUISimSession& Session = Race->Session();
	const FUISimRating Rating = Session.Rating();
	const TArray<FUISimEntry>& Entries = Session.Entries();
	const TArray<FUISimIncident>& Incidents = Session.Incidents();
	const TArray<int32> Rated = RatedIncidents(*Race);
	const bool bSeason = Race->IsSeasonMode();
	const FButtons Buttons = LayoutButtons(bSeason);

	// Card.
	Box(FBox2D(FVector2D::ZeroVector, CardRect.GetSize()), &Round16, UIStyle::CardBorder);
	Box(FBox2D(FVector2D(1.0, 1.0), CardRect.GetSize() - FVector2D(1.0, 1.0)), &Round15, UIStyle::Rgb(21, 22, 26));
	++Layer;

	// Header.
	Text(FVector2D(24.0, 20.0), TEXT("Race director rating"), UIStyle::Font(TEXT("Bold"), 11), UIStyle::Text);
	TextRight(CardRect.GetSize().X - 24.0, 23.0, Session.TrackName().ToUpper() + TEXT(" · RACE"), Label, UIStyle::Mute);

	// Overall score; the small "/ 100" shares the baseline of the big number.
	const FSlateFontInfo Big = UIStyle::Font(TEXT("Bold"), 40);
	const FSlateFontInfo Unit = UIStyle::Font(TEXT("Regular"), 16);
	const FString Score = FString::Printf(TEXT("%.0f"), Rating.Overall);
	const FVector2D ScoreSize = Measure->Measure(Score, Big);
	Text(FVector2D(24.0, 56.0), Score, Big, Threshold(Rating.Overall));
	Text(FVector2D(24.0 + ScoreSize.X + 4.0, 56.0 + (ScoreSize.Y - Measure->Measure(TEXT(" / 100"), Unit).Y) * 0.85), TEXT(" / 100"), Unit, UIStyle::Mute);
	Text(FVector2D(24.0, 132.0), TEXT("OVERALL"), Small, UIStyle::Mute);

	// Category tiles.
	const TCHAR* const TileLabel[] = {TEXT("FAIRNESS"), TEXT("CONSISTENCY"), TEXT("RACE QUALITY"), TEXT("SAFETY")};
	const double TileValue[] = {Rating.Fairness, Rating.Consistency, Rating.RaceQuality, Rating.Safety};
	const FString TileNote[] = {TEXT("Verdicts against the truth"), TEXT("Similar cases, similar verdicts"),
		TEXT("Little neutralisation, no late verdicts"), FString::Printf(TEXT("Sites secured fast · %d follow-up"), Rating.FollowUpAccidents)};
	const double TileWidth = (1096.0 - 240.0 - 3.0 * 12.0) / 4.0;
	for (int32 Tile = 0; Tile < 4; ++Tile)
	{
		const double Left = 240.0 + Tile * (TileWidth + 12.0);
		const FBox2D Rect(FVector2D(Left, 56.0), FVector2D(Left + TileWidth, 150.0));
		Box(Rect, &Round12, Edge);
		Box(Rect.ExpandBy(-1.0), &Round11, Fill);
		Text(Rect.Min + FVector2D(14.0, 12.0), TileLabel[Tile], Small, UIStyle::Mute);
		Text(Rect.Min + FVector2D(14.0, 28.0), FString::Printf(TEXT("%.0f %%"), TileValue[Tile] * 100.0), UIStyle::Font(TEXT("Bold"), 20),
			Threshold(TileValue[Tile] * 100.0));
		Text(Rect.Min + FVector2D(14.0, 64.0), TileNote[Tile], Small, UIStyle::Mute);
	}

	// Incident table.
	Text(FVector2D(24.0, 172.0), TEXT("#"), Small, UIStyle::Mute);
	Text(FVector2D(70.0, 172.0), TEXT("INCIDENT"), Small, UIStyle::Mute);
	Text(FVector2D(400.0, 172.0), TEXT("YOUR VERDICT"), Small, UIStyle::Mute);
	Text(FVector2D(640.0, 172.0), TEXT("CORRECT"), Small, UIStyle::Mute);
	TextRight(930.0, 172.0, TEXT("BLAME"), Small, UIStyle::Mute);
	TextRight(1010.0, 172.0, TEXT("SCORE"), Small, UIStyle::Mute);
	for (int32 Line = 0; Line < RowsPerPage; ++Line)
	{
		const int32 RatedSlot = Page * RowsPerPage + Line;
		if (!Rated.IsValidIndex(RatedSlot))
		{
			break;
		}
		const int32 Index = Rated[RatedSlot];
		const FUISimIncidentRating& R = Rating.Incidents[Index];
		const FUISimIncident& Incident = Incidents[Index];
		const double Top = RowsTop + Line * RowHeight;
		const double Y = Top + 8.0;
		FString Where = Entries[Incident.Car].Code;
		if (Incident.OtherCar != Incident.Car)
		{
			Where += TEXT(" / ") + Entries[Incident.OtherCar].Code;
		}
		const FString Given = R.bDecided ? Decision(Entries, R.GivenCar, R.GivenPenalty) : FString(TEXT("NOT DECIDED"));
		const double Percent = FMath::RoundToInt(R.Score * 100.0);
		Text(FVector2D(24.0, Y), FString::FromInt(R.IncidentId), Label, UIStyle::Text);
		Text(FVector2D(70.0, Y), AUIRaceDirector::IncidentTypeName(Incident.Type) + TEXT(" · ") + Where, Label, UIStyle::Text);
		Text(FVector2D(400.0, Y), Given, Label, R.bDecided ? UIStyle::Text : UIStyle::Mute);
		Text(FVector2D(640.0, Y), Decision(Entries, R.ExpectedCar, R.ExpectedPenalty), Label, UIStyle::Text);
		TextRight(930.0, Y, FString::Printf(TEXT("%d %%"), static_cast<int32>(FMath::RoundToInt(Incident.GroundTruthBlame * 100.0))), Label, UIStyle::Text);
		TextRight(1010.0, Y, FString::Printf(TEXT("%.0f %%"), Percent), LabelBold, Threshold(Percent));
		if (R.bOverturned)
		{
			Text(FVector2D(1030.0, Y + 1.0), TEXT("OVERTURNED"), SmallBold, UIStyle::Red);
		}
		else if (R.bHeard)
		{
			Text(FVector2D(1030.0, Y + 1.0), TEXT("HEARD"), Small, UIStyle::Mute);
		}
		Box(FBox2D(FVector2D(24.0, Top + RowHeight - 1.0), FVector2D(1096.0, Top + RowHeight)), White, UIStyle::Rgb(29, 30, 34));
	}

	// Footer.
	double FooterY = 728.0;
	if (Rating.Overturned > 0)
	{
		Text(FVector2D(24.0, FooterY), FString::Printf(TEXT("Set aside on review: %d verdict(s) without a hearing, 5 points each"), Rating.Overturned),
			Small, UIStyle::Mute);
		FooterY += 14.0;
	}
	Text(FVector2D(24.0, FooterY), TEXT("BLAME: share of the attacker in the incident"), Small, UIStyle::Mute);

	// Buttons: framed capsules; TO SEASON is the primary one.
	auto Button = [&](const FBox2D& Rect, const TCHAR* Text_, bool bPrimary, bool bEnabled)
	{
		Box(Rect, &Round15, bPrimary ? UIStyle::Yellow : Edge);
		if (!bPrimary)
		{
			Box(Rect.ExpandBy(-1.0), &Round15, Fill);
		}
		FLinearColor Ink = bPrimary ? UIStyle::Ink : UIStyle::Text;
		Ink.A = bEnabled ? 1.0f : 0.35f;
		const FVector2D Size = Measure->Measure(Text_, LabelBold);
		Text(Rect.GetCenter() - Size / 2.0, Text_, LabelBold, Ink);
	};
	if (bSeason)
	{
		Button(Buttons.Season, TEXT("TO SEASON"), true, true);
	}
	Button(Buttons.Next, TEXT("NEXT"), false, Page + 1 < PageCount);
	Button(Buttons.Prev, TEXT("PREV"), false, Page > 0);
	TextRight(Buttons.PageRight, ButtonTop + 8.0, FString::Printf(TEXT("PAGE %d/%d"), Page + 1, PageCount), Label, UIStyle::Mute);
	Layer += 2;

	// Viewport pixels of the buttons, for -UISimScript checks (click x y); logged again when they move.
	const UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
	if (ViewportWidget.IsValid())
	{
		const FVector2D ViewOrigin = FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
		auto Pixel = [&](const FBox2D& Rect) { return FVector2D(Area.LocalToAbsolute(Rect.GetCenter() + Origin)) - ViewOrigin; };
		const FVector2D Next = Pixel(Buttons.Next);
		if (Next != LoggedAt)
		{
			LoggedAt = Next;
			const FVector2D Prev = Pixel(Buttons.Prev);
			UE_LOG(LogUISim, Display, TEXT("Rating: 'PREV' at %.0f %.0f, 'NEXT' at %.0f %.0f"), Prev.X, Prev.Y, Next.X, Next.Y);
			if (bSeason)
			{
				const FVector2D Season = Pixel(Buttons.Season);
				UE_LOG(LogUISim, Display, TEXT("Rating: 'TO SEASON' at %.0f %.0f"), Season.X, Season.Y);
			}
		}
	}
	return Layer;
}

FReply UUIRatingWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!Panel || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Local = FVector2D(Panel->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition())) - CardRect.Min;
	AUIRaceDirector* Race = Director.Get();
	const bool bSeason = Race && Race->IsSeasonMode();
	const FButtons Buttons = LayoutButtons(bSeason);
	if (bSeason && Buttons.Season.IsInside(Local))
	{
		FUICommand Command;
		Command.Type = EUICommand::BackToSeason;
		Race->Execute(Command); // books the race; this screen goes with it
		return FReply::Handled();
	}
	const int32 Before = Page;
	if (Buttons.Prev.IsInside(Local))
	{
		Page = FMath::Max(Page - 1, 0);
	}
	else if (Buttons.Next.IsInside(Local))
	{
		Page = FMath::Min(Page + 1, PageCount - 1);
	}
	if (Page != Before)
	{
		UE_LOG(LogUISim, Display, TEXT("Rating: page %d/%d"), Page + 1, PageCount);
	}
	return FReply::Handled();
}
