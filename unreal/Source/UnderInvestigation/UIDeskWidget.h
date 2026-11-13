#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIDeskWidget.generated.h"

class AUIRaceDirector;
class UBorder;

// The desk behind all panels (M12): dark background with a window for the 3D picture, the cards and
// the top bar after the user's design: title, status banner, lap, session clock, sim speed buttons
// and weather. Only the sim speed buttons take clicks; the panels sit on top. Layout in UIStyle.h.
UCLASS()
class UUIDeskWidget : public UUserWidget
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
	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UBorder> SpeedArea; // the sim speed buttons; takes the clicks

	mutable bool bLogged = false; // click targets written to the log once
};
