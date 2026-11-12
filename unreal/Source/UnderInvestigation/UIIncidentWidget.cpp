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
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogUISim, Log, All); // same category name as UIRaceDirector.cpp

namespace
{
	constexpr int32 MaxRows = 8;
	constexpr double PlotWidth = 400.0;
	constexpr double PlotHeight = 130.0;
	constexpr double PlotMaxKmh = 350.0;
	constexpr int32 PlotSamples = 100;
	constexpr double ControlHeight = 196.0;
	constexpr double ButtonHeight = 22.0;

	// Penalty dialog: name and explanation in our own words, with the article of Section B.
	const TCHAR* const PenaltyButton[] = {TEXT("KEINE"), TEXT("VERWARNUNG"), TEXT("5 S"), TEXT("10 S"), TEXT("DT"), TEXT("S&G"), TEXT("DSQ")};
	const TCHAR* const PenaltyHelp[] = {
		TEXT("Keine Strafe: kein Verstoss, oder niemand ist ganz oder\nueberwiegend schuld (B1.9.3)."),
		TEXT("Verwarnung (Reprimand): festgehalten, ohne Zeitverlust;\nfuenf im Jahr kosten Startplaetze (B1.9.5 f)."),
		TEXT("5 Sekunden: beim naechsten Stopp absitzen, sonst auf die\nRennzeit (B1.9.5 a, B1.9.6 b)."),
		TEXT("10 Sekunden: wie 5 s, fuer schwerere Verstoesse\n(B1.9.5 b, B1.9.6 b)."),
		TEXT("Drive-Through: durch die Boxengasse ohne Halt, binnen\ndrei Runden; zu spaet: +20 s (B1.9.5 c, B1.9.6 c)."),
		TEXT("Stop-and-Go: 10 s Halt an der Box, ohne Service;\nzu spaet: +30 s (B1.9.5 d, B1.9.6 c)."),
		TEXT("Disqualifikation: aus der Wertung gestrichen, etwa fuer\nden Verstoss gegen die Mischungspflicht (B1.9.5 i, B6.3.6)."),
	};
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
		case EUISimIncidentType::WetTyreRule: return TEXT("Regenreifen-Pflicht");
		case EUISimIncidentType::FalseStart: return TEXT("Fruehstart");
		case EUISimIncidentType::GridPosition: return TEXT("Startposition");
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
		USizeBox* ControlBox = WidgetTree->ConstructWidget<USizeBox>();
		ControlBox->SetWidthOverride(PlotWidth);
		ControlBox->SetHeightOverride(ControlHeight);
		ControlArea = WidgetTree->ConstructWidget<UBorder>();
		ControlArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f)); // visible, so it takes the clicks
		ControlBox->SetContent(ControlArea);
		ReviewSection->AddChildToVerticalBox(ControlBox);
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

	Title->SetText(FText::FromString(FString::Printf(TEXT("VORFAELLE (%d)  [Klick oder Pfeile, Enter]"), Incidents.Num())));
	RowIncident.Init(-1, Rows.Num());

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
		RowIncident[Row] = Index;
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
		Rows[Row]->SetVisibility(ESlateVisibility::Visible); // clickable
	}

	if (!Race->IsReviewing())
	{
		ReviewSection->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	ReviewSection->SetVisibility(ESlateVisibility::SelfHitTestInvisible); // the control area takes clicks
	if (Race->PictureInPicture())
	{
		PipImage->SetBrushResourceObject(Race->PictureInPicture());
	}
	const FUISimIncident& Incident = Incidents[Race->ReviewedIncident()];
	if (ChosenForIncident != Incident.Id)
	{
		ChosenForIncident = Incident.Id; // a new incident: nothing chosen yet
		ChosenPenalty = -1;
	}
	ReviewInfo->SetText(FText::FromString(FString::Printf(TEXT("REPLAY #%d  t %+.1f s %s\nKamera: %s   Bild-im-Bild: %s"),
		Incident.Id, Race->ReviewOffset(), Race->IsReviewPaused() ? TEXT("[PAUSE]") : TEXT(""),
		*Race->ReviewCameraName(false), *Race->ReviewCameraName(true))));
	const FString Legend = Incident.Car == Incident.OtherCar ? Entries[Incident.Car].Code
		: FString::Printf(TEXT("%s rot  %s blau"), *Entries[Incident.Car].Code, *Entries[Incident.OtherCar].Code);
	PlotLegend->SetText(FText::FromString(FString::Printf(TEXT("Tempo km/h, -%.0f..+%.0f s:  %s"),
		AUIRaceDirector::ReviewWindow, AUIRaceDirector::ReviewWindow, *Legend)));

	// Hearing (M11.7): what the recording shows next to what each driver claims. Comparing them is the player's job.
	const FUISimSession& Session = Race->Session();
	FString Help = TEXT("Tasten: Space Pause, Links/Rechts 1 s, C Kamera,\nTab Beschuldigter, 0-6 Urteil, Backspace Ende");
	if (Session.IsHeard(Incident.Id))
	{
		const FUISimHearing Hearing = Session.Hearing(Incident.Id);
		static const TCHAR* Labels[] = {TEXT("Abstand m"), TEXT("Spaeter gebremst m"), TEXT("Rueberziehen m"), TEXT("Messwert"), TEXT("Verstoss (1=ja)")};
		Help = FString::Printf(TEXT("ANHOERUNG          Beweis %7s %7s\n"), *Entries[Incident.Car].Code,
			Hearing.bTwoCars ? *Entries[Incident.OtherCar].Code : TEXT(""));
		for (const FUISimHearingItem& Item : Hearing.Items)
		{
			Help += FString::Printf(TEXT("%-18s %6.1f %7.1f"), Labels[FMath::Clamp(Item.Kind, 0, 4)], Item.Evidence, Item.ClaimCar);
			Help += Hearing.bTwoCars ? FString::Printf(TEXT(" %7.1f\n"), Item.ClaimOther) : FString(TEXT("\n"));
		}
		const double Wait = Session.HearingUntil(Incident.Id) - Session.Snapshot().Time;
		Help += !Session.Snapshot().bFinished && Wait > 0.0
			? FString::Printf(TEXT("Anhoerung laeuft: Urteil in %.0f s moeglich"), Wait)
			: FString(TEXT("Weicht eine Aussage deutlich vom Beweis ab, stimmt sie nicht."));
	}
	else if (!Session.VerdictFor(Incident.Id))
	{
		Help += TEXT("\nANHOERUNG: beide Seiten hoeren (im Rennen 3 min Wartezeit)");
	}
	ReviewControls->SetText(FText::FromString(Help));
}

