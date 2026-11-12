#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIRaceDirector.h"
#include "UIIncidentWidget.generated.h"

class AUIRaceDirector;
class UBorder;
class UImage;
class USizeBox;
class UTextBlock;
class UVerticalBox;

// Incident list (top right). While reviewing it also shows the second camera
// (picture-in-picture) and a speed trace of both cars around the incident.
// Mouse (M8.7): click a row to review it; in the review a timeline (click or drag), buttons for
// -1 s, pause, +1 s, camera and end, and the penalty dialog (accused, penalty with explanation,
// confirm). Every action goes through AUIRaceDirector::Execute().
UCLASS()
class UUIIncidentWidget : public UUserWidget
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
		FBox2D Rect;
		FString Label;
		EAction Action = EAction::Command;
		FUICommand Command;
		int32 Penalty = 0; // ChoosePenalty
		bool bEnabled = true;
		bool bActive = false;
	};
	TArray<FControl> Controls() const; // review buttons and penalty dialog, in ControlArea coordinates
	FBox2D TimelineRect() const;
	bool SeekTimeline(const FVector2D& Local);
	UTextBlock* MakeText(const FLinearColor& Color, int32 Size = 12);

	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UTextBlock> Title;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> Rows;
	UPROPERTY()
	TObjectPtr<UVerticalBox> ReviewSection;
	UPROPERTY()
	TObjectPtr<UTextBlock> ReviewInfo;
	UPROPERTY()
	TObjectPtr<UImage> PipImage;
	UPROPERTY()
	TObjectPtr<USizeBox> PlotArea;
	UPROPERTY()
	TObjectPtr<UTextBlock> PlotLegend;
	UPROPERTY()
	TObjectPtr<UTextBlock> ReviewControls;
	UPROPERTY()
	TObjectPtr<UBorder> ControlArea;

	TArray<int32> RowIncident;       // incident index shown in each row, -1 = none
	int32 ChosenPenalty = -1;        // penalty picked in the dialog, not yet confirmed
	int32 ChosenForIncident = -1;    // incident id the choice belongs to
	double LastDragOffset = 1000.0;  // last timeline position sent while dragging
	mutable TSet<int32> LoggedRows;  // rows whose click position is in the log
	mutable FVector2D LoggedControlsAt = FVector2D(-1.0, -1.0); // control area position last logged
};
