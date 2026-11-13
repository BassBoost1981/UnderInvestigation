#include "UIWeatherWidget.h"

#include "UIRaceDirector.h"
#include "UIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 MaxStreaks = 220; // rain streaks at full rain
}

TSharedRef<SWidget> UUIWeatherWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Canvas->SetVisibility(ESlateVisibility::HitTestInvisible); // the rain overlay must not catch clicks
		WidgetTree->RootWidget = Canvas;
	}
	return Super::RebuildWidget();
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
