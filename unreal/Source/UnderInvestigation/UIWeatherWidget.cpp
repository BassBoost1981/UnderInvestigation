#include "UIWeatherWidget.h"

#include "UIRaceDirector.h"

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
		Panel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f));
		Panel->SetPadding(FMargin(10.0f));
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.0f)); // top centre, between tower and incident list
		PanelSlot->SetAlignment(FVector2D(0.5, 0.0));
		PanelSlot->SetPosition(FVector2D(120.0, 24.0));
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
		// Rain overlay: a darker tint and falling streaks, drawn below the weather panel.
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), FCoreStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None, FLinearColor(0.05f, 0.07f, 0.12f, static_cast<float>(0.35 * Rain)));
		++Layer;
		const double Now = Race->Session().Snapshot().Time;
		const int32 Streaks = FMath::RoundToInt(MaxStreaks * Rain);
		for (int32 Index = 0; Index < Streaks; ++Index)
		{
			// Fixed pseudo-random columns, falling with the race clock.
			const double X = FMath::Frac(FMath::Sin(Index * 12.9898) * 43758.5453) * Size.X;
			const double Y = FMath::Frac(FMath::Frac(FMath::Sin(Index * 78.233) * 12543.123) + Now * 0.9) * Size.Y;
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(),
				{FVector2D(X, Y), FVector2D(X - 4.0, Y + 22.0)}, ESlateDrawEffect::None,
				FLinearColor(0.75f, 0.8f, 0.9f, static_cast<float>(0.25 + 0.35 * Rain)), true, 1.0f);
		}
		++Layer;
	}
	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, Layer, InWidgetStyle, bParentEnabled);
}
