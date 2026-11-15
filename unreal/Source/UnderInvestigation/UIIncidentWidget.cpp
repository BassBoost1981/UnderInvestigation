#include "UIIncidentWidget.h"

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
	// Measures of the design, relative to the card.
	constexpr double ListTop = 38.0, EntryHeight = 74.0, ButtonTop = 44.0, ButtonHeight = 22.0;
	constexpr int32 MaxEntries = 3;

	const FSlateFontInfo& ButtonFont()
	{
		static const FSlateFontInfo Font = UIStyle::Font(TEXT("Bold"), 7);
		return Font;
	}
}

TSharedRef<SWidget> UUIIncidentWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		CardArea = WidgetTree->ConstructWidget<UBorder>();
		CardArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f)); // visible, so it takes the clicks
		UCanvasPanelSlot* AreaSlot = Canvas->AddChildToCanvas(CardArea);
		AreaSlot->SetPosition(UIStyle::IncidentCard.Min);
		AreaSlot->SetSize(UIStyle::IncidentCard.GetSize());
	}
	return Super::RebuildWidget();
}

TArray<UUIIncidentWidget::FEntry> UUIIncidentWidget::Entries(int32& OutOpen) const
{
	TArray<FEntry> List;
	OutOpen = 0;
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted())
	{
		return List;
	}
	const FUISimSession& Session = Race->Session();
	const FUISimSnapshot& Snapshot = Session.Snapshot();
	const TArray<FUISimIncident>& Incidents = Session.Incidents();
	const TArray<FUISimEntry>& Cars = Session.Entries();

	// Open incidents, newest first; the window of three follows the selected one.
	TArray<int32> Open;
	for (int32 Index = Incidents.Num() - 1; Index >= 0; --Index)
	{
		if (!Session.VerdictFor(Incidents[Index].Id))
		{
			Open.Add(Index);
		}
	}
	OutOpen = Open.Num();
	const int32 SelectedAt = Open.IndexOfByKey(Race->SelectedIncident());
	const int32 First = FMath::Clamp(SelectedAt - (MaxEntries - 1), 0, FMath::Max(0, Open.Num() - MaxEntries));
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	for (int32 Place = 0; Place < MaxEntries && Open.IsValidIndex(First + Place); ++Place)
	{
		const int32 Index = Open[First + Place];
		const FUISimIncident& Incident = Incidents[Index];
		FEntry Entry;
		Entry.Index = Index;
		Entry.Top = ListTop + Place * EntryHeight;
		const FString CarCodes = Incident.Car == Incident.OtherCar ? Cars[Incident.Car].Code.ToUpper()
			: Cars[Incident.Car].Code.ToUpper() + TEXT(" / ") + Cars[Incident.OtherCar].Code.ToUpper();
		Entry.Title = FString::Printf(TEXT("%s · %s"), *AUIRaceDirector::IncidentTypeName(Incident.Type), *CarCodes);
		Entry.Detail = FString::Printf(TEXT("#%d · %s"), Incident.Id, *AUIRaceDirector::FormatRaceTime(Incident.Time));

		const bool bInReview = Race->IsReviewing() && Race->ReviewedIncident() == Index;
		const bool bHeard = Session.IsHeard(Incident.Id);
		const double Wait = Session.HearingUntil(Incident.Id) - Snapshot.Time;
		if (bHeard && !Snapshot.bFinished && Wait > 0.0)
		{
			Entry.State = FString::Printf(TEXT("HEARING · %d:%02d"), FMath::CeilToInt32(Wait) / 60, FMath::CeilToInt32(Wait) % 60);
			Entry.StateColor = UIStyle::Yellow;
		}
		else if (bInReview)
		{
			Entry.State = TEXT("IN REVIEW");
			Entry.StateColor = UIStyle::Yellow;
		}
		else if (bHeard)
		{
			Entry.State = TEXT("HEARD");
			Entry.StateColor = UIStyle::Yellow;
		}
		else if (Race->IsInvestigated(Incident.Id))
		{
			Entry.State = TEXT("UNDER INVESTIGATION");
			Entry.StateColor = UIStyle::Yellow;
		}
		else
		{
			Entry.State = Race->IsNoted(Incident.Id) ? TEXT("NOTED") : TEXT("NEW");
			Entry.StateColor = Race->IsNoted(Incident.Id) ? UIStyle::Mute : UIStyle::Text;
		}

		// Capsule buttons, left to right.
		double Left = 12.0;
		auto Add = [&](const FString& Label, EUICommand Type, int32 CommandIndex, const FLinearColor& Color, bool bEnabled = true)
		{
			FButton Button;
			Button.Label = Label;
			Button.Command.Type = Type;
			Button.Command.Index = CommandIndex;
			Button.Color = Color;
			Button.bEnabled = bEnabled && Race->CanExecute(Button.Command);
			const double Width = Measure->Measure(Label, ButtonFont()).X + 20.0;
			Button.Rect = FBox2D(FVector2D(Left, Entry.Top + ButtonTop), FVector2D(Left + Width, Entry.Top + ButtonTop + ButtonHeight));
			Left += Width + 4.0;
			Entry.Buttons.Add(Button);
		};
		Add(TEXT("CAM"), EUICommand::FollowCar, Incident.Car, UIStyle::Mute);
		if (!Race->IsNoted(Incident.Id) && !Race->IsInvestigated(Incident.Id))
		{
			Add(TEXT("NOTED"), EUICommand::NoteIncident, Index, UIStyle::Text);
		}
		Add(TEXT("INVESTIGATE"), EUICommand::StartReview, Index, UIStyle::Yellow, !bInReview);
		// While a review runs, the hearing belongs to the review card (the command acts on the reviewed incident).
		Add(TEXT("HEARING"), EUICommand::HoldHearing, Index, UIStyle::Yellow, !Race->IsReviewing());
		Add(TEXT("DISMISS"), EUICommand::DismissIncident, Index, UIStyle::Text);
		List.Add(Entry);
	}
	return List;
}

