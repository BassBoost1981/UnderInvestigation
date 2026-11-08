#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIRadioWidget.generated.h"

class AUIRaceDirector;
class UTextBlock;

// Marshal radio log (bottom left): the latest reports from the marshal posts.
UCLASS()
class UUIRadioWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRaceDirector(AUIRaceDirector* InDirector) { Director = InDirector; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> Lines; // newest last
};