int32 UUIIncidentWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
		bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted())
	{
		return Layer;
	}
	// Click positions of the list rows, for -UISimScript checks (click x y).
	const UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
	const FVector2D Origin = ViewportWidget.IsValid() ? FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition()) : FVector2D::ZeroVector;
	for (int32 Row = 0; Row < Rows.Num(); ++Row)
	{
		const FGeometry& RowGeometry = Rows[Row]->GetCachedGeometry();
		if (ViewportWidget.IsValid() && Rows[Row]->IsVisible() && RowGeometry.GetLocalSize().X > 0.0 && !LoggedRows.Contains(Row))
		{
			LoggedRows.Add(Row);
			const FVector2D At = FVector2D(RowGeometry.LocalToAbsolute(FVector2D(60.0, RowGeometry.GetLocalSize().Y / 2.0))) - Origin;
			UE_LOG(LogUISim, Display, TEXT("Incident list: row %d at %.0f %.0f"), Row + 1, At.X, At.Y);
		}
	}
	if (!Race->IsReviewing() || !PlotArea)
	{
		return Layer;
	}

	// Review controls and the penalty dialog.
	if (ControlArea && ControlArea->GetCachedGeometry().GetLocalSize().X > 0.0)
	{
		const FGeometry& Area = ControlArea->GetCachedGeometry();
		const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Mono", 9);
		auto Text = [&](const FVector2D& TopLeft, const FString& String, const FLinearColor& Color, int32 TextLayer)
		{
			FSlateDrawElement::MakeText(OutDrawElements, TextLayer, Area.ToPaintGeometry(FVector2D(PlotWidth, 40.0),
				FSlateLayoutTransform(1.0f, TopLeft)), String, Font, ESlateDrawEffect::None, Color);
		};
		auto Box = [&](const FBox2D& Rect, const FLinearColor& Color, int32 BoxLayer)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, BoxLayer, Area.ToPaintGeometry(Rect.GetSize(), FSlateLayoutTransform(1.0f, Rect.Min)),
				White, ESlateDrawEffect::None, Color);
		};
		++Layer;
		// Timeline -5..+5 s: incident in the middle, replay position in yellow.
		const FBox2D Bar = TimelineRect();
		Box(Bar, FLinearColor(0.15f, 0.15f, 0.15f, 0.95f), Layer);
		const double Window = AUIRaceDirector::ReviewWindow;
		auto BarX = [&](double Offset) { return Bar.Min.X + (Offset + Window) / (2.0 * Window) * Bar.GetSize().X; };
		Box(FBox2D(FVector2D(BarX(0.0) - 1.0, Bar.Min.Y), FVector2D(BarX(0.0) + 1.0, Bar.Max.Y)), FLinearColor::White, Layer + 1);
		Box(FBox2D(FVector2D(BarX(Race->ReviewOffset()) - 3.0, Bar.Min.Y - 2.0), FVector2D(BarX(Race->ReviewOffset()) + 3.0, Bar.Max.Y + 2.0)),
			FLinearColor(1.0f, 0.85f, 0.2f), Layer + 1);
		for (const FControl& Control : Controls())
		{
			const FLinearColor Fill = !Control.bEnabled ? FLinearColor(0.08f, 0.08f, 0.08f, 0.9f)
				: Control.bActive ? FLinearColor(1.0f, 0.85f, 0.2f) : FLinearColor(0.22f, 0.22f, 0.24f, 0.95f);
			const FLinearColor Ink = !Control.bEnabled ? FLinearColor(0.4f, 0.4f, 0.4f) : Control.bActive ? FLinearColor::Black : FLinearColor::White;
			Box(Control.Rect, Fill, Layer);
			Text(Control.Rect.Min + FVector2D(5.0, 4.0), Control.Label, Ink, Layer + 1);
		}
		const FUISimIncident& Reviewed = Race->Session().Incidents()[Race->ReviewedIncident()];
		const TArray<FUISimEntry>& Entries = Race->Session().Entries();
		if (const FUISimVerdict* Verdict = Race->Session().VerdictFor(Reviewed.Id))
		{
			Text(FVector2D(0.0, 52.0), FString::Printf(TEXT("Entschieden: %s %s"), *AUIRaceDirector::PenaltyName(Verdict->Penalty),
				Verdict->Car >= 0 ? *Entries[Verdict->Car].Code : TEXT("")), FLinearColor(1.0f, 0.85f, 0.2f), Layer + 1);
		}
		else
		{
			Text(FVector2D(0.0, 56.0), TEXT("Urteil gegen:"), FLinearColor(0.8f, 0.8f, 0.8f), Layer + 1);
			const FString Help = ChosenPenalty >= 0 ? FString(PenaltyHelp[ChosenPenalty]) : FString(TEXT("Strafe waehlen, dann BESTAETIGEN."));
			Text(FVector2D(0.0, 136.0), Help, FLinearColor(0.85f, 0.85f, 0.85f), Layer + 1);
		}
		Layer += 2;
		const FVector2D AreaAt = FVector2D(Area.GetAbsolutePosition());
		if (AreaAt != LoggedControlsAt && ViewportWidget.IsValid())
		{
			LoggedControlsAt = AreaAt;
			for (const FControl& Control : Controls())
			{
				const FVector2D At = FVector2D(Area.LocalToAbsolute(Control.Rect.GetCenter())) - Origin;
				UE_LOG(LogUISim, Display, TEXT("Review controls: '%s' at %.0f %.0f"), *Control.Label, At.X, At.Y);
			}
			for (double Offset : {-5.0, 0.0, 5.0})
			{
				const FVector2D At = FVector2D(Area.LocalToAbsolute(FVector2D(BarX(Offset), Bar.GetCenter().Y))) - Origin;
				UE_LOG(LogUISim, Display, TEXT("Review controls: timeline %+.0f s at %.0f %.0f"), Offset, At.X, At.Y);
			}
		}
	}

	// Speed trace of both cars from the replay, drawn into the plot area.
	const FUISimIncident& Incident = Race->Session().Incidents()[Race->ReviewedIncident()];
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

