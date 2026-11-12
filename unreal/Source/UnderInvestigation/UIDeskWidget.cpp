#include "UIDeskWidget.h"

#include "UIRaceDirector.h"
#include "UIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

TSharedRef<SWidget> UUIDeskWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Canvas->SetVisibility(ESlateVisibility::HitTestInvisible); // draws only
		WidgetTree->RootWidget = Canvas;
	}
	return Super::RebuildWidget();
}

int32 UUIDeskWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	static const FSlateRoundedBoxBrush Rounded(FLinearColor::White, UIStyle::CardRadius);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	auto Box = [&](const FVector2D& Min, const FVector2D& Max, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Max - Min, FSlateLayoutTransform(1.0f, Min)),
			Brush, ESlateDrawEffect::None, Color);
	};
	auto Text = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2D(600.0, 30.0),
			FSlateLayoutTransform(1.0f, At)), String, Font, ESlateDrawEffect::None, Color);
	};

	// Background around the window for the 3D picture.
	const FBox2D& Feed = UIStyle::Feed;
	Box(FVector2D(0.0, 0.0), FVector2D(Size.X, Feed.Min.Y), White, UIStyle::Background);
	Box(FVector2D(0.0, Feed.Max.Y), Size, White, UIStyle::Background);
	Box(FVector2D(0.0, Feed.Min.Y), FVector2D(Feed.Min.X, Feed.Max.Y), White, UIStyle::Background);
	Box(FVector2D(Feed.Max.X, Feed.Min.Y), FVector2D(Size.X, Feed.Max.Y), White, UIStyle::Background);

	for (const FBox2D* Card : {&UIStyle::TopBar, &UIStyle::TimingCard, &UIStyle::MapCard, &UIStyle::ControlCard, &UIStyle::IncidentCard})
	{
		Box(Card->Min, Card->Max, &Rounded, UIStyle::Card);
	}
	Box(UIStyle::FeedCard.Min, FVector2D(UIStyle::FeedCard.Max.X, Feed.Min.Y), White, UIStyle::Card); // title strip of the picture

	const AUIRaceDirector* Race = Director.Get();
	if (Race && Race->Session().IsStarted())
	{
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FSlateFontInfo TitleFont = UIStyle::Font(TEXT("Bold"), 15);
		const FSlateFontInfo SmallFont = UIStyle::Font(TEXT("Regular"), 9);
		const FVector2D Top = UIStyle::TopBar.Min + FVector2D(16.0, 12.0);
		Text(Top, TEXT("RACE CONTROL"), TitleFont, UIStyle::Text);
		Text(Top + FVector2D(Measure->Measure(TEXT("RACE CONTROL"), TitleFont).X + 8.0, 0.0), TEXT("/ SIM"), TitleFont, UIStyle::Mute);
		Text(Top + FVector2D(0.0, 26.0), FString::Printf(TEXT("%s  -  %d RUNDEN"), *Race->Session().TrackName().ToUpper(),
			Race->Session().Snapshot().TotalLaps), SmallFont, UIStyle::Mute);
		Text(UIStyle::FeedCard.Min + FVector2D(14.0, 8.0), TEXT("KAMERABILD"), UIStyle::Font(TEXT("Bold"), 10), UIStyle::Text);
		const FString Camera = Race->ActiveCameraName().ToUpper();
		Text(FVector2D(UIStyle::FeedCard.Max.X - 14.0 - Measure->Measure(Camera, SmallFont).X, UIStyle::FeedCard.Min.Y + 9.0), Camera,
			SmallFont, UIStyle::Mute);
	}
	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}
