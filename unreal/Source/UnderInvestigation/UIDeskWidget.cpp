#include "UIDeskWidget.h"

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
	// Sim speed buttons of the design: a dark capsule with four segments, the active one bright.
	struct FSpeedButton
	{
		const TCHAR* Label;
		double Scale;
		double Width;
	};
	const FSpeedButton SpeedButtons[] = {{TEXT("PAUSE"), 0.0, 58.0}, {TEXT("1×"), 1.0, 34.0}, {TEXT("4×"), 4.0, 36.0},
		{TEXT("16×"), 16.0, 40.0}};
	const FBox2D SpeedBox(FVector2D(1282.0, 43.0), FVector2D(1462.0, 73.0));

	FBox2D SpeedButtonRect(int32 Index)
	{
		double Left = SpeedBox.Min.X + 3.0;
		for (int32 Before = 0; Before < Index; ++Before)
		{
			Left += SpeedButtons[Before].Width + 2.0;
		}
		return FBox2D(FVector2D(Left, SpeedBox.Min.Y + 3.0), FVector2D(Left + SpeedButtons[Index].Width, SpeedBox.Max.Y - 3.0));
	}

	struct FBanner
	{
		FString Label;
		FString Sub;
		FLinearColor Fill = UIStyle::Card;
		FLinearColor Ink = UIStyle::Green;
	};

	// The big status line, as in the design; the states the design does not know follow its pattern.
	FBanner MakeBanner(const FUISimSnapshot& Snapshot)
	{
		FBanner Banner;
		TArray<FString> Yellow;
		bool bDouble = false;
		for (int32 Sector = 0; Sector < Snapshot.SectorFlags.Num(); ++Sector)
		{
			if (Snapshot.SectorFlags[Sector] != EUISimFlag::Green)
			{
				Yellow.Add(FString::FromInt(Sector + 1));
				bDouble |= Snapshot.SectorFlags[Sector] == EUISimFlag::DoubleYellow;
			}
		}
		if (Snapshot.bFinished || Snapshot.bChequered)
		{
			Banner = {TEXT("CHEQUERED FLAG"), Snapshot.bFinished ? TEXT("RACE FINISHED") : TEXT("LEADER HAS FINISHED"), UIStyle::Text, UIStyle::Ink};
		}
		else if (Snapshot.State == EUISimRaceState::RedFlag)
		{
			Banner = {TEXT("RED FLAG"), Snapshot.bGridStart ? TEXT("RESTART IN PROGRESS") : TEXT("SESSION SUSPENDED"), UIStyle::Red,
				FLinearColor::White};
		}
		else if (Snapshot.bFormationLap)
		{
			Banner = {TEXT("FORMATION LAP"), TEXT("STANDING START FOLLOWS"), UIStyle::Card, UIStyle::Yellow};
		}
		else if (Snapshot.State == EUISimRaceState::SafetyCar)
		{
			Banner = {TEXT("SAFETY CAR"), Snapshot.bSafetyCarInThisLap ? TEXT("IN THIS LAP") : TEXT("DEPLOYED · NO OVERTAKING"),
				UIStyle::Yellow, UIStyle::Ink};
		}
		else if (Snapshot.State == EUISimRaceState::VirtualSafetyCar)
		{
			Banner = {TEXT("VIRTUAL SAFETY CAR"), TEXT("DELTA TIME ENFORCED"), UIStyle::Yellow, UIStyle::Ink};
		}
		else if (!Snapshot.bRaceStarted)
		{
			Banner = {TEXT("START"), FString::Printf(TEXT("LIGHTS %d / 5"), Snapshot.StartLights), UIStyle::Card, UIStyle::Text};
		}
		else if (Yellow.Num() > 0)
		{
			Banner = {bDouble ? TEXT("DOUBLE YELLOW") : TEXT("YELLOW FLAG"), TEXT("MARSHAL SECTOR ") + FString::Join(Yellow, TEXT(", ")),
				UIStyle::Card, UIStyle::Yellow};
		}
		else
		{
			Banner = {TEXT("TRACK CLEAR"), TEXT("GREEN"), UIStyle::Card, UIStyle::Green};
		}
		if (Snapshot.bLappedCarsReleased && Snapshot.State == EUISimRaceState::SafetyCar)
		{
			Banner.Sub += TEXT(" · LAPPED CARS MAY OVERTAKE");
		}
		if (Snapshot.bPitEntryClosed)
		{
			Banner.Sub += TEXT(" · PIT ENTRY CLOSED");
		}
		return Banner;
	}
}

