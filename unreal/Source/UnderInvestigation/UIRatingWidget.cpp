#include "UIRatingWidget.h"

#include "UIRaceDirector.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 MaxIncidentLines = 14;

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

		Body = WidgetTree->ConstructWidget<UTextBlock>();
		Body->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 14));
		Body->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Panel->SetContent(Body);
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
	Panel->SetVisibility(ESlateVisibility::HitTestInvisible);

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
	int32 Lines = 0;
	int32 Hidden = 0;
	for (int32 Index = 0; Index < Rating.Incidents.Num(); ++Index)
	{
		const FUISimIncidentRating& R = Rating.Incidents[Index];
		if (Incidents[Index].Type == EUISimIncidentType::Accident && !R.bDecided)
		{
			continue; // not rated (core Rating.cpp)
		}
		if (Lines++ >= MaxIncidentLines)
		{
			++Hidden;
			continue;
		}
		const FString Given = R.bDecided ? Decision(Entries, R.GivenCar, R.GivenPenalty) : FString(TEXT("(nicht entschieden)"));
		const FString Truth = Decision(Entries, R.ExpectedCar, R.ExpectedPenalty);
		const int32 Blame = FMath::RoundToInt(Incidents[Index].GroundTruthBlame * 100.0);
		Text += FString::Printf(TEXT("#%-6d %-24s %-18s %3d%%  %3.0f %%\n"), R.IncidentId, *Given, *Truth, Blame, R.Score * 100.0);
	}
	if (Hidden > 0)
	{
		Text += FString::Printf(TEXT("... und %d weitere\n"), Hidden);
	}
	Text += TEXT("\n(Prozentwert hinter dem richtigen Urteil: Schuldanteil des Angreifers)");
	Body->SetText(FText::FromString(Text));
}
