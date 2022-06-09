// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"
#include "Containers/Map.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

class ULevelInstanceSubsystem;
class UActorDescContainer;
enum class ELevelInstanceRuntimeBehavior : uint8;

/**
 * ActorDesc for Actors that are part of a LevelInstanceActor Level.
 */
class ENGINE_API FLevelInstanceActorDesc : public FWorldPartitionActorDesc
{
#if WITH_EDITOR

public:
	FLevelInstanceActorDesc();
	virtual ~FLevelInstanceActorDesc() override;

	inline FName GetLevelPackage() const { return LevelPackage; }

	virtual bool IsContainerInstance() const override;
	virtual bool GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const override;

protected:
	virtual void Init(const AActor* InActor) override;
	virtual void Init(const FWorldPartitionActorDescInitData& DescData) override;
	virtual bool Equals(const FWorldPartitionActorDesc* Other) const override;
	virtual void TransferFrom(const FWorldPartitionActorDesc* From) override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void SetContainer(UActorDescContainer* InContainer) override;

	FName LevelPackage;
	FTransform LevelInstanceTransform;
	ELevelInstanceRuntimeBehavior DesiredRuntimeBehavior;

	TWeakObjectPtr<UActorDescContainer> LevelInstanceContainer;

private:
	void RegisterContainerInstance(UWorld* InWorld);
	void UnregisterContainerInstance();
#endif
};
