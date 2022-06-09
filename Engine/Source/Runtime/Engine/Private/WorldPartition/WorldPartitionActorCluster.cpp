// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorCluster.h"

#if WITH_EDITOR

#include "Algo/Transform.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

template<class LayerNameContainer>
TSet<const UDataLayerInstance*> GetDataLayerInstances(UWorld* InWorld, const LayerNameContainer& DataLayerInstanceNames)
{
	TSet<const UDataLayerInstance*> DataLayerInstances;
	if (const UDataLayerSubsystem* DataLayerSubsystem = UWorld::GetSubsystem<UDataLayerSubsystem>(InWorld))
	{
		for (const FName& DataLayerInstanceName : DataLayerInstanceNames)
		{
			if (const UDataLayerInstance* DataLayerInstance = DataLayerSubsystem->GetDataLayerInstance(DataLayerInstanceName))
			{
				if (ensure(DataLayerInstance->IsRuntime()))
				{
					DataLayerInstances.Add(DataLayerInstance);
				}
			}
		}
	}
	return DataLayerInstances;
}

FActorCluster::FActorCluster(UWorld* InWorld, const FWorldPartitionActorDescView& InActorDescView)
	: bIsSpatiallyLoaded(InActorDescView.GetIsSpatiallyLoaded())
	, RuntimeGrid(InActorDescView.GetRuntimeGrid())
	, Bounds(InActorDescView.GetBounds())
{
	Actors.Add(InActorDescView.GetGuid());
	DataLayers = GetDataLayerInstances(InWorld, InActorDescView.GetRuntimeDataLayers());
	DataLayersID = FDataLayersID(DataLayers.Array());
}

void FActorCluster::Add(const FActorCluster& InActorCluster)
{
	check(RuntimeGrid == InActorCluster.RuntimeGrid);
	check(DataLayersID == InActorCluster.DataLayersID);
	check(bIsSpatiallyLoaded == InActorCluster.bIsSpatiallyLoaded);

	// Merge bounds
	Bounds += InActorCluster.Bounds;

	// Merge Actors
	Actors.Append(InActorCluster.Actors);
}

FActorClusterInstance::FActorClusterInstance(const FActorCluster* InCluster, const FActorContainerInstance* InContainerInstance)
	: Cluster(InCluster)
	, ContainerInstance(InContainerInstance)
{
	Bounds = Cluster->Bounds.TransformBy(ContainerInstance->Transform);
	
	TSet<const UDataLayerInstance*> DataLayerSet;
	DataLayerSet.Reserve(Cluster->DataLayers.Num() + ContainerInstance->DataLayers.Num());
	DataLayerSet.Append(Cluster->DataLayers);
	DataLayerSet.Append(ContainerInstance->DataLayers);

	DataLayers = DataLayerSet.Array();
}

FActorClusterContext::FActorClusterContext(TArray<FActorContainerInstance>&& InContainerInstances, FFilterActorDescViewFunc InFilterActorDescViewFunc)
	: FilterActorDescViewFunc(InFilterActorDescViewFunc)
	, ContainerInstances(MoveTemp(InContainerInstances))
{
	for (const FActorContainerInstance& ContainerInstance : ContainerInstances)
	{
		const TArray<FActorCluster>& NewClusters = CreateActorClusters(ContainerInstance);
		for (const FActorCluster& Cluster : NewClusters)
		{
			ClusterInstances.Emplace(&Cluster, &ContainerInstance);
		}
	}
}

FActorContainerInstance::FActorContainerInstance(const FActorContainerID& InID, const FTransform& InTransform, const FBox& InBounds, const TSet<FName>& InRuntimeDataLayers, EContainerClusterMode InClusterMode, const UActorDescContainer* InContainer, TMap<FGuid, FWorldPartitionActorDescView*> InActorDescViewMap)
	: ID(InID)
	, Transform(InTransform)
	, Bounds(InBounds)
	, ClusterMode(InClusterMode)
	, Container(InContainer)
	, ActorDescViewMap(InActorDescViewMap)
{
	DataLayers = GetDataLayerInstances(InContainer->GetWorld(), InRuntimeDataLayers);
}

const FWorldPartitionActorDescView& FActorContainerInstance::GetActorDescView(const FGuid& InGuid) const
{
	return *ActorDescViewMap.FindChecked(InGuid);
}

FActorInstance::FActorInstance()
	: ContainerInstance(nullptr)
{}

FActorInstance::FActorInstance(const FGuid& InActor, const FActorContainerInstance* InContainerInstance)
	: Actor(InActor)
	, ContainerInstance(InContainerInstance)
{
	check(ContainerInstance);
}

const FWorldPartitionActorDescView& FActorInstance::GetActorDescView() const
{
	return ContainerInstance->GetActorDescView(Actor);
}

