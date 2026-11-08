#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIRatingWidget.generated.h"

class AUIRaceDirector;
class UBorder;
class UTextBlock;

// Rating screen after the flag: score per category and, for every incident,
// the verdict given next to what really happened (ground truth revealed).
UCLASS()
class UUIRatingWidget : public UUserWidget
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
	TObjectPtr<UBorder> Panel;
	UPROPERTY()
	TObjectPtr<UTextBlock> Body;
};
