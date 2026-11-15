#include "UIReviewWidget.h"

#include "UIRaceDirector.h"
#include "UIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogUISim, Log, All); // same category name as UIRaceDirector.cpp

namespace
{
	// Layout in card coordinates (the card is the place of the track map, 635 x 411).
	const FBox2D Pip(FVector2D(12.0, 40.0), FVector2D(332.0, 220.0));   // second camera, 16:9
	const FBox2D Plot(FVector2D(12.0, 230.0), FVector2D(332.0, 372.0)); // speed trace
	constexpr double Right = 348.0, RightWidth = 275.0;
	const FBox2D Timeline(FVector2D(Right, 58.0), FVector2D(Right + RightWidth, 72.0));
	constexpr double ReplayTop = 80.0, AccusedTop = 114.0, PenaltyTop = 146.0, PenaltyStep = 28.0;
	constexpr double HelpTop = 204.0, HearingTop = 244.0, TableTop = 276.0, ButtonHeight = 22.0;
	constexpr double PlotMaxKmh = 350.0;
	constexpr int32 PlotSamples = 100;

	// Penalty dialog: name and explanation in our own words, with the article of Section B.
	const TCHAR* const PenaltyButton[] = {TEXT("NONE"), TEXT("REPRIMAND"), TEXT("5 S"), TEXT("10 S"), TEXT("DT"), TEXT("S&G"), TEXT("DSQ")};
	const TCHAR* const PenaltyHelp[] = {
		TEXT("No penalty: no breach, or nobody wholly or\nmainly to blame (B1.9.3)."),
		TEXT("Reprimand: on record, no time lost; five in a\nseason cost grid places (B1.9.5 f)."),
		TEXT("5 seconds: served at the next stop, otherwise\nadded to the race time (B1.9.5 a, B1.9.6 b)."),
		TEXT("10 seconds: as 5 s, for more serious breaches\n(B1.9.5 b, B1.9.6 b)."),
		TEXT("Drive-through: through the pit lane without a stop,\nwithin three laps; too late: +20 s (B1.9.5 c, B1.9.6 c)."),
		TEXT("Stop-and-go: 10 s stop at the box, no work;\ntoo late: +30 s (B1.9.5 d, B1.9.6 c)."),
		TEXT("Disqualification: out of the results, e.g. for\nthe compound rule (B1.9.5 i, B6.3.6)."),
	};
	const FLinearColor CarColor = UIStyle::Red;
	const FLinearColor OtherCarColor = UIStyle::Rgb(80, 150, 255);
	const FLinearColor Fill = UIStyle::Rgb(34, 35, 39); // white at 6 % on the card
	const FLinearColor Edge = UIStyle::Rgb(44, 45, 49); // white at 10 %
}

TSharedRef<SWidget> UUIReviewWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		CardArea = WidgetTree->ConstructWidget<UBorder>();
		CardArea->SetBrushColor(UIStyle::Card); // covers the track map card
		CardArea->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* AreaSlot = Canvas->AddChildToCanvas(CardArea);
		AreaSlot->SetPosition(UIStyle::MapCard.Min + FVector2D(1.0, 1.0));
		AreaSlot->SetSize(UIStyle::MapCard.GetSize() - FVector2D(2.0, 2.0));

		PipImage = WidgetTree->ConstructWidget<UImage>();
		PipImage->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* PipSlot = Canvas->AddChildToCanvas(PipImage);
		PipSlot->SetPosition(UIStyle::MapCard.Min + Pip.Min);
		PipSlot->SetSize(Pip.GetSize());
	}
	return Super::RebuildWidget();
}

void UUIReviewWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AUIRaceDirector* Race = Director.Get();
	const bool bReview = Race && Race->Session().IsStarted() && Race->IsReviewing();
	CardArea->SetVisibility(bReview ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	PipImage->SetVisibility(bReview ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!bReview)
	{
		return;
	}
	if (Race->PictureInPicture())
	{
		PipImage->SetBrushResourceObject(Race->PictureInPicture());
	}
	const int32 Id = Race->Session().Incidents()[Race->ReviewedIncident()].Id;
	if (ChosenForIncident != Id)
	{
		ChosenForIncident = Id; // a new incident: nothing chosen yet
		ChosenPenalty = -1;
	}
}

