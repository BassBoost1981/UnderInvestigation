#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIDeskWidget.generated.h"

class AUIRaceDirector;

// The desk behind all panels (M12.1): dark background with a window for the 3D picture, the cards
// and the top bar. Draws only; the panels sit on top and take the clicks. Layout in UIStyle.h.
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

private:
	TWeakObjectPtr<AUIRaceDirector> Director;
};