TSharedRef<SWidget> UUIDeskWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible); // draws only, except the sim speed buttons
		WidgetTree->RootWidget = Canvas;

		SpeedArea = WidgetTree->ConstructWidget<UBorder>();
		SpeedArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f)); // visible, so it takes the clicks
		UCanvasPanelSlot* AreaSlot = Canvas->AddChildToCanvas(SpeedArea);
		AreaSlot->SetPosition(SpeedBox.Min);
		AreaSlot->SetSize(SpeedBox.GetSize());
	}
	return Super::RebuildWidget();
}

int32 UUIDeskWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	static const FSlateRoundedBoxBrush Round16(FLinearColor::White, 16.0f);
	static const FSlateRoundedBoxBrush Round15(FLinearColor::White, 15.0f);
	static const FSlateRoundedBoxBrush Capsule(FLinearColor::White, 36.0f); // radius of half the height
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	auto Box = [&](const FVector2D& Min, const FVector2D& Max, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Max - Min, FSlateLayoutTransform(1.0f, Min)),
			Brush, ESlateDrawEffect::None, Color);
	};
	// A card of the design: fill with a border one unit wide.
	auto CardBox = [&](const FBox2D& Rect, const FLinearColor& Fill, bool bCapsule = false)
	{
		Box(Rect.Min, Rect.Max, bCapsule ? &Capsule : &Round16, UIStyle::CardBorder);
		Box(Rect.Min + FVector2D(1.0, 1.0), Rect.Max - FVector2D(1.0, 1.0), bCapsule ? &Capsule : &Round15, Fill);
	};
	auto Text = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2D(900.0, 40.0),
			FSlateLayoutTransform(1.0f, At)), String, Font, ESlateDrawEffect::None, Color);
	};
	auto Width = [&](const FString& String, const FSlateFontInfo& Font) { return Measure->Measure(String, Font).X; };

	// Background around the window for the 3D picture.
	const FBox2D& Feed = UIStyle::Feed;
	Box(FVector2D(0.0, 0.0), FVector2D(Size.X, Feed.Min.Y), White, UIStyle::Background);
	Box(FVector2D(0.0, Feed.Max.Y), Size, White, UIStyle::Background);
	Box(FVector2D(0.0, Feed.Min.Y), FVector2D(Feed.Min.X, Feed.Max.Y), White, UIStyle::Background);
	Box(FVector2D(Feed.Max.X, Feed.Min.Y), FVector2D(Size.X, Feed.Max.Y), White, UIStyle::Background);

	for (const FBox2D* Card : {&UIStyle::TopTitle, &UIStyle::TopSession, &UIStyle::TopWeather, &UIStyle::TimingCard, &UIStyle::LowerLeftCard,
		&UIStyle::MapCard, &UIStyle::IncidentCard})
	{
		CardBox(*Card, UIStyle::Card);
	}
	// The video card leaves the window for the 3D picture open: four strips around it.
	const FBox2D& Video = UIStyle::VideoCard;
	Box(Video.Min, FVector2D(Video.Max.X, Feed.Min.Y), White, UIStyle::Card);
	Box(FVector2D(Video.Min.X, Feed.Max.Y), Video.Max, White, UIStyle::Card);
	Box(FVector2D(Video.Min.X, Feed.Min.Y), FVector2D(Feed.Min.X, Feed.Max.Y), White, UIStyle::Card);
	Box(FVector2D(Feed.Max.X, Feed.Min.Y), FVector2D(Video.Max.X, Feed.Max.Y), White, UIStyle::Card);

	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted())
	{
		CardBox(UIStyle::TopBanner, UIStyle::Card, true);
		return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
	}
	const FUISimSession& Session = Race->Session();
	const FUISimSnapshot& Snapshot = Session.Snapshot();
	const FSlateFontInfo LabelFont = UIStyle::Font(TEXT("Regular"), 7);
	const FSlateFontInfo SubFont = UIStyle::Font(TEXT("Regular"), 8);

	// Title.
	const FSlateFontInfo TitleFont = UIStyle::Font(TEXT("Bold"), 13, 40);
	const FVector2D TitleAt = UIStyle::TopTitle.Min + FVector2D(15.0, 19.0);
	Text(TitleAt, TEXT("RACE CONTROL"), TitleFont, UIStyle::Text);
	Text(TitleAt + FVector2D(Width(TEXT("RACE CONTROL"), TitleFont) + 8.0, 0.0), TEXT("/ SIM"), UIStyle::Font(TEXT("Regular"), 13, 40), UIStyle::Mute);
	Text(TitleAt + FVector2D(0.0, 21.0), FString::Printf(TEXT("%s · RACE"), *Session.TrackName().ToUpper()), SubFont, UIStyle::Mute);

	// Status banner: label and sub line centred as one group.
	const FBanner Banner = MakeBanner(Snapshot);
	CardBox(UIStyle::TopBanner, Banner.Fill, true);
	const FSlateFontInfo BannerFont = UIStyle::Font(TEXT("Bold"), 21, 60);
	const double LabelWidth = Width(Banner.Label, BannerFont);
	const double GroupLeft = UIStyle::TopBanner.GetCenter().X - (LabelWidth + 12.0 + Width(Banner.Sub, SubFont)) / 2.0;
	Text(FVector2D(GroupLeft, UIStyle::TopBanner.Min.Y + 20.0), Banner.Label, BannerFont, Banner.Ink);
	Text(FVector2D(GroupLeft + LabelWidth + 12.0, UIStyle::TopBanner.Min.Y + 30.0), Banner.Sub, SubFont, Banner.Ink);

	// Lap, session clock, sim speed.
	const FSlateFontInfo ValueFont = UIStyle::Font(TEXT("Bold"), 16);
	const FVector2D LapAt = UIStyle::TopSession.Min + FVector2D(16.0, 16.0);
	const FString Lap = FString::FromInt(Snapshot.LeaderLap);
	const FString Total = FString::Printf(TEXT(" / %d"), Snapshot.TotalLaps);
	Text(LapAt, TEXT("LAP"), LabelFont, UIStyle::Mute);
	Text(LapAt + FVector2D(0.0, 16.0), Lap, ValueFont, UIStyle::Text);
	Text(LapAt + FVector2D(Width(Lap, ValueFont), 16.0), Total, UIStyle::Font(TEXT("Regular"), 16), UIStyle::Mute);
	const FVector2D ClockAt = LapAt + FVector2D(Width(Lap, ValueFont) + Width(Total, UIStyle::Font(TEXT("Regular"), 16)) + 22.0, 0.0);
	const int32 Seconds = FMath::FloorToInt32(Snapshot.Time);
	Text(ClockAt, TEXT("SESSION"), LabelFont, UIStyle::Mute);
	Text(ClockAt + FVector2D(0.0, 16.0), FString::Printf(TEXT("%d:%02d:%02d"), Seconds / 3600, Seconds / 60 % 60, Seconds % 60), ValueFont,
		UIStyle::Text);
	Text(FVector2D(SpeedBox.Min.X, SpeedBox.Min.Y - 17.0), TEXT("SIM SPEED"), LabelFont, UIStyle::Mute);
	Box(SpeedBox.Min, SpeedBox.Max, &Round16, UIStyle::CardBorder);
	Box(SpeedBox.Min + FVector2D(1.0, 1.0), SpeedBox.Max - FVector2D(1.0, 1.0), &Round15, UIStyle::Rgb(14, 15, 18));
	const FSlateFontInfo ButtonFont = UIStyle::Font(TEXT("Bold"), 8);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SpeedButtons); ++Index)
	{
		const FBox2D Rect = SpeedButtonRect(Index);
		const bool bActive = FMath::IsNearlyEqual(Race->TimeScale(), SpeedButtons[Index].Scale);
		if (bActive)
		{
			Box(Rect.Min, Rect.Max, &Round15, UIStyle::Text);
		}
		Text(FVector2D(Rect.GetCenter().X - Width(SpeedButtons[Index].Label, ButtonFont) / 2.0, Rect.Min.Y + 5.0), SpeedButtons[Index].Label,
			ButtonFont, bActive ? UIStyle::Ink : UIStyle::Text);
	}
	// A speed the buttons do not offer (keys, scripts): shown beside the label.
	bool bListed = false;
	for (const FSpeedButton& Button : SpeedButtons)
	{
		bListed |= FMath::IsNearlyEqual(Race->TimeScale(), Button.Scale);
	}
	if (!bListed)
	{
		Text(FVector2D(SpeedBox.Min.X + 62.0, SpeedBox.Min.Y - 17.0), FString::Printf(TEXT("%g×"), Race->TimeScale()), LabelFont, UIStyle::Yellow);
	}

	// Weather: five fields spread over the card. The core has no temperatures or wind; it shows what it simulates.
	const double RainSoon = FMath::Max3(Session.ForecastRain(Snapshot.Time + 300.0), Session.ForecastRain(Snapshot.Time + 600.0),
		Session.ForecastRain(Snapshot.Time + 900.0));
	struct FField
	{
		FString Label;
		FString Value;
		FLinearColor Color;
	};
	const FField Fields[] = {
		{TEXT("WEATHER"), Session.WeatherName().ToUpper(), UIStyle::Text},
		{TEXT("RAIN"), FString::Printf(TEXT("%.0f%%"), Snapshot.Rain * 100.0), UIStyle::Text},
		{TEXT("LINE WET"), FString::Printf(TEXT("%.0f%%"), Snapshot.LineWetness * 100.0), UIStyle::Text},
		{Snapshot.StandingWater > 0.0 ? TEXT("STANDING WATER") : TEXT("OFF LINE"), FString::Printf(TEXT("%.0f%%"),
			(Snapshot.StandingWater > 0.0 ? Snapshot.StandingWater : Snapshot.TrackWetness) * 100.0),
			Snapshot.StandingWater > 0.0 ? UIStyle::Red : UIStyle::Text},
		{TEXT("RAIN 15 MIN"), FString::Printf(TEXT("%.0f%%"), RainSoon * 100.0), RainSoon > 0.4 ? UIStyle::Yellow : UIStyle::Text}};
	const FSlateFontInfo FieldFont = UIStyle::Font(TEXT("Bold"), 9);
	double Used = 0.0;
	TArray<double, TInlineAllocator<5>> Widths;
	for (const FField& Field : Fields)
	{
		Widths.Add(FMath::Max(Width(Field.Label, LabelFont), Width(Field.Value, FieldFont)));
		Used += Widths.Last();
	}
	const double Spread = (UIStyle::TopWeather.GetSize().X - 28.0 - Used) / (UE_ARRAY_COUNT(Fields) - 1);
	double Left = UIStyle::TopWeather.Min.X + 14.0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Fields); ++Index)
	{
		Text(FVector2D(Left, UIStyle::TopWeather.Min.Y + 21.0), Fields[Index].Label, LabelFont, UIStyle::Mute);
		Text(FVector2D(Left, UIStyle::TopWeather.Min.Y + 35.0), Fields[Index].Value, FieldFont, Fields[Index].Color);
		Left += Widths[Index] + Spread;
	}

	// Video card: title and the routed camera.
	Text(Video.Min + FVector2D(16.0, 13.0), TEXT("Video matrix"), UIStyle::Font(TEXT("Bold"), 11), UIStyle::Text);
	const FString Camera = Race->ActiveCameraName().ToUpper();
	const FSlateFontInfo CameraFont = UIStyle::Font(TEXT("Bold"), 8);
	const double CameraLeft = Video.Max.X - 16.0 - Width(Camera, CameraFont);
	Text(FVector2D(CameraLeft, Video.Min.Y + 16.0), Camera, CameraFont, UIStyle::Text);
	Text(FVector2D(CameraLeft - 5.0 - Width(TEXT("ROUTED TO MAIN:"), SubFont), Video.Min.Y + 16.0), TEXT("ROUTED TO MAIN:"), SubFont, UIStyle::Mute);

	if (!bLogged && SpeedArea && SpeedArea->GetCachedGeometry().GetLocalSize().X > 0.0)
	{
		// Viewport pixels of the sim speed buttons, for -UISimScript checks (click x y).
		bLogged = true;
		const UWorld* World = GetWorld();
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
		if (ViewportWidget.IsValid())
		{
			const FVector2D Origin = FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(SpeedButtons); ++Index)
			{
				const FVector2D At = FVector2D(AllottedGeometry.LocalToAbsolute(SpeedButtonRect(Index).GetCenter())) - Origin;
				UE_LOG(LogUISim, Display, TEXT("Top bar: sim speed x%g at %.0f %.0f"), SpeedButtons[Index].Scale, At.X, At.Y);
			}
		}
	}
	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}

FReply UUIDeskWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (Race && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && Race->Session().IsStarted())
	{
		const FVector2D Local = FVector2D(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(SpeedButtons); ++Index)
		{
			if (SpeedButtonRect(Index).IsInside(Local))
			{
				FUICommand Command;
				Command.Type = EUICommand::SetTimeScale;
				Command.Value = SpeedButtons[Index].Scale;
				Race->Execute(Command);
				return FReply::Handled();
			}
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