TArray<UUIReviewWidget::FControl> UUIReviewWidget::Controls() const
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
	Add(Right, ReplayTop, 44.0, TEXT("-1 S"), EAction::Command, EUICommand::SeekReview, -1.0);
	Add(Right + 48.0, ReplayTop, 58.0, Race->IsReviewPaused() ? TEXT("PLAY") : TEXT("PAUSE"), EAction::Command, EUICommand::ToggleReviewPause,
		0.0, 0, Race->IsReviewPaused());
	Add(Right + 110.0, ReplayTop, 44.0, TEXT("+1 S"), EAction::Command, EUICommand::SeekReview, 1.0);
	Add(Right + 158.0, ReplayTop, 64.0, TEXT("CAMERA"), EAction::Command, EUICommand::NextReviewCamera);
	Add(Right + 226.0, ReplayTop, 49.0, TEXT("END"), EAction::Command, EUICommand::StopReview);

	// Verdict, while the incident is open.
	const FUISimIncident& Incident = Race->Session().Incidents()[Race->ReviewedIncident()];
	if (Race->Session().VerdictFor(Incident.Id))
	{
		return List;
	}
	const TArray<FUISimEntry>& Entries = Race->Session().Entries();
	Add(Right + 70.0, AccusedTop, 58.0, Entries[Incident.Car].Code.ToUpper(), EAction::Command, EUICommand::SetAccused, 0.0, 0,
		Race->AccusedSide() == 0);
	if (Incident.OtherCar != Incident.Car)
	{
		Add(Right + 132.0, AccusedTop, 58.0, Entries[Incident.OtherCar].Code.ToUpper(), EAction::Command, EUICommand::SetAccused, 0.0, 1,
			Race->AccusedSide() == 1);
	}
	const double Widths[] = {54.0, 82.0, 63.0, 64.0, 54.0, 54.0, 54.0};
	double Left = Right;
	for (int32 Penalty = 0; Penalty < 7; ++Penalty)
	{
		if (Penalty == 4)
		{
			Left = Right;
		}
		const int32 Index = Add(Left, PenaltyTop + (Penalty < 4 ? 0.0 : PenaltyStep), Widths[Penalty], PenaltyButton[Penalty],
			EAction::ChoosePenalty, EUICommand::Verdict, 0.0, 0, ChosenPenalty == Penalty);
		List[Index].Penalty = Penalty;
		Left += Widths[Penalty] + 4.0;
	}
	FUICommand Verdict;
	Verdict.Type = EUICommand::Verdict;
	Verdict.Penalty = static_cast<EUISimPenalty>(FMath::Max(ChosenPenalty, 0));
	const int32 Confirm = Add(Right + 174.0, PenaltyTop + PenaltyStep, RightWidth - 174.0, TEXT("CONFIRM"), EAction::Confirm, EUICommand::Verdict);
	List[Confirm].Command = Verdict;
	List[Confirm].bEnabled = ChosenPenalty >= 0 && Race->CanExecute(Verdict);
	Add(Right, HearingTop, 92.0, TEXT("HEARING"), EAction::Command, EUICommand::HoldHearing);
	return List;
}