void CreateActorCluster(const FWorldPartitionActorDescView& ActorDescView, TMap<FGuid, FActorCluster*>& ActorToActorCluster, TSet<FActorCluster*>& ActorClustersSet, UWorld* World, const TMap<FGuid, FWorldPartitionActorDescView*>& ActorDescViewMap)
{
	const FGuid& ActorGuid = ActorDescView.GetGuid();

	FActorCluster* ActorCluster = ActorToActorCluster.FindRef(ActorGuid);
	if (!ActorCluster)
	{
		ActorCluster = new FActorCluster(World, ActorDescView);
		ActorClustersSet.Add(ActorCluster);
		ActorToActorCluster.Add(ActorGuid, ActorCluster);
	}

	for (const FGuid& ReferenceGuid : ActorDescView.GetReferences())
	{
		if (const FWorldPartitionActorDescView* const* ReferenceActorDescViewPtr = ActorDescViewMap.Find(ReferenceGuid))
		{
			const FWorldPartitionActorDescView* ReferenceActorDescView = *ReferenceActorDescViewPtr;

			// Ignore references from spatially loaded actors to non-spatially loaded ones (at this point, they are in the same data layers).
			if (ActorDescView.GetIsSpatiallyLoaded() && !ReferenceActorDescView->GetIsSpatiallyLoaded())
			{
				continue;
			}

			FActorCluster* ReferenceCluster = ActorToActorCluster.FindRef(ReferenceGuid);
			if (ReferenceCluster)
			{
				if (ReferenceCluster != ActorCluster)
				{
					// Merge reference cluster in Actor's cluster
					ActorCluster->Add(*ReferenceCluster);
					for (const FGuid& ReferenceClusterActorGuid : ReferenceCluster->Actors)
					{
						ActorToActorCluster[ReferenceClusterActorGuid] = ActorCluster;
					}
					ActorClustersSet.Remove(ReferenceCluster);
					delete ReferenceCluster;
				}
			}
			else
			{
				// Put Reference in Actor's cluster
				ActorCluster->Add(FActorCluster(World, *ReferenceActorDescView));
			}

			// Map its cluster
			ActorToActorCluster.Add(ReferenceGuid, ActorCluster);
		}
	}
}

const FActorContainerInstance* FActorClusterContext::GetClusterInstance(const FActorContainerID& InContainerID) const
{
	for (const FActorContainerInstance& ContainerInstance : ContainerInstances)
	{
		if (ContainerInstance.ID == InContainerID)
		{
			return &ContainerInstance;
		}
	}

	return nullptr;
}

FActorContainerInstance* FActorClusterContext::GetClusterInstance(const UActorDescContainer* InContainer)
{
	for (FActorContainerInstance& ContainerInstance : ContainerInstances)
	{
		if (ContainerInstance.Container == InContainer)
		{
			return &ContainerInstance;
		}
	}

	return nullptr;
}

const FActorContainerInstance* FActorClusterContext::GetClusterInstance(const UActorDescContainer* InContainer) const
{
	for (const FActorContainerInstance& ContainerInstance : ContainerInstances)
	{
		if (ContainerInstance.Container == InContainer)
		{
			return &ContainerInstance;
		}
	}

	return nullptr;
}

void FActorClusterContext::CreateActorClusters(UWorld* World, const TMap<FGuid, FWorldPartitionActorDescView*>& ActorDescViewMap, TArray<FActorCluster>& OutActorClusters, FActorClusterContext::FFilterActorDescViewFunc InFilterActorDescViewFunc)
{
	TMap<FGuid, FActorCluster*> ActorToActorCluster;
	TSet<FActorCluster*> ActorClustersSet;

	for (auto& ActorDescViewPair : ActorDescViewMap)
	{
		const FWorldPartitionActorDescView& ActorDescView = *ActorDescViewPair.Value;

		if (!InFilterActorDescViewFunc || InFilterActorDescViewFunc(ActorDescView))
		{
			CreateActorCluster(ActorDescView, ActorToActorCluster, ActorClustersSet, World, ActorDescViewMap);
		}
	}

	OutActorClusters.Reserve(ActorClustersSet.Num());
	Algo::Transform(ActorClustersSet, OutActorClusters, [](FActorCluster* ActorCluster) { return MoveTemp(*ActorCluster); });
	for (FActorCluster* ActorCluster : ActorClustersSet) { delete ActorCluster; }
}

void FActorClusterContext::CreateActorClusters(UWorld* World, const TMap<FGuid, FWorldPartitionActorDescView*>& ActorDescViewMap, TArray<FActorCluster>& OutActorClusters)
{
	CreateActorClusters(World, ActorDescViewMap, OutActorClusters, nullptr);
}

const TArray<FActorCluster>& FActorClusterContext::CreateActorClusters(const FActorContainerInstance& ContainerInstance)
{
	if (TArray<FActorCluster>* FoundClusters = Clusters.Find(ContainerInstance.Container))
	{
		return *FoundClusters;
	}
		
	TArray<FActorCluster>& ActorClusters = Clusters.Add(ContainerInstance.Container);
	
	CreateActorClusters(ContainerInstance.Container->GetWorld(), ContainerInstance.ActorDescViewMap, ActorClusters, FilterActorDescViewFunc);
		
	return ActorClusters;
}
#endif // #if WITH_EDITOR