int32 UUIIncidentWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted() || !CardArea || CardArea->GetCachedGeometry().GetLocalSize().X <= 0.0)
	{
		return Layer;
	}
	static const FSlateRoundedBoxBrush Capsule(FLinearColor::White, 11.0f);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FGeometry& Card = CardArea->GetCachedGeometry();
	const double Width = Card.GetLocalSize().X;
	auto Box = [&](const FBox2D& Rect, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Card.ToPaintGeometry(Rect.GetSize(), FSlateLayoutTransform(1.0f, Rect.Min)), Brush,
			ESlateDrawEffect::None, Color);
	};
	auto Text = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, Card.ToPaintGeometry(FVector2D(Width, 30.0), FSlateLayoutTransform(1.0f, At)),
			String, Font, ESlateDrawEffect::None, Color);
	};
	auto Right = [&](double Y, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		Text(FVector2D(Width - 16.0 - Measure->Measure(String, Font).X, Y), String, Font, Color);
	};

	int32 Open = 0;
	const TArray<FEntry> List = Entries(Open);
	++Layer;
	Text(FVector2D(16.0, 13.0), TEXT("Incidents"), UIStyle::Font(TEXT("Bold"), 11), UIStyle::Text);
	Right(16.0, FString::Printf(TEXT("%d OPEN"), Open), UIStyle::Font(TEXT("Regular"), 8), UIStyle::Mute);
	if (List.IsEmpty())
	{
		Text(FVector2D(12.0, ListTop + 12.0), TEXT("No open incidents."), UIStyle::Font(TEXT("Regular"), 8), UIStyle::Mute);
	}
	const FSlateFontInfo TitleFont = UIStyle::Font(TEXT("Bold"), 9);
	const FSlateFontInfo DetailFont = UIStyle::Font(TEXT("Regular"), 8);
	const FSlateFontInfo StateFont = UIStyle::Font(TEXT("Bold"), 7);
	FString Targets;
	for (const FEntry& Entry : List)
	{
		if (Entry.Index == Race->SelectedIncident())
		{
			Box(FBox2D(FVector2D(1.0, Entry.Top), FVector2D(Width - 1.0, Entry.Top + EntryHeight)), White, UIStyle::Rgb(28, 29, 33));
		}
		Box(FBox2D(FVector2D(1.0, Entry.Top + EntryHeight - 1.0), FVector2D(Width - 1.0, Entry.Top + EntryHeight)), White, UIStyle::Rgb(29, 30, 34));
		Text(FVector2D(12.0, Entry.Top + 8.0), Entry.Title, TitleFont, UIStyle::Text);
		Right(Entry.Top + 10.0, Entry.State, StateFont, Entry.StateColor);
		Text(FVector2D(12.0, Entry.Top + 25.0), Entry.Detail, DetailFont, UIStyle::Mute);
		for (const FButton& Button : Entry.Buttons)
		{
			Box(Button.Rect, &Capsule, UIStyle::Rgb(43, 44, 48));
			Box(Button.Rect.ExpandBy(-1.0), &Capsule, Button.bEnabled ? UIStyle::Rgb(33, 34, 38) : UIStyle::Rgb(24, 25, 29));
			FLinearColor Ink = Button.Color;
			Ink.A = Button.bEnabled ? 1.0f : 0.35f;
			Text(FVector2D(Button.Rect.Min.X + 10.0, Button.Rect.Min.Y + 4.0), Button.Label, ButtonFont(), Ink);
			Targets += FString::Printf(TEXT("%d %s %.0f;"), Entry.Index, *Button.Label, Button.Rect.Min.X);
		}
	}
	Layer += 2;

	if (Targets != LoggedTargets)
	{
		// Viewport pixels of the buttons, for -UISimScript checks (click x y); written again when they change.
		LoggedTargets = Targets;
		const UWorld* World = GetWorld();
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
		if (ViewportWidget.IsValid())
		{
			const FVector2D Origin = FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
			for (const FEntry& Entry : List)
			{
				for (const FButton& Button : Entry.Buttons)
				{
					const FVector2D At = FVector2D(Card.LocalToAbsolute(Button.Rect.GetCenter())) - Origin;
					UE_LOG(LogUISim, Display, TEXT("Incidents: #%d '%s' at %.0f %.0f"), Race->Session().Incidents()[Entry.Index].Id,
						*Button.Label, At.X, At.Y);
				}
			}
		}
	}
	return Layer;
}

FReply UUIIncidentWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (!Race || !CardArea || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Local = FVector2D(CardArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	int32 Open = 0;
	for (const FEntry& Entry : Entries(Open))
	{
		for (const FButton& Button : Entry.Buttons)
		{
			if (Button.Rect.IsInside(Local) && Button.bEnabled)
			{
				FUICommand Select;
				Select.Type = EUICommand::SelectIncident;
				Select.Index = Entry.Index;
				Race->Execute(Select);
				Race->Execute(Button.Command);
				return FReply::Handled();
			}
		}
	}
	return FReply::Handled();
}
