#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIStrategyWidget.generated.h"

class AUIRaceDirector;
class UBorder;

// The card "Strategy · stints" (M12.7, after the user's design, below the live timing): one row per driver
// in race order, in two columns of ten. Each row shows the stints driven in the colour of the compound,
// the rest of the current set up to the planned stop as a projection, the leader's lap as a line, and
// the next stop (lap, TO FLAG, OUT). A click on a row puts the TV camera on that car.
UCLASS()
class UUIStrategyWidget : public UUserWidget
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
	FBox2D RowRect(int32 Row) const; // in card coordinates

	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UBorder> CardArea; // takes the clicks

	mutable bool bLogged = false; // row click positions written once
};
