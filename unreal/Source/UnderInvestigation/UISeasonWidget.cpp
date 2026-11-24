#include "UISeasonWidget.h"

#include "UIRaceDirector.h"
#include "UISimCareer.h"
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
	// The one button, in the right card of the top bar: start the next race, or a new season after the last one.
	const FBox2D StartButton(UIStyle::TopWeather.Min + FVector2D(16.0, 16.0), UIStyle::TopWeather.Max - FVector2D(16.0, 16.0));

	// The three cards below the top bar, on the columns of the desk.
	const FBox2D CalendarCard(UIStyle::TimingCard.Min, FVector2D(UIStyle::TimingCard.Max.X, 1066.0));
	const FBox2D DriversCard(FVector2D(771.0, 98.0), FVector2D(1406.0, 1066.0));
	const FBox2D TeamsCard(FVector2D(1419.0, 98.0), FVector2D(1906.0, 1066.0));
	constexpr double BottomMargin = 12.0; // below the last row of a card

	const FLinearColor Separator = UIStyle::Rgb(29, 30, 34);
	const FLinearColor RainBlue = UIStyle::Rgb(80, 150, 255);

	FLinearColor Threshold(double Percent)
	{
		return Percent >= 75.0 ? UIStyle::Green : Percent >= 50.0 ? UIStyle::Yellow : UIStyle::Red;
	}
}

TSharedRef<SWidget> UUISeasonWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;

		// Covers the whole view: there is no race to look at between the races.
		Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrushColor(UIStyle::Background);
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		PanelSlot->SetOffsets(FMargin(0.0f));
	}
	return Super::RebuildWidget();
}