int32 UUIReviewWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted() || !Race->IsReviewing() || !CardArea || CardArea->GetCachedGeometry().GetLocalSize().X <= 0.0)
	{
		return Layer;
	}
	static const FSlateRoundedBoxBrush Round8(FLinearColor::White, 8.0f);
	static const FSlateRoundedBoxBrush Round7(FLinearColor::White, 7.0f);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	// Card coordinates: the area starts one unit inside the card.
	const FGeometry& Area = CardArea->GetCachedGeometry();
	const FVector2D Shift(-1.0, -1.0);
	auto Box = [&](const FBox2D& Rect, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Area.ToPaintGeometry(Rect.GetSize(), FSlateLayoutTransform(1.0f, Rect.Min + Shift)),
			Brush, ESlateDrawEffect::None, Color);
	};
	auto Text = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, Area.ToPaintGeometry(FVector2D(620.0, 60.0),
			FSlateLayoutTransform(1.0f, At + Shift)), String, Font, ESlateDrawEffect::None, Color);
	};
	auto TextRight = [&](double RightEdge, double Y, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		Text(FVector2D(RightEdge - Measure->Measure(String, Font).X, Y), String, Font, Color);
	};
	const FUISimSession& Session = Race->Session();
	const FUISimIncident& Incident = Session.Incidents()[Race->ReviewedIncident()];
	const TArray<FUISimEntry>& Entries = Session.Entries();
	const FString CarCode = Entries[Incident.Car].Code.ToUpper(), OtherCode = Entries[Incident.OtherCar].Code.ToUpper();
	const bool bTwoCars = Incident.Car != Incident.OtherCar;
	const FSlateFontInfo Small = UIStyle::Font(TEXT("Regular"), 7);
	const FSlateFontInfo SmallBold = UIStyle::Font(TEXT("Bold"), 7);
	const FSlateFontInfo Label = UIStyle::Font(TEXT("Regular"), 8);

	++Layer;
	// Header: incident and replay time.
	const FString Title = FString::Printf(TEXT("Review · #%d %s · %s"), Incident.Id, *AUIRaceDirector::IncidentTypeName(Incident.Type),
		bTwoCars ? *(CarCode + TEXT(" / ") + OtherCode) : *CarCode);
	Text(FVector2D(16.0, 13.0), Title, UIStyle::Font(TEXT("Bold"), 11), UIStyle::Text);
	TextRight(619.0, 16.0, FString::Printf(TEXT("REPLAY %+.1f S%s"), Race->ReviewOffset(), Race->IsReviewPaused() ? TEXT(" · PAUSED") : TEXT("")),
		Label, Race->IsReviewPaused() ? UIStyle::Yellow : UIStyle::Mute);

	// Picture-in-picture (the image lies under this paint): camera name.
	Text(Pip.Min + FVector2D(8.0, 6.0), TEXT("PIP · ") + Race->ReviewCameraName(true), SmallBold, UIStyle::Text);
	Text(FVector2D(Right, 40.0), TEXT("MAIN · ") + Race->ReviewCameraName(false), SmallBold, UIStyle::Text);

	// Speed trace of both cars from the replay.
	Box(Plot, &Round8, UIStyle::Rgb(16, 17, 20));
	const double Window = AUIRaceDirector::ReviewWindow;
	auto ToPoint = [&](double Offset, double Kmh)
	{
		return Plot.Min + FVector2D((Offset + Window) / (2.0 * Window) * Plot.GetSize().X,
			Plot.GetSize().Y - FMath::Clamp(Kmh / PlotMaxKmh, 0.0, 1.0) * Plot.GetSize().Y) + Shift;
	};
	TArray<FVector2D> CarLine, OtherLine;
	TArray<FUISimReplayCar> Cars;
	double SafetyCarDistance = 0.0;
	bool bSafetyCar = false;
	for (int32 Sample = 0; Sample <= PlotSamples; ++Sample)
	{
		const double Offset = -Window + 2.0 * Window * Sample / PlotSamples;
		if (Session.ReplayAt(Incident.Time + Offset, Cars, SafetyCarDistance, bSafetyCar))
		{
			CarLine.Add(ToPoint(Offset, Cars[Incident.Car].Speed * 3.6));
			OtherLine.Add(ToPoint(Offset, Cars[Incident.OtherCar].Speed * 3.6));
		}
	}
	const FPaintGeometry Paint = Area.ToPaintGeometry();
	const double Now = Race->ReviewOffset();
	FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Paint, {ToPoint(0.0, 0.0), ToPoint(0.0, PlotMaxKmh)}, ESlateDrawEffect::None,
		FLinearColor(1.0f, 1.0f, 1.0f, 0.4f), true, 1.0f); // moment of the incident
	FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Paint, {ToPoint(Now, 0.0), ToPoint(Now, PlotMaxKmh)}, ESlateDrawEffect::None,
		UIStyle::Yellow, true, 1.0f); // replay position
	if (bTwoCars)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Paint, OtherLine, ESlateDrawEffect::None, OtherCarColor, true, 2.0f);
	}
	FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Paint, CarLine, ESlateDrawEffect::None, CarColor, true, 2.0f);
	Text(FVector2D(Plot.Min.X, Plot.Max.Y + 8.0), FString::Printf(TEXT("SPEED KM/H · -%.0f TO +%.0f S"), Window, Window), Small, UIStyle::Mute);
	Text(FVector2D(Plot.Min.X + 150.0, Plot.Max.Y + 8.0), CarCode, SmallBold, CarColor);
	if (bTwoCars)
	{
		Text(FVector2D(Plot.Min.X + 190.0, Plot.Max.Y + 8.0), OtherCode, SmallBold, OtherCarColor);
	}

	// Timeline -5..+5 s: incident in the middle, replay position in yellow.
	Box(Timeline, &Round7, Fill);
	auto BarX = [&](double Offset) { return Timeline.Min.X + (Offset + Window) / (2.0 * Window) * Timeline.GetSize().X; };
	Box(FBox2D(FVector2D(BarX(0.0) - 1.0, Timeline.Min.Y), FVector2D(BarX(0.0) + 1.0, Timeline.Max.Y)), White, UIStyle::Mute);
	Box(FBox2D(FVector2D(BarX(Now) - 3.0, Timeline.Min.Y - 2.0), FVector2D(BarX(Now) + 3.0, Timeline.Max.Y + 2.0)), &Round7, UIStyle::Yellow);

	// Buttons.
	const TArray<FControl> List = Controls();
	for (const FControl& Control : List)
	{
		const FLinearColor Face = !Control.bEnabled ? UIStyle::Rgb(24, 25, 29) : Control.bActive || (Control.Action == EAction::Confirm)
			? UIStyle::Yellow : Fill;
		Box(Control.Rect, &Round8, Control.bEnabled && (Control.bActive || Control.Action == EAction::Confirm) ? UIStyle::Yellow : Edge);
		Box(Control.Rect.ExpandBy(-1.0), &Round7, Face);
		FLinearColor Ink = Control.bEnabled && (Control.bActive || Control.Action == EAction::Confirm) ? UIStyle::Ink : UIStyle::Text;
		Ink.A = Control.bEnabled ? 1.0f : 0.35f;
		const FVector2D Size = Measure->Measure(Control.Label, SmallBold);
		Text(Control.Rect.GetCenter() - Size / 2.0, Control.Label, SmallBold, Ink);
	}

	// Verdict and hearing texts.
	if (const FUISimVerdict* Verdict = Session.VerdictFor(Incident.Id))
	{
		Text(FVector2D(Right, AccusedTop + 4.0), FString::Printf(TEXT("DECIDED · %s%s"), *AUIRaceDirector::PenaltyName(Verdict->Penalty).ToUpper(),
			Verdict->Car >= 0 ? *(TEXT(" FOR ") + Entries[Verdict->Car].Code.ToUpper()) : TEXT("")), SmallBold, UIStyle::Yellow);
	}
	else
	{
		Text(FVector2D(Right, AccusedTop + 5.0), TEXT("ACCUSED"), Small, UIStyle::Mute);
		Text(FVector2D(Right, HelpTop), ChosenPenalty >= 0 ? FString(PenaltyHelp[ChosenPenalty]) : FString(TEXT("Choose a penalty, then CONFIRM.")),
			Small, UIStyle::Mute);
		if (!Session.IsHeard(Incident.Id))
		{
			Text(FVector2D(Right + 100.0, HearingTop + 5.0), TEXT("both sides heard; 3 min wait in the race"), Small, UIStyle::Mute);
		}
	}
	if (Session.IsHeard(Incident.Id))
	{
		// Hearing (M11.7): what the recording shows next to what each driver claims. Comparing them is the player's job.
		const FUISimHearing Hearing = Session.Hearing(Incident.Id);
		static const TCHAR* Labels[] = {TEXT("Gap m"), TEXT("Braked later m"), TEXT("Moved over m"), TEXT("Measured"), TEXT("Breach (1 = yes)")};
		const double Columns[] = {Right + 160.0, Right + 215.0, Right + 270.0};
		Text(FVector2D(Right, TableTop), TEXT("HEARING"), SmallBold, UIStyle::Yellow);
		TextRight(Columns[0], TableTop, TEXT("EVIDENCE"), SmallBold, UIStyle::Mute);
		TextRight(Columns[1], TableTop, CarCode, SmallBold, CarColor);
		if (Hearing.bTwoCars)
		{
			TextRight(Columns[2], TableTop, OtherCode, SmallBold, OtherCarColor);
		}
		double Y = TableTop + 15.0;
		for (const FUISimHearingItem& Item : Hearing.Items)
		{
			Text(FVector2D(Right, Y), Labels[FMath::Clamp(Item.Kind, 0, 4)], Small, UIStyle::Text);
			TextRight(Columns[0], Y, FString::Printf(TEXT("%.1f"), Item.Evidence), Small, UIStyle::Text);
			TextRight(Columns[1], Y, FString::Printf(TEXT("%.1f"), Item.ClaimCar), Small, UIStyle::Text);
			if (Hearing.bTwoCars)
			{
				TextRight(Columns[2], Y, FString::Printf(TEXT("%.1f"), Item.ClaimOther), Small, UIStyle::Text);
			}
			Y += 14.0;
		}
		const double Wait = Session.HearingUntil(Incident.Id) - Session.Snapshot().Time;
		Text(FVector2D(Right, Y + 4.0), !Session.Snapshot().bFinished && Wait > 0.0
			? FString::Printf(TEXT("Hearing running: verdict possible in %.0f s"), Wait)
			: FString(TEXT("A statement far from the evidence is not true.")), Small, Wait > 0.0 ? UIStyle::Yellow : UIStyle::Mute);
	}
	Layer += 2;

	// Viewport pixels of the controls, for -UISimScript checks (click x y); written again when they change.
	FString Targets;
	for (const FControl& Control : List)
	{
		Targets += FString::Printf(TEXT("%s %.0f %.0f;"), *Control.Label, Control.Rect.Min.X, Control.Rect.Min.Y);
	}
	if (Targets != LoggedTargets)
	{
		LoggedTargets = Targets;
		const UWorld* World = GetWorld();
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
		if (ViewportWidget.IsValid())
		{
			const FVector2D Origin = FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
			auto Pixel = [&](const FVector2D& Local) { return FVector2D(Area.LocalToAbsolute(Local + Shift)) - Origin; };
			for (const FControl& Control : List)
			{
				const FVector2D At = Pixel(Control.Rect.GetCenter());
				UE_LOG(LogUISim, Display, TEXT("Review controls: '%s' at %.0f %.0f"), *Control.Label, At.X, At.Y);
			}
			for (double Offset : {-5.0, 0.0, 5.0})
			{
				const FVector2D At = Pixel(FVector2D(BarX(Offset), Timeline.GetCenter().Y));
				UE_LOG(LogUISim, Display, TEXT("Review controls: timeline %+.0f s at %.0f %.0f"), Offset, At.X, At.Y);
			}
		}
	}
	return Layer;
}

bool UUIReviewWidget::SeekTimeline(const FVector2D& Local)
{
	AUIRaceDirector* Race = Director.Get();
	const FBox2D Bar = Timeline.ExpandBy(FVector2D(0.0, 4.0));
	if (!Race || !Bar.IsInside(Local))
	{
		return false;
	}
	const double Window = AUIRaceDirector::ReviewWindow;
	const double Offset = FMath::RoundToDouble(((Local.X - Timeline.Min.X) / Timeline.GetSize().X * 2.0 * Window - Window) * 10.0) / 10.0;
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

FReply UUIReviewWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->IsReviewing() || !CardArea || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Local = FVector2D(CardArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition())) + FVector2D(1.0, 1.0);
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

FReply UUIReviewWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Dragging along the timeline scrubs the replay.
	const AUIRaceDirector* Race = Director.Get();
	if (Race && Race->IsReviewing() && CardArea && InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) &&
		SeekTimeline(FVector2D(CardArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition())) + FVector2D(1.0, 1.0)))
	{
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}
