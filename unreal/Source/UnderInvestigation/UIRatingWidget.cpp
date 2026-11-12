#include "UIRatingWidget.h"

#include "UIRaceDirector.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogUISim, Log, All); // same category name as UIRaceDirector.cpp

namespace
{
	constexpr int32 MaxIncidentLines = 14;
	// Page buttons (in ButtonArea coordinates).
	const FBox2D PrevButton(FVector2D(0.0, 4.0), FVector2D(130.0, 28.0));
	const FBox2D NextButton(FVector2D(140.0, 4.0), FVector2D(270.0, 28.0));
	const FBox2D SeasonButton(FVector2D(280.0, 4.0), FVector2D(420.0, 28.0)); // season only (M10.6)

	FString Decision(const TArray<FUISimEntry>& Entries, int32 Car, EUISimPenalty Penalty)
	{
		if (Penalty == EUISimPenalty::None || Car < 0)
		{
			return TEXT("keine Aktion");
		}
		return AUIRaceDirector::PenaltyName(Penalty) + TEXT(" ") + Entries[Car].Code;
	}
}

TSharedRef<SWidget> UUIRatingWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;

		Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
		Panel->SetPadding(FMargin(20.0f));
		Panel->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f)); // centre of the screen
		PanelSlot->SetAlignment(FVector2D(0.5, 0.5));

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Panel->SetContent(Column);
		Body = WidgetTree->ConstructWidget<UTextBlock>();
		Body->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 14));
		Body->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Body->SetVisibility(ESlateVisibility::HitTestInvisible);
		Column->AddChildToVerticalBox(Body);
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(420.0);
		Box->SetHeightOverride(32.0);
		ButtonArea = WidgetTree->ConstructWidget<UBorder>();
		ButtonArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f)); // visible, so it takes the clicks
		Box->SetContent(ButtonArea);
		Column->AddChildToVerticalBox(Box);
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
	Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible); // the page buttons take clicks

	const FUISimRating Rating = Race->Session().Rating();
	const TArray<FUISimEntry>& Entries = Race->Session().Entries();
	FString Text = FString::Printf(TEXT("BEWERTUNG DER RENNLEITUNG:  %.0f / 100\n\n"), Rating.Overall);
	Text += FString::Printf(TEXT("Fairness      %3.0f %%   (Urteile gegen die Wahrheit)\n"), Rating.Fairness * 100.0);
	Text += FString::Printf(TEXT("Konsistenz    %3.0f %%   (aehnliche Faelle, aehnliche Urteile)\n"), Rating.Consistency * 100.0);
	Text += FString::Printf(TEXT("Rennqualitaet %3.0f %%   (wenig Neutralisation, keine Urteile nach dem Rennen)\n"), Rating.RaceQuality * 100.0);
	Text += FString::Printf(TEXT("Sicherheit    %3.0f %%   (Unfallstellen schnell gesichert, %d Folgeunfaelle)\n\n"),
		Rating.Safety * 100.0, Rating.FollowUpAccidents);
	Text += TEXT("Vorfall  Dein Urteil              Richtig waere            Punkte\n");

	const TArray<FUISimIncident>& Incidents = Race->Session().Incidents();
	int32 Rated = 0;
	for (int32 Index = 0; Index < Rating.Incidents.Num(); ++Index)
	{
		Rated += Incidents[Index].Type == EUISimIncidentType::Accident && !Rating.Incidents[Index].bDecided ? 0 : 1;
	}
	PageCount = FMath::Max(1, (Rated + MaxIncidentLines - 1) / MaxIncidentLines);
	Page = FMath::Clamp(Page, 0, PageCount - 1);
	int32 Line = 0;
	for (int32 Index = 0; Index < Rating.Incidents.Num(); ++Index)
	{
		const FUISimIncidentRating& R = Rating.Incidents[Index];
		if (Incidents[Index].Type == EUISimIncidentType::Accident && !R.bDecided)
		{
			continue; // not rated (core Rating.cpp)
		}
		if (Line++ / MaxIncidentLines != Page)
		{
			continue; // another page
		}
		const FString Given = R.bDecided ? Decision(Entries, R.GivenCar, R.GivenPenalty) : FString(TEXT("(nicht entschieden)"));
		const FString Truth = Decision(Entries, R.ExpectedCar, R.ExpectedPenalty);
		const int32 Blame = FMath::RoundToInt(Incidents[Index].GroundTruthBlame * 100.0);
		Text += FString::Printf(TEXT("#%-6d %-24s %-18s %3d%%  %3.0f %%%s\n"), R.IncidentId, *Given, *Truth, Blame, R.Score * 100.0,
			R.bOverturned ? TEXT("  KASSIERT") : TEXT(""));
	}
	Text += FString::Printf(TEXT("Seite %d/%d\n"), Page + 1, PageCount);
	if (Rating.Overturned > 0)
	{
		Text += FString::Printf(TEXT("Im Right of Review kassiert: %d Urteil(e) ohne Anhoerung, je 5 Punkte Abzug\n"), Rating.Overturned);
	}
	Text += TEXT("\n(Prozentwert hinter dem richtigen Urteil: Schuldanteil des Angreifers)");
	Body->SetText(FText::FromString(Text));
}

