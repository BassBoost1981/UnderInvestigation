#include "UIWeatherWidget.h"

#include "UIRaceDirector.h"
#include "UIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr double RainHazardLookAhead = 3600.0; // seconds of forecast checked for the Rain Hazard
	constexpr int32 MaxStreaks = 220;              // rain streaks at full rain
}

TSharedRef<SWidget> UUIWeatherWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Canvas->SetVisibility(ESlateVisibility::HitTestInvisible); // the rain overlay must not catch clicks
		WidgetTree->RootWidget = Canvas;
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrushColor(FLinearColor::Transparent); // the top bar of the desk is the background
		Panel->SetPadding(FMargin(8.0f));
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetAlignment(FVector2D(0.5, 0.0)); // centred in the top bar
		PanelSlot->SetPosition(FVector2D(UIStyle::TopBar.GetCenter().X, UIStyle::TopBar.Min.Y));
		Text = WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 11));
		Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.85f, 1.0f)));
		Panel->SetContent(Text);
	}
	return Super::RebuildWidget();
}

void UUIWeatherWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted() || !Text)
	{
		return;
	}
	const FUISimSession& Session = Race->Session();
	const FUISimSnapshot& S = Session.Snapshot();
	double Hazard = 0.0;
	for (double T = S.Time; T <= S.Time + RainHazardLookAhead; T += 60.0)
	{
		Hazard = FMath::Max(Hazard, Session.ForecastRain(T));
	}
	FString Line = FString::Printf(TEXT("WETTER: %s   Regen %3.0f %%"), *Session.WeatherName(), S.Rain * 100.0);
	Line += FString::Printf(TEXT("\nNaesse Linie %3.0f %%, daneben %3.0f %%"), S.LineWetness * 100.0, S.TrackWetness * 100.0);
	if (S.StandingWater > 0.0)
	{
		Line += FString::Printf(TEXT("\nSTEHENDES WASSER %3.0f %% (Aquaplaning)"), S.StandingWater * 100.0);
	}
	Line += FString::Printf(TEXT("\nVorhersage +5/+10/+15 min: %3.0f / %3.0f / %3.0f %%"), Session.ForecastRain(S.Time + 300.0) * 100.0,
		Session.ForecastRain(S.Time + 600.0) * 100.0, Session.ForecastRain(S.Time + 900.0) * 100.0);
	if (Hazard > 0.4) // B1.5.11: more than 40 % rain expected (sim: forecast intensity as the stand-in)
	{
		Line += TEXT("\nRAIN HAZARD");
	}
	if (S.bWetTyresMandated)
	{
		Line += TEXT("\nREGENREIFEN-PFLICHT (bis das SC hereinkommt)");
	}
	Text->SetText(FText::FromString(Line));
}

int32 UUIWeatherWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const AUIRaceDirector* Race = Director.Get();
	const double Rain = Race && Race->Session().IsStarted() ? Race->Session().Snapshot().Rain : 0.0;
	int32 Layer = LayerId;
	if (Rain > 0.0)
	{
		// Rain overlay on the camera picture: a darker tint and falling streaks.
		const FVector2D Size = UIStyle::Feed.GetSize();
		const FPaintGeometry Picture = AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(1.0f, UIStyle::Feed.Min));
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Picture, FCoreStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None, FLinearColor(0.05f, 0.07f, 0.12f, static_cast<float>(0.35 * Rain)));
		++Layer;
		const double Now = Race->Session().Snapshot().Time;
		const int32 Streaks = FMath::RoundToInt(MaxStreaks * Rain);
		for (int32 Index = 0; Index < Streaks; ++Index)
		{
			// Fixed pseudo-random columns, falling with the race clock.
			const double X = FMath::Frac(FMath::Sin(Index * 12.9898) * 43758.5453) * Size.X;
			const double Y = FMath::Frac(FMath::Frac(FMath::Sin(Index * 78.233) * 12543.123) + Now * 0.9) * (Size.Y - 22.0);
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, Picture,
				{FVector2D(FMath::Max(X, 4.0), Y), FVector2D(FMath::Max(X, 4.0) - 4.0, Y + 22.0)}, ESlateDrawEffect::None,
				FLinearColor(0.75f, 0.8f, 0.9f, static_cast<float>(0.25 + 0.35 * Rain)), true, 1.0f);
		}
		++Layer;
	}
	if (Race && Race->Session().IsStarted() && Race->Session().Snapshot().bGridStart)
	{
		// Start gantry (M10.7): five lights, red one after the other, all out = go (B5.7.1).
		const int32 Lit = Race->Session().Snapshot().StartLights;
		const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
		const FVector2D LightSize(34.0, 34.0);
		const double Step = LightSize.X + 12.0;
		const FVector2D Origin(UIStyle::Feed.GetCenter().X - (5.0 * Step - 12.0) / 2.0, UIStyle::Feed.Min.Y + 60.0);
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2D(5.0 * Step + 12.0, LightSize.Y + 24.0),
			FSlateLayoutTransform(1.0f, Origin - FVector2D(12.0, 12.0))), White, ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
		for (int32 Index = 0; Index < 5; ++Index)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(LightSize,
				FSlateLayoutTransform(1.0f, Origin + FVector2D(Index * Step, 0.0))), White, ESlateDrawEffect::None,
				Index < Lit ? FLinearColor(1.0f, 0.05f, 0.03f) : FLinearColor(0.12f, 0.02f, 0.02f));
		}
		Layer += 2;
	}
	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, Layer, InWidgetStyle, bParentEnabled);
}