FBox2D UUIIncidentWidget::TimelineRect() const
{
	return FBox2D(FVector2D(0.0, 2.0), FVector2D(PlotWidth, 16.0));
}

TArray<UUIIncidentWidget::FControl> UUIIncidentWidget::Controls() const
{
	TArray<FControl> List;
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->IsReviewing())
	{
		return List;
	}
	auto Add = [&](double Left, double Top, double Width, const FString& Label, EAction Action, EUICommand Type, double Value = 0.0,
		int32 Index = 0, bool bActive = false)
	{
		FControl Control;
		Control.Rect = FBox2D(FVector2D(Left, Top), FVector2D(Left + Width, Top + ButtonHeight));
		Control.Label = Label;
		Control.Action = Action;
		Control.Command.Type = Type;
		Control.Command.Value = Value;
		Control.Command.Index = Index;
		Control.bActive = bActive;
		Control.bEnabled = Action != EAction::Command || Race->CanExecute(Control.Command);
		List.Add(Control);
		return List.Num() - 1;
	};
	// Replay: -1 s, pause/play, +1 s, camera, end.
	Add(0.0, 22.0, 64.0, TEXT("-1 S"), EAction::Command, EUICommand::SeekReview, -1.0);
	Add(70.0, 22.0, 84.0, Race->IsReviewPaused() ? TEXT("PLAY") : TEXT("PAUSE"), EAction::Command, EUICommand::ToggleReviewPause, 0.0, 0,
		Race->IsReviewPaused());
	Add(160.0, 22.0, 64.0, TEXT("+1 S"), EAction::Command, EUICommand::SeekReview, 1.0);
	Add(230.0, 22.0, 84.0, TEXT("KAMERA"), EAction::Command, EUICommand::NextReviewCamera);
	Add(320.0, 22.0, 80.0, TEXT("ENDE"), EAction::Command, EUICommand::StopReview);

	// Penalty dialog, while the incident is open.
	const FUISimIncident& Incident = Race->Session().Incidents()[Race->ReviewedIncident()];
	if (Race->Session().VerdictFor(Incident.Id))
	{
		return List;
	}
	Add(0.0, 136.0, 130.0, TEXT("ANHOERUNG"), EAction::Command, EUICommand::HoldHearing);
	const TArray<FUISimEntry>& Entries = Race->Session().Entries();
	Add(110.0, 52.0, 70.0, Entries[Incident.Car].Code, EAction::Command, EUICommand::SetAccused, 0.0, 0, Race->AccusedSide() == 0);
	if (Incident.OtherCar != Incident.Car)
	{
		Add(186.0, 52.0, 70.0, Entries[Incident.OtherCar].Code, EAction::Command, EUICommand::SetAccused, 0.0, 1, Race->AccusedSide() == 1);
	}
	const double Widths[] = {64.0, 100.0, 56.0, 56.0, 64.0, 64.0, 64.0};
	double Left = 0.0;
	for (int32 Penalty = 0; Penalty < 7; ++Penalty)
	{
		const double Top = Penalty < 4 ? 80.0 : 108.0;
		if (Penalty == 4)
		{
			Left = 0.0;
		}
		const int32 Index = Add(Left, Top, Widths[Penalty], PenaltyButton[Penalty], EAction::ChoosePenalty, EUICommand::Verdict, 0.0, 0,
			ChosenPenalty == Penalty);
		List[Index].Penalty = Penalty;
		Left += Widths[Penalty] + 6.0;
	}
	FUICommand Verdict;
	Verdict.Type = EUICommand::Verdict;
	Verdict.Penalty = static_cast<EUISimPenalty>(FMath::Max(ChosenPenalty, 0));
	const int32 Confirm = Add(222.0, 108.0, 178.0, TEXT("BESTAETIGEN"), EAction::Confirm, EUICommand::Verdict);
	List[Confirm].Command = Verdict;
	List[Confirm].bEnabled = ChosenPenalty >= 0 && Race->CanExecute(Verdict);
	return List;
}

