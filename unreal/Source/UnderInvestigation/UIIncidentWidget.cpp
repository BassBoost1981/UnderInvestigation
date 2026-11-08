#include "UIIncidentWidget.h"

#include "UIRaceDirector.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 MaxRows = 8;
	constexpr double PlotWidth = 400.0;
	constexpr double PlotHeight = 130.0;
	constexpr double PlotMaxKmh = 350.0;
	constexpr int32 PlotSamples = 100;
	const FLinearColor CarColor(1.0f, 0.35f, 0.25f);
	const FLinearColor OtherCarColor(0.3f, 0.75f, 1.0f);

	FString IncidentTypeName(EUISimIncidentType Type)
	{
		switch (Type)
		{
		case EUISimIncidentType::Collision: return TEXT("Kollision");
		case EUISimIncidentType::OvertakeUnderYellow: return TEXT("Ueberholt unter Gelb");
		case EUISimIncidentType::OvertakeUnderVsc: return TEXT("Ueberholt unter VSC");
		case EUISimIncidentType::OvertakeUnderSafetyCar: return TEXT("Ueberholt unter SC");
		case EUISimIncidentType::OvertakeUnderRedFlag: return TEXT("Ueberholt unter Rot");
		case EUISimIncidentType::OvertakeBeforeRestartLine: return TEXT("Ueberholt vor Linie");
		case EUISimIncidentType::IgnoringBlueFlags: return TEXT("Blau ignoriert");
		case EUISimIncidentType::Accident: return TEXT("Unfall");
		case EUISimIncidentType::PitLaneSpeeding: return TEXT("Speeding Boxengasse");
		case EUISimIncidentType::UnsafeRelease: return TEXT("Unsafe Release");
		case EUISimIncidentType::TrackLimits: return TEXT("Track Limits");
		case EUISimIncidentType::CompoundRule: return TEXT("Mischungspflicht");
		case EUISimIncidentType::PitEntryClosed: return TEXT("Boxengasse gesperrt");
		}
		return FString();
	}
}

UTextBlock* UUIIncidentWidget::MakeText(const FLinearColor& Color, int32 Size)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", Size));
	Text->SetColorAndOpacity(FSlateColor(Color));
	return Text;
}

TSharedRef<SWidget> UUIIncidentWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f));
		Panel->SetPadding(FMargin(12.0f));
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetAnchors(FAnchors(1.0f, 0.0f));   // top right
		PanelSlot->SetAlignment(FVector2D(1.0, 0.0));
		PanelSlot->SetPosition(FVector2D(-24.0, 24.0));

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Panel->SetContent(Column);

		Title = MakeText(FLinearColor(1.0f, 0.85f, 0.2f), 14);
		Column->AddChildToVerticalBox(Title);
		for (int32 Index = 0; Index < MaxRows; ++Index)
		{
			UTextBlock* Row = MakeText(FLinearColor::White);
			Row->SetVisibility(ESlateVisibility::Collapsed);
			Column->AddChildToVerticalBox(Row);
			Rows.Add(Row);
		}

		ReviewSection = WidgetTree->ConstructWidget<UVerticalBox>();
		ReviewSection->SetVisibility(ESlateVisibility::Collapsed);
		Column->AddChildToVerticalBox(ReviewSection);

		ReviewInfo = MakeText(FLinearColor(1.0f, 0.85f, 0.2f));
		ReviewSection->AddChildToVerticalBox(ReviewInfo);

		USizeBox* PipBox = WidgetTree->ConstructWidget<USizeBox>();
		PipBox->SetWidthOverride(PlotWidth);
		PipBox->SetHeightOverride(PlotWidth * 9.0 / 16.0);
		PipImage = WidgetTree->ConstructWidget<UImage>();
		PipBox->SetContent(PipImage);
		ReviewSection->AddChildToVerticalBox(PipBox);

		PlotArea = WidgetTree->ConstructWidget<USizeBox>();
		PlotArea->SetWidthOverride(PlotWidth);
		PlotArea->SetHeightOverride(PlotHeight);
		UBorder* PlotBackground = WidgetTree->ConstructWidget<UBorder>();
		PlotBackground->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.9f));
		PlotArea->SetContent(PlotBackground);
		ReviewSection->AddChildToVerticalBox(PlotArea);

		PlotLegend = MakeText(FLinearColor(0.75f, 0.75f, 0.75f), 12);
		ReviewSection->AddChildToVerticalBox(PlotLegend);
		ReviewControls = MakeText(FLinearColor(0.6f, 0.6f, 0.6f), 12);
		ReviewControls->SetText(FText::FromString(
			TEXT("Space Pause  Links/Rechts 1 s  C Kamera\nBackspace Replay beenden")));
		ReviewSection->AddChildToVerticalBox(ReviewControls);
	}
	return Super::RebuildWidget();
}

void UUIIncidentWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted())
	{
		return;
	}
	const TArray<FUISimIncident>& Incidents = Race->Session().Incidents();
	const TArray<FUISimEntry>& Entries = Race->Session().Entries();
	const int32 Selected = Race->SelectedIncident();

	Title->SetText(FText::FromString(FString::Printf(TEXT("VORFAELLE (%d)  [Pfeile, Enter]"), Incidents.Num())));

	// Newest first; keep the selected incident visible.
	const int32 Newest = Incidents.Num() - 1;
	const int32 Top = FMath::Max(Newest, Selected);
	for (int32 Row = 0; Row < Rows.Num(); ++Row)
	{
		const int32 Index = Top - Row;
		if (!Incidents.IsValidIndex(Index))
		{
			Rows[Row]->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FUISimIncident& Incident = Incidents[Index];
		FString Decision = TEXT("offen");
		if (const FUISimVerdict* Verdict = Race->Session().VerdictFor(Incident.Id))
		{
			Decision = AUIRaceDirector::PenaltyName(Verdict->Penalty);
			if (Verdict->Car >= 0)
			{
				Decision += TEXT(" ") + Entries[Verdict->Car].Code;
			}
		}
		const FString Cars = Incident.Car == Incident.OtherCar ? Entries[Incident.Car].Code
			: Entries[Incident.Car].Code + TEXT("/") + Entries[Incident.OtherCar].Code;
		Rows[Row]->SetText(FText::FromString(FString::Printf(TEXT("%s#%-2d %9s %-20s %-7s  %s"),
			Index == Selected ? TEXT(">") : TEXT(" "), Incident.Id, *AUIRaceDirector::FormatRaceTime(Incident.Time),
			*IncidentTypeName(Incident.Type), *Cars, *Decision)));
		Rows[Row]->SetColorAndOpacity(FSlateColor(Index == Selected ? FLinearColor(1.0f, 0.85f, 0.2f) : FLinearColor::White));
		Rows[Row]->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	if (!Race->IsReviewing())
	{
		ReviewSection->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	ReviewSection->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (Race->PictureInPicture())
	{
		PipImage->SetBrushResourceObject(Race->PictureInPicture());
	}
	const FUISimIncident& Incident = Incidents[Selected];
	ReviewInfo->SetText(FText::FromString(FString::Printf(TEXT("REPLAY #%d  t %+.1f s %s\nKamera: %s   Bild-im-Bild: %s"),
		Incident.Id, Race->ReviewOffset(), Race->IsReviewPaused() ? TEXT("[PAUSE]") : TEXT(""),
		*Race->ReviewCameraName(false), *Race->ReviewCameraName(true))));
	const FString Legend = Incident.Car == Incident.OtherCar ? Entries[Incident.Car].Code
		: FString::Printf(TEXT("%s rot  %s blau"), *Entries[Incident.Car].Code, *Entries[Incident.OtherCar].Code);
	PlotLegend->SetText(FText::FromString(FString::Printf(TEXT("Tempo km/h, -%.0f..+%.0f s:  %s"),
		AUIRaceDirector::ReviewWindow, AUIRaceDirector::ReviewWindow, *Legend)));

	FString VerdictLine;
	if (const FUISimVerdict* Verdict = Race->Session().VerdictFor(Incident.Id))
	{
		VerdictLine = FString::Printf(TEXT("Entschieden: %s %s"), *AUIRaceDirector::PenaltyName(Verdict->Penalty),
			Verdict->Car >= 0 ? *Entries[Verdict->Car].Code : TEXT(""));
	}
	else
	{
		const int32 Accused = Race->AccusedSide() == 0 ? Incident.Car : Incident.OtherCar;
		VerdictLine = FString::Printf(TEXT("Urteil gegen %s [Tab wechselt]:\n0 keine  1 Verwarnung  2 5s  3 10s  4 DT  5 S&G  6 DSQ"),
			*Entries[Accused].Code);
	}
	ReviewControls->SetText(FText::FromString(VerdictLine +
		TEXT("\nSpace Pause  Links/Rechts 1 s  C Kamera  Backspace Ende")));
}

int32 UUIIncidentWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
		bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->IsReviewing() || !PlotArea)
	{
		return Layer;
	}

	// Speed trace of both cars from the replay, drawn into the plot area.
	const FUISimIncident& Incident = Race->Session().Incidents()[Race->SelectedIncident()];
	const FGeometry& Plot = PlotArea->GetCachedGeometry();
	const FVector2D Size = Plot.GetLocalSize();
	if (Size.X <= 0.0 || Size.Y <= 0.0)
	{
		return Layer;
	}
	const double Window = AUIRaceDirector::ReviewWindow;
	auto ToPoint = [&](double Offset, double Kmh)
	{
		return FVector2D((Offset + Window) / (2.0 * Window) * Size.X, Size.Y - FMath::Clamp(Kmh / PlotMaxKmh, 0.0, 1.0) * Size.Y);
	};

	TArray<FVector2D> CarLine;
	TArray<FVector2D> OtherLine;
	TArray<FUISimReplayCar> Cars;
	double SafetyCarDistance = 0.0;
	bool bSafetyCar = false;
	for (int32 Sample = 0; Sample <= PlotSamples; ++Sample)
	{
		const double Offset = -Window + 2.0 * Window * Sample / PlotSamples;
		if (Race->Session().ReplayAt(Incident.Time + Offset, Cars, SafetyCarDistance, bSafetyCar))
		{
			CarLine.Add(ToPoint(Offset, Cars[Incident.Car].Speed * 3.6));
			OtherLine.Add(ToPoint(Offset, Cars[Incident.OtherCar].Speed * 3.6));
		}
	}
	++Layer;
	const FPaintGeometry Paint = Plot.ToPaintGeometry();
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, {ToPoint(0.0, 0.0), ToPoint(0.0, PlotMaxKmh)},
		ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f), true, 1.0f); // moment of the incident
	const double Now = Race->ReviewOffset();
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, {ToPoint(Now, 0.0), ToPoint(Now, PlotMaxKmh)},
		ESlateDrawEffect::None, FLinearColor(1.0f, 0.85f, 0.2f, 0.8f), true, 1.0f); // replay position
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, CarLine, ESlateDrawEffect::None, CarColor, true, 2.0f);
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, OtherLine, ESlateDrawEffect::None, OtherCarColor, true, 2.0f);
	return Layer;
}
