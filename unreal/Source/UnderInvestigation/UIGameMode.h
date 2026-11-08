#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UIGameMode.generated.h"

// Spawns lighting and the race director into the empty startup map.
UCLASS()
class AUIGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AUIGameMode();
	virtual void BeginPlay() override;
};