bool UUIIncidentWidget::SeekTimeline(const FVector2D& Local)
{
	AUIRaceDirector* Race = Director.Get();
	const FBox2D Bar = TimelineRect().ExpandBy(FVector2D(0.0, 4.0));
	if (!Race || !Bar.IsInside(Local))
	{
		return false;
	}
	const double Window = AUIRaceDirector::ReviewWindow;
	const double Offset = FMath::RoundToDouble(((Local.X - Bar.Min.X) / Bar.GetSize().X * 2.0 * Window - Window) * 10.0) / 10.0;
	if (FMath::Abs(Offset - LastDragOffset) < 0.05)
	{
		return true; // same position as the last drag step
	}
	LastDragOffset = Offset;
	FUICommand Command;
	Command.Type = EUICommand::SetReviewOffset;
	Command.Value = Offset;
	Race->Execute(Command);
	return true;
}

FReply UUIIncidentWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (!Race || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Screen = InMouseEvent.GetScreenSpacePosition();

	// A row of the list: select the incident and review it.
	for (int32 Row = 0; Row < Rows.Num(); ++Row)
	{
		if (RowIncident.IsValidIndex(Row) && RowIncident[Row] >= 0 && Rows[Row]->IsVisible() &&
			Rows[Row]->GetCachedGeometry().IsUnderLocation(Screen))
		{
			FUICommand Select;
			Select.Type = EUICommand::SelectIncident;
			Select.Index = RowIncident[Row];
			Race->Execute(Select);
			FUICommand Review;
			Review.Type = EUICommand::StartReview;
			Review.Index = RowIncident[Row];
			Race->Execute(Review);
			return FReply::Handled();
		}
	}

	if (!Race->IsReviewing() || !ControlArea)
	{
		return FReply::Handled();
	}
	const FVector2D Local = FVector2D(ControlArea->GetCachedGeometry().AbsoluteToLocal(Screen));
	LastDragOffset = 1000.0;
	if (SeekTimeline(Local))
	{
		return FReply::Handled();
	}
	for (const FControl& Control : Controls())
	{
		if (!Control.Rect.IsInside(Local))
		{
			continue;
		}
		switch (Control.Action)
		{
		case EAction::Command:
			Race->Execute(Control.Command);
			break;
		case EAction::ChoosePenalty:
			ChosenPenalty = Control.Penalty;
			UE_LOG(LogUISim, Display, TEXT("Penalty dialog: %s chosen"), *AUIRaceDirector::PenaltyName(static_cast<EUISimPenalty>(Control.Penalty)));
			break;
		case EAction::Confirm:
			if (Control.bEnabled)
			{
				Race->Execute(Control.Command);
				ChosenPenalty = -1;
			}
			break;
		}
		break;
	}
	return FReply::Handled();
}

FReply UUIIncidentWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Dragging along the timeline scrubs the replay.
	const AUIRaceDirector* Race = Director.Get();
	if (Race && Race->IsReviewing() && ControlArea && InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) &&
		SeekTimeline(FVector2D(ControlArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()))))
	{
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}
