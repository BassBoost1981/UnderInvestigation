#include "UIMessagesWidget.h"

#include "UIRaceDirector.h"
#include "UIStyle.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

namespace
{
	constexpr double ListTop = 38.0, RowHeight = 22.0, TextLeft = 90.0;

	// The presentation layer words the structured reports from the core.
	FUIMessage RadioMessage(const FUISimRadio& Radio, const FString& Code)
	{
		FUIMessage Message;
		Message.Time = Radio.Time;
		const FString Post = FString::Printf(TEXT("MARSHAL POST %d: "), Radio.MarshalPost);
		switch (Radio.Type)
		{
		case EUISimRadio::Spin:
			Message.Text = Post + FString::Printf(TEXT("%s SPUN, CONTINUING"), *Code);
			Message.Color = UIStyle::Mute;
			break;
		case EUISimRadio::CarOffTrack:
			Message.Text = Post + FString::Printf(TEXT("%s STOPPED OFF TRACK, DRIVER OK"), *Code);
			Message.Color = UIStyle::Yellow;
			break;
		case EUISimRadio::CarOnTrack:
			Message.Text = Post + FString::Printf(TEXT("%s STOPPED ON TRACK · DEBRIS"), *Code);
			Message.Color = UIStyle::Red;
			break;
		case EUISimRadio::RecoveryNeedsFlag:
			Message.Text = Post + FString::Printf(TEXT("RECOVERY OF %s READY, NEEDS %s"), *Code, Radio.bOnTrack ? TEXT("DOUBLE YELLOW") : TEXT("YELLOW"));
			Message.Color = UIStyle::Yellow;
			break;
		case EUISimRadio::TrackClear:
			Message.Text = Post + FString::Printf(TEXT("%s RECOVERED, TRACK CLEAR"), *Code);
			Message.Color = UIStyle::Green;
			break;
		}
		return Message;
	}
}

int32 UUIMessagesWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted())
	{
		return Layer;
	}
	static const FSlateRoundedBoxBrush Bar(FLinearColor::White, 2.0f);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FBox2D& Card = UIStyle::MessageCard;
	auto Box = [&](const FBox2D& Rect, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(Rect.GetSize(),
			FSlateLayoutTransform(1.0f, Card.Min + Rect.Min)), Brush, ESlateDrawEffect::None, Color);
	};
	auto Text = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(FVector2D(Card.GetSize().X, 30.0),
			FSlateLayoutTransform(1.0f, Card.Min + At)), String, Font, ESlateDrawEffect::None, Color);
	};

	// Own decisions and marshal radio, newest first (the radio of a moment before a decision at the same time).
	const FUISimSession& Session = Race->Session();
	TArray<FUIMessage> All = Race->Messages();
	for (const FUISimRadio& Radio : Session.Radio())
	{
		All.Add(RadioMessage(Radio, Session.Entries()[Radio.Car].Code.ToUpper()));
	}
	All.StableSort([](const FUIMessage& A, const FUIMessage& B) { return A.Time < B.Time; });

	++Layer;
	Text(FVector2D(16.0, 13.0), TEXT("Race control messages"), UIStyle::Font(TEXT("Bold"), 11), UIStyle::Text);
	const FSlateFontInfo Font = UIStyle::Font(TEXT("Regular"), 8);
	const double TextWidth = Card.GetSize().X - TextLeft - 12.0;
	const int32 Rows = FMath::FloorToInt32((Card.GetSize().Y - ListTop) / RowHeight);
	if (All.IsEmpty())
	{
		Text(FVector2D(12.0, ListTop + 4.0), TEXT("No messages yet."), Font, UIStyle::Mute);
	}
	for (int32 Row = 0; Row < Rows && Row < All.Num(); ++Row)
	{
		const FUIMessage& Message = All[All.Num() - 1 - Row];
		const double Top = ListTop + Row * RowHeight;
		const int32 Seconds = FMath::Max(0, FMath::FloorToInt32(Message.Time));
		const FString Clock = Seconds >= 3600 ? FString::Printf(TEXT("%d:%02d:%02d"), Seconds / 3600, Seconds / 60 % 60, Seconds % 60)
			: FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
		FString Line = Message.Text;
		if (Measure->Measure(Line, Font).X > TextWidth)
		{
			while (Line.Len() > 1 && Measure->Measure(Line + TEXT("…"), Font).X > TextWidth)
			{
				Line.LeftChopInline(1);
			}
			Line += TEXT("…");
		}
		Text(FVector2D(12.0, Top + 4.0), Clock, Font, UIStyle::Mute);
		Box(FBox2D(FVector2D(78.0, Top + 4.0), FVector2D(82.0, Top + 18.0)), &Bar, Message.Color);
		Text(FVector2D(TextLeft, Top + 4.0), Line, Font, UIStyle::Text);
		Box(FBox2D(FVector2D(1.0, Top + RowHeight - 1.0), FVector2D(Card.GetSize().X - 1.0, Top + RowHeight)), White, UIStyle::Rgb(29, 30, 34));
	}
	return Layer + 2;
}
