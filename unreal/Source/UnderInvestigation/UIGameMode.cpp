#include "UIGameMode.h"

#include "UIRaceDirector.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Components/SkyAtmosphereComponent.h"
#include "GameFramework/SpectatorPawn.h"

AUIGameMode::AUIGameMode()
{
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}

void AUIGameMode::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();

	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-40.0, 30.0, 0.0));
	Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	if (UDirectionalLightComponent* SunLight = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
	{
		SunLight->SetAtmosphereSunLight(true);
	}

	AActor* Atmosphere = World->SpawnActor<AActor>();
	USkyAtmosphereComponent* AtmosphereComponent = NewObject<USkyAtmosphereComponent>(Atmosphere);
	Atmosphere->SetRootComponent(AtmosphereComponent);
	AtmosphereComponent->RegisterComponent();

	ASkyLight* Sky = World->SpawnActor<ASkyLight>();
	USkyLightComponent* SkyComponent = Sky->GetLightComponent();
	SkyComponent->SetMobility(EComponentMobility::Movable);
	SkyComponent->SetRealTimeCapture(true);
	SkyComponent->RecaptureSky();

	World->SpawnActor<AUIRaceDirector>();
}