int32 UUIRatingWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!Panel || !Panel->IsVisible() || !ButtonArea || ButtonArea->GetCachedGeometry().GetLocalSize().X <= 0.0)
	{
		return Layer;
	}
	const FGeometry& Area = ButtonArea->GetCachedGeometry();
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Mono", 11);
	++Layer;
	auto Button = [&](const FBox2D& Rect, const TCHAR* Label, bool bEnabled)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Area.ToPaintGeometry(Rect.GetSize(), FSlateLayoutTransform(1.0f, Rect.Min)), White,
			ESlateDrawEffect::None, bEnabled ? FLinearColor(0.22f, 0.22f, 0.24f, 0.95f) : FLinearColor(0.08f, 0.08f, 0.08f, 0.9f));
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, Area.ToPaintGeometry(Rect.GetSize(),
			FSlateLayoutTransform(1.0f, Rect.Min + FVector2D(8.0, 4.0))), Label, Font, ESlateDrawEffect::None,
			bEnabled ? FLinearColor::White : FLinearColor(0.4f, 0.4f, 0.4f));
	};
	Button(PrevButton, TEXT("< ZURUECK"), Page > 0);
	Button(NextButton, TEXT("WEITER >"), Page + 1 < PageCount);
	const bool bSeason = Director.IsValid() && Director->IsSeasonMode();
	if (bSeason)
	{
		Button(SeasonButton, TEXT("ZUR SAISON >>"), true);
	}
	Layer += 2;

	if (FVector2D(Area.GetAbsolutePosition()) != LoggedAt)
	{
		// Viewport pixels of the page buttons, for -UISimScript checks (click x y); logged again when they move.
		LoggedAt = FVector2D(Area.GetAbsolutePosition());
		const UWorld* World = GetWorld();
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
		if (ViewportWidget.IsValid())
		{
			const FVector2D Origin = FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
			const FVector2D Prev = FVector2D(Area.LocalToAbsolute(PrevButton.GetCenter())) - Origin;
			const FVector2D Next = FVector2D(Area.LocalToAbsolute(NextButton.GetCenter())) - Origin;
			UE_LOG(LogUISim, Display, TEXT("Rating: '< ZURUECK' at %.0f %.0f, 'WEITER >' at %.0f %.0f"), Prev.X, Prev.Y, Next.X, Next.Y);
			if (bSeason)
			{
				const FVector2D Season = FVector2D(Area.LocalToAbsolute(SeasonButton.GetCenter())) - Origin;
				UE_LOG(LogUISim, Display, TEXT("Rating: 'ZUR SAISON' at %.0f %.0f"), Season.X, Season.Y);
			}
		}
	}
	return Layer;
}

FReply UUIRatingWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!ButtonArea || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Local = FVector2D(ButtonArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	AUIRaceDirector* Race = Director.Get();
	if (Race && Race->IsSeasonMode() && SeasonButton.IsInside(Local))
	{
		FUICommand Command;
		Command.Type = EUICommand::BackToSeason;
		Race->Execute(Command); // books the race; this screen goes with it
		return FReply::Handled();
	}
	const int32 Before = Page;
	if (PrevButton.IsInside(Local))
	{
		Page = FMath::Max(Page - 1, 0);
	}
	else if (NextButton.IsInside(Local))
	{
		Page = FMath::Min(Page + 1, PageCount - 1);
	}
	if (Page != Before)
	{
		UE_LOG(LogUISim, Display, TEXT("Rating: page %d/%d"), Page + 1, PageCount);
	}
	return FReply::Handled();
}
