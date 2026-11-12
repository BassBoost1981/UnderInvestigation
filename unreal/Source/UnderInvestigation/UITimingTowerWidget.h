#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UITimingTowerWidget.generated.h"

class AUIRaceDirector;
class UBorder;
class UTextBlock;

// Timing tower (top left), drawn in C++: lap counter, then one row per car with position, team
// colour, driver code, gap, tyre and a status tag. The gap column shows the interval to the car
// ahead or the gap to the leader; a click on the mode label switches. "IN PIT" stands there
// only while the car is in the pit lane. A click on a row puts the TV camera on that car.
// Only formats what the snapshot already contains; ordering and gaps come from the core.
UCLASS()
class UUITimingTowerWidget : public UUserWidget
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

private:
	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UBorder> TowerArea; // painted rows; takes the clicks
	UPROPERTY()
	TObjectPtr<UTextBlock> Footer;

	bool bInterval = true; // gap column: interval to the car ahead (true) or gap to the leader
	mutable FVector2D LoggedAt = FVector2D(-1.0, -1.0); // position of the mode label last logged
};
