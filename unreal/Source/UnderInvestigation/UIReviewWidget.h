#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIRaceDirector.h"
#include "UIReviewWidget.generated.h"

class AUIRaceDirector;
class UBorder;
class UImage;

// The review card (M12.6, in the style of the user's design; the design has no review). While an incident
// is reviewed it takes the place of the track map: the second camera (picture-in-picture), a speed trace
// of both cars, the timeline (click or drag) with -1 s, pause, +1 s, camera and end, the accused car, the
// penalty with its explanation and CONFIRM, and the hearing with evidence and statements (M11.7).
// Every action goes through AUIRaceDirector::Execute().
UCLASS()
class UUIReviewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRaceDirector(AUIRaceDirector* InDirector) { Director = InDirector; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	enum class EAction : uint8 { Command, ChoosePenalty, Confirm };
	struct FControl
	{
		FBox2D Rect; // in card coordinates
		FString Label;
		EAction Action = EAction::Command;
		FUICommand Command;
		int32 Penalty = 0; // ChoosePenalty
		bool bEnabled = true;
		bool bActive = false;
	};
	TArray<FControl> Controls() const; // for the current state
	bool SeekTimeline(const FVector2D& Local);

	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UBorder> CardArea; // takes the clicks while reviewing
	UPROPERTY()
	TObjectPtr<UImage> PipImage;

	int32 ChosenPenalty = -1;       // penalty picked, not yet confirmed
	int32 ChosenForIncident = -1;   // incident id the choice belongs to
	double LastDragOffset = 1000.0; // last timeline position sent while dragging
	mutable FString LoggedTargets;  // click targets last written to the log
};