int32 UUISeasonWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FUISimCareer& Career = FUISimCareer::Get();
	if (!Career.IsOpen() || !Panel || Panel->GetCachedGeometry().GetLocalSize().X <= 0.0)
	{
		return Layer;
	}
	static const FSlateRoundedBoxBrush Round16(FLinearColor::White, 16.0f);
	static const FSlateRoundedBoxBrush Round15(FLinearColor::White, 15.0f);
	static const FSlateRoundedBoxBrush Capsule(FLinearColor::White, 36.0f); // radius of half the height
	static const FSlateRoundedBoxBrush Dot(FLinearColor::White, 3.5f);
	static const FSlateRoundedBoxBrush ButtonCapsule(FLinearColor::White, StartButton.GetSize().Y / 2.0);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FGeometry& Area = Panel->GetCachedGeometry(); // design units of the screen
	++Layer;
	auto Box = [&](const FVector2D& Min, const FVector2D& Max, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Area.ToPaintGeometry(Max - Min, FSlateLayoutTransform(1.0f, Min)), Brush,
			ESlateDrawEffect::None, Color);
	};
	// A card of the design: fill with a border one unit wide.
	auto CardBox = [&](const FBox2D& Rect, const FLinearColor& Fill, bool bCapsule = false)
	{
		Box(Rect.Min, Rect.Max, bCapsule ? &Capsule : &Round16, UIStyle::CardBorder);
		Box(Rect.Min + FVector2D(1.0, 1.0), Rect.Max - FVector2D(1.0, 1.0), bCapsule ? &Capsule : &Round15, Fill);
	};
	auto Text = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, Area.ToPaintGeometry(FVector2D(900.0, 40.0), FSlateLayoutTransform(1.0f, At)),
			String, Font, ESlateDrawEffect::None, Color);
	};
	auto Width = [&](const FString& String, const FSlateFontInfo& Font) { return Measure->Measure(String, Font).X; };
	auto TextRight = [&](double RightEdge, double Y, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		Text(FVector2D(RightEdge - Width(String, Font), Y), String, Font, Color);
	};
	// Cut to MaxWidth, with "…" when something is cut.
	auto Fit = [&](const FString& String, const FSlateFontInfo& Font, double MaxWidth)
	{
		if (Width(String, Font) <= MaxWidth)
		{
			return String;
		}
		FString Cut = String;
		while (Cut.Len() > 0 && Width(Cut + TEXT("…"), Font) > MaxWidth)
		{
			Cut.LeftChopInline(1);
		}
		return Cut.TrimEnd() + TEXT("…");
	};
	const FSlateFontInfo LabelFont = UIStyle::Font(TEXT("Regular"), 7);
	const FSlateFontInfo TagFont = UIStyle::Font(TEXT("Bold"), 7);
	const FSlateFontInfo SubFont = UIStyle::Font(TEXT("Regular"), 8);
	const FSlateFontInfo RowFont = UIStyle::Font(TEXT("Regular"), 9);
	const FSlateFontInfo RowBold = UIStyle::Font(TEXT("Bold"), 9);
	const FSlateFontInfo CardTitleFont = UIStyle::Font(TEXT("Bold"), 11);
	const FSlateFontInfo ValueFont = UIStyle::Font(TEXT("Bold"), 16);
	const FSlateFontInfo UnitFont = UIStyle::Font(TEXT("Regular"), 16);

	const TArray<FUISimSeasonRace> Calendar = Career.Calendar();
	const TArray<FUISimStanding> Drivers = Career.Drivers();
	const TArray<FUISimStanding> Teams = Career.Teams();
	const int32 Next = Career.NextRace();
	const bool bFinished = Career.IsFinished();
	TArray<FString> Suspended;
	for (const FUISimStanding& Driver : Drivers)
	{
		if (Driver.bSuspended)
		{
			Suspended.Add(Driver.Name);
		}
	}

	// Cards.
	for (const FBox2D* Card : {&UIStyle::TopTitle, &UIStyle::TopSession, &UIStyle::TopWeather, &CalendarCard, &DriversCard, &TeamsCard})
	{
		CardBox(*Card, UIStyle::Card);
	}
	CardBox(UIStyle::TopBanner, UIStyle::Card, true);
	Layer += 2; // what follows lies on the cards (a layer of its own: Slate does not keep the order within one layer)

	// Title.
	const FSlateFontInfo TitleFont = UIStyle::Font(TEXT("Bold"), 13, 40);
	const FVector2D TitleAt = UIStyle::TopTitle.Min + FVector2D(15.0, 19.0);
	Text(TitleAt, TEXT("RACE CONTROL"), TitleFont, UIStyle::Text);
	Text(TitleAt + FVector2D(Width(TEXT("RACE CONTROL"), TitleFont) + 8.0, 0.0), TEXT("/ SIM"), UIStyle::Font(TEXT("Regular"), 13, 40), UIStyle::Mute);
	Text(TitleAt + FVector2D(0.0, 21.0), Fit(TEXT("SEASON · ") + Career.SeasonName().ToUpper(), SubFont, UIStyle::TopTitle.GetSize().X - 30.0),
		SubFont, UIStyle::Mute);

	// Banner: the next race, or the champions after the last one; label and sub line centred as one group.
	FString BannerLabel, BannerSub, BannerSub2;
	FLinearColor BannerInk = UIStyle::Text;
	if (Calendar.IsValidIndex(Next))
	{
		BannerLabel = FString::Printf(TEXT("RACE %d OF %d"), Next + 1, Calendar.Num());
		BannerSub = Calendar[Next].Name;
	}
	else
	{
		BannerLabel = TEXT("SEASON COMPLETE");
		BannerInk = UIStyle::Green;
		if (Drivers.Num() > 0 && Teams.Num() > 0)
		{
			// Two lines beside the label: both names are too long for one.
			BannerSub = FString::Printf(TEXT("Champion: %s"), *Drivers[0].Name);
			BannerSub2 = FString::Printf(TEXT("Constructors: %s"), *Teams[0].Name);
		}
	}
	const FSlateFontInfo BannerFont = UIStyle::Font(TEXT("Bold"), 21, 60);
	const double LabelWidth = Width(BannerLabel, BannerFont);
	const double SubRoom = UIStyle::TopBanner.GetSize().X - 72.0 - LabelWidth - 12.0;
	BannerSub = Fit(BannerSub, SubFont, SubRoom);
	BannerSub2 = Fit(BannerSub2, SubFont, SubRoom);
	const double SubWidth = FMath::Max(Width(BannerSub, SubFont), Width(BannerSub2, SubFont));
	const double GroupLeft = UIStyle::TopBanner.GetCenter().X - (LabelWidth + 12.0 + SubWidth) / 2.0;
	Text(FVector2D(GroupLeft, UIStyle::TopBanner.Min.Y + 20.0), BannerLabel, BannerFont, BannerInk);
	Text(FVector2D(GroupLeft + LabelWidth + 12.0, UIStyle::TopBanner.Min.Y + (BannerSub2.IsEmpty() ? 30.0 : 22.0)), BannerSub, SubFont, UIStyle::Text);
	Text(FVector2D(GroupLeft + LabelWidth + 12.0, UIStyle::TopBanner.Min.Y + 38.0), BannerSub2, SubFont, UIStyle::Text);

	// Season rating and races run, as the lap and session fields of the desk.
	const FVector2D RatingAt = UIStyle::TopSession.Min + FVector2D(16.0, 16.0);
	const FString RatingValue = Next > 0 ? FString::Printf(TEXT("%.1f"), Career.Rating()) : FString(TEXT("—"));
	const FString Hundred = TEXT(" / 100");
	Text(RatingAt, TEXT("SEASON RATING"), LabelFont, UIStyle::Mute);
	Text(RatingAt + FVector2D(0.0, 16.0), RatingValue, ValueFont, Next > 0 ? Threshold(Career.Rating()) : UIStyle::Mute);
	Text(RatingAt + FVector2D(Width(RatingValue, ValueFont), 16.0), Hundred, UnitFont, UIStyle::Mute);
	const double RatingWidth = FMath::Max(Width(RatingValue, ValueFont) + Width(Hundred, UnitFont), Width(TEXT("SEASON RATING"), LabelFont));
	const FVector2D RacesAt = RatingAt + FVector2D(RatingWidth + 22.0, 0.0);
	const FString Done = FString::FromInt(Next);
	Text(RacesAt, TEXT("RACES"), LabelFont, UIStyle::Mute);
	Text(RacesAt + FVector2D(0.0, 16.0), Done, ValueFont, UIStyle::Text);
	Text(RacesAt + FVector2D(Width(Done, ValueFont), 16.0), FString::Printf(TEXT(" / %d"), Calendar.Num()), UnitFont, UIStyle::Mute);

	// The button: primary, a yellow capsule.
	const TCHAR* Label = bFinished ? TEXT("NEW SEASON") : TEXT("START RACE");
	const FSlateFontInfo ButtonFont = UIStyle::Font(TEXT("Bold"), 10);
	Box(StartButton.Min, StartButton.Max, &ButtonCapsule, UIStyle::Yellow);
	Text(StartButton.GetCenter() - Measure->Measure(Label, ButtonFont) / 2.0, Label, ButtonFont, UIStyle::Ink);

	// Calendar: two lines per race, the second one with the rating details once the race is run.
	{
		const FBox2D& Card = CalendarCard;
		const double X = Card.Min.X;
		Text(Card.Min + FVector2D(16.0, 13.0), TEXT("Calendar"), CardTitleFont, UIStyle::Text);
		const double RowsTop = Card.Min.Y + 44.0;
		const double RowHeight = FMath::Min(40.0, (Card.Max.Y - BottomMargin - RowsTop) / FMath::Max(1, Calendar.Num()));
		for (int32 Index = 0; Index < Calendar.Num(); ++Index)
		{
			const FUISimSeasonRace& Race = Calendar[Index];
			const double Top = RowsTop + Index * RowHeight;
			const double Y = Top + 5.0;
			if (Index == Next)
			{
				Box(FVector2D(X + 1.0, Top), FVector2D(Card.Max.X - 1.0, Top + RowHeight), White, FLinearColor(1.0f, 1.0f, 1.0f, 0.06f));
				// After the RAIN tag when it reaches that far.
				const double NextLeft = Race.bRain ? FMath::Max(600.0, 572.0 + Width(TEXT("RAIN"), TagFont) + 8.0) : 600.0;
				Text(FVector2D(X + NextLeft, Y + 2.0), TEXT("NEXT"), TagFont, UIStyle::Yellow);
			}
			Text(FVector2D(X + 16.0, Y), FString::FromInt(Index + 1), RowBold, UIStyle::Text);
			const FString Laps = FString::Printf(TEXT("%d L"), Race.Laps);
			const double NameRight = X + 560.0 - Width(Laps, RowFont) - 12.0;
			const FString Name = Fit(Race.Name, RowFont, NameRight - (X + 44.0));
			Text(FVector2D(X + 44.0, Y), Name, RowFont, UIStyle::Text);
			if (Race.Circuit != Race.Name && Name == Race.Name)
			{
				const double CircuitLeft = X + 44.0 + Width(Name, RowFont);
				Text(FVector2D(CircuitLeft, Y), Fit(TEXT(" · ") + Race.Circuit, RowFont, NameRight - CircuitLeft), RowFont, UIStyle::Mute);
			}
			TextRight(X + 560.0, Y, Laps, RowFont, UIStyle::Mute);
			if (Race.bRain)
			{
				Text(FVector2D(X + 572.0, Y + 2.0), TEXT("RAIN"), TagFont, RainBlue);
			}
			if (Race.bDone)
			{
				TextRight(X + 728.0, Y, FString::Printf(TEXT("%.1f"), Race.Rating), RowBold, Threshold(Race.Rating));
				FString Details = FString::Printf(TEXT("F %.0f · C %.0f · Q %.0f · S %.0f · %d incidents, %d decided"), Race.Fairness * 100.0,
					Race.Consistency * 100.0, Race.RaceQuality * 100.0, Race.Safety * 100.0, Race.Incidents, Race.Decided);
				if (Race.Overturned > 0)
				{
					Details += FString::Printf(TEXT(", %d overturned"), Race.Overturned);
				}
				TArray<FString> Podium;
				for (int32 Place = 0; Place < Race.Podium.Num(); ++Place)
				{
					Podium.Add(FString::Printf(TEXT("P%d %s"), Place + 1, *Race.Podium[Place]));
				}
				if (Podium.Num() > 0)
				{
					Details += TEXT(" · ") + FString::Join(Podium, TEXT(", "));
				}
				if (Race.SatOut.Num() > 0)
				{
					Details += TEXT(" · sat out: ") + FString::Join(Race.SatOut, TEXT(", "));
				}
				Text(FVector2D(X + 44.0, Y + 16.0), Fit(Details, LabelFont, Card.Max.X - 16.0 - (X + 44.0)), LabelFont, UIStyle::Mute);
			}
			else if (Index == Next && Suspended.Num() > 0)
			{
				Text(FVector2D(X + 44.0, Y + 16.0), Fit(TEXT("suspended: ") + FString::Join(Suspended, TEXT(", ")), LabelFont,
					Card.Max.X - 16.0 - (X + 44.0)), LabelFont, UIStyle::Red);
			}
			if (Index + 1 < Calendar.Num())
			{
				Box(FVector2D(X + 16.0, Top + RowHeight - 1.0), FVector2D(Card.Max.X - 16.0, Top + RowHeight), White, Separator);
			}
		}
	}

	// Championships: a header line, then one row per driver or team.
	const TArray<FString> TeamOrder = Career.TeamOrder();
	auto Table = [&](const FBox2D& Card, const TCHAR* Title, const TArray<FUISimStanding>& Rows, bool bDrivers)
	{
		const double X = Card.Min.X;
		const double PtsRight = X + (bDrivers ? 500.0 : 380.0);
		const double WinsRight = X + (bDrivers ? 550.0 : 440.0);
		const double PenRight = X + 600.0;
		const double NameLeft = X + 50.0;
		Text(Card.Min + FVector2D(16.0, 13.0), Title, CardTitleFont, UIStyle::Text);
		const double HeadY = Card.Min.Y + 44.0;
		Text(FVector2D(X + 16.0, HeadY), TEXT("#"), LabelFont, UIStyle::Mute);
		Text(FVector2D(NameLeft, HeadY), bDrivers ? TEXT("DRIVER") : TEXT("TEAM"), LabelFont, UIStyle::Mute);
		TextRight(PtsRight, HeadY, TEXT("PTS"), LabelFont, UIStyle::Mute);
		TextRight(WinsRight, HeadY, TEXT("WINS"), LabelFont, UIStyle::Mute);
		if (bDrivers)
		{
			Text(FVector2D(X + 250.0, HeadY), TEXT("TEAM"), LabelFont, UIStyle::Mute);
			TextRight(PenRight, HeadY, TEXT("PEN"), LabelFont, UIStyle::Mute);
		}
		const double RowsTop = Card.Min.Y + 62.0;
		const double RowHeight = FMath::Min(30.0, (Card.Max.Y - BottomMargin - RowsTop) / FMath::Max(1, Rows.Num()));
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			const FUISimStanding& Row = Rows[Index];
			const double Top = RowsTop + Index * RowHeight;
			const double Y = Top + (RowHeight - 15.0) / 2.0;
			Text(FVector2D(X + 16.0, Y), FString::FromInt(Index + 1), RowFont, UIStyle::Mute);
			const int32 Team = TeamOrder.IndexOfByKey(bDrivers ? Row.Team : Row.Name);
			if (Team >= 0)
			{
				Box(FVector2D(X + 37.0, Top + RowHeight / 2.0 - 3.5), FVector2D(X + 44.0, Top + RowHeight / 2.0 + 3.5), &Dot,
					UIStyle::TeamColor(Team, TeamOrder.Num()));
			}
			const double NameRight = bDrivers ? X + 240.0 : PtsRight - 50.0;
			if (bDrivers && Row.bSuspended)
			{
				// The tag does not fit after PEN within the card: it follows the name.
				const double TagWidth = Width(TEXT("SUSPENDED"), TagFont);
				const FString Name = Fit(Row.Name, RowFont, NameRight - TagWidth - 8.0 - (NameLeft));
				Text(FVector2D(NameLeft, Y), Name, RowFont, UIStyle::Text);
				Text(FVector2D(NameLeft + Width(Name, RowFont) + 8.0, Y + 2.0), TEXT("SUSPENDED"), TagFont, UIStyle::Red);
			}
			else
			{
				Text(FVector2D(NameLeft, Y), Fit(Row.Name, RowFont, NameRight - (NameLeft)), RowFont, UIStyle::Text);
			}
			TextRight(PtsRight, Y, FString::FromInt(Row.Points), RowBold, UIStyle::Text);
			TextRight(WinsRight, Y, FString::FromInt(Row.Wins), RowFont, UIStyle::Text);
			if (bDrivers)
			{
				Text(FVector2D(X + 250.0, Y), Fit(Row.Team, RowFont, PtsRight - 50.0 - (X + 250.0)), RowFont, UIStyle::Mute);
				TextRight(PenRight, Y, FString::FromInt(Row.PenaltyPoints), RowFont, Row.PenaltyPoints > 0 ? UIStyle::Text : UIStyle::Mute);
			}
			if (Index + 1 < Rows.Num())
			{
				Box(FVector2D(X + 16.0, Top + RowHeight - 1.0), FVector2D(Card.Max.X - 16.0, Top + RowHeight), White, Separator);
			}
		}
	};
	Table(DriversCard, TEXT("Drivers' championship"), Drivers, true);
	Table(TeamsCard, TEXT("Constructors' championship"), Teams, false);
	Layer += 2;

	// Viewport pixel of the button, for -UISimScript checks (click x y); logged again when it moves.
	const UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
	if (ViewportWidget.IsValid())
	{
		const FVector2D At = FVector2D(Area.LocalToAbsolute(StartButton.GetCenter())) -
			FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
		if (At != LoggedAt)
		{
			LoggedAt = At;
			UE_LOG(LogUISim, Display, TEXT("Season: '%s' at %.0f %.0f"), Label, At.X, At.Y);
		}
	}
	return Layer;
}

FReply UUISeasonWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (!Panel || !Race || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Local = FVector2D(Panel->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	if (StartButton.IsInside(Local))
	{
		FUICommand Command;
		Command.Type = FUISimCareer::Get().IsFinished() ? EUICommand::NewSeason : EUICommand::StartSeasonRace;
		Race->Execute(Command);
	}
	return FReply::Handled();
}
