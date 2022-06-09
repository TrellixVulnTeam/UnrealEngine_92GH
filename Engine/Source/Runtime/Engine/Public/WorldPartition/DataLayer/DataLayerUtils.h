// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "WorldPartition/DataLayer/DataLayerType.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "WorldPartition/WorldPartitionStreamingGeneration.h"

class FWorldDataLayersActorDesc;
class FWorldPartitionActorDesc;
class UActorDescContainer;
class AWorldDataLayers;

class ENGINE_API FDataLayerUtils
{
public:
#if WITH_EDITOR
	static const TCHAR* GetDataLayerIconName(EDataLayerType DataLayerType)
	{
		static constexpr const TCHAR* IconNameByType[static_cast<int>(EDataLayerType::Size)] = { TEXT("DataLayer.Runtime") , TEXT("DataLayer.Editor"), TEXT("") };
		return IconNameByType[static_cast<uint32>(DataLayerType)];
	}

	static TArray<FName> ResolvedDataLayerInstanceNames(const FWorldPartitionActorDesc* InActorDesc, const FWorldDataLayersActorDesc* InWorldDataLayersActorDesc = nullptr, UWorld* InWorld = nullptr, bool* bOutIsResultValid = nullptr);
	
	static bool ResolveRuntimeDataLayerInstanceNames(const FWorldPartitionActorDescView& InActorDescView, const FActorDescViewMap& ActorDescViewMap, TArray<FName>& OutRuntimeDataLayerInstanceNames);
#endif

#if DATALAYER_TO_INSTANCE_RUNTIME_CONVERSION_ENABLED
	UE_DEPRECATED(5.1, "Label usage is deprecated.")
	static FName GetSanitizedDataLayerLabel(FName InDataLayerLabel)
	{
		return FName(InDataLayerLabel.ToString().TrimStartAndEnd().Replace(TEXT("\""), TEXT("")));
	}
#endif
};