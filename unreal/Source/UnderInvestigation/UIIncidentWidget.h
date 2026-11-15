#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIRaceDirector.h"
#include "UIIncidentWidget.generated.h"

class AUIRaceDirector;
class UBorder;

// The card "Incidents" (M12.6, after the user's design): the open incidents, newest first, each with
// its state (NEW, NOTED, UNDER INVESTIGATION, IN REVIEW, HEARING) and buttons CAM (TV camera on the
// car), NOTED, INVESTIGATE (opens the review card), HEARING and DISMISS (no further action). Decided
// incidents leave the list and stand in the race control messages. Three fit; the window follows the
// incident selected by key. Every action goes through AUIRaceDirector::Execute().
UCLASS()
class UUIIncidentWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRaceDirector(AUIRaceDirector* InDirector) { Director = InDirector; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	struct FButton
	{
		FBox2D Rect; // in card coordinates
		FString Label;
		FUICommand Command;
		FLinearColor Color = FLinearColor::White;
		bool bEnabled = true;
	};
	struct FEntry
	{
		int32 Index = 0; // into Session().Incidents()
		double Top = 0.0;
		FString Title;
		FString Detail;
		FString State;
		FLinearColor StateColor = FLinearColor::White;
		TArray<FButton> Buttons;
	};
	TArray<FEntry> Entries(int32& OutOpen) const; // what the card shows now

	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UBorder> CardArea; // takes the clicks

	mutable FString LoggedTargets; // click targets last written to the log
};
