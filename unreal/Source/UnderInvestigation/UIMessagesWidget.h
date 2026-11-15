#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIMessagesWidget.generated.h"

class AUIRaceDirector;

// The card "Race control messages" (M12.6, after the user's design): the player's own decisions
// (AUIRaceDirector::Messages()) and the marshal radio, newest first, each with race time and a colour bar.
UCLASS()
class UUIMessagesWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRaceDirector(AUIRaceDirector* InDirector) { Director = InDirector; }

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	TWeakObjectPtr<AUIRaceDirector> Director;
};
