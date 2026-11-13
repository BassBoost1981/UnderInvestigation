#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIWeatherWidget.generated.h"

class AUIRaceDirector;

// Weather on the camera picture (M9.7): rain streaks and a darker tint while it rains, and the start
// lights. The weather values stand in the top bar of the desk (UIDeskWidget).
UCLASS()
class UUIWeatherWidget : public UUserWidget
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
