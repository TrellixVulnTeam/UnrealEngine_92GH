// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGHelpers.h"
#include "PCGVolume.h"
#include "PCGManagedResource.h"
#include "Data/PCGDifferenceData.h"
#include "Data/PCGIntersectionData.h"
#include "Data/PCGLandscapeData.h"
#include "Data/PCGLandscapeSplineData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGPrimitiveData.h"
#include "Data/PCGSplineData.h"
#include "Data/PCGUnionData.h"
#include "Data/PCGVolumeData.h"
#include "Graph/PCGGraphExecutor.h"
#include "Grid/PCGPartitionActor.h"
#include "Helpers/PCGActorHelpers.h"

#include "ActorPartition/ActorPartitionSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/ShapeComponent.h"
#include "GameFramework/Volume.h"
#include "Kismet/GameplayStatics.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeSplineActor.h"
#include "LandscapeSplinesComponent.h"
#include "Misc/ScopeLock.h"
#include "WorldPartition/WorldPartition.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE "UPCGComponent"

namespace PCGComponent
{
	const bool bSaveOnCleanupAndGenerate = false;
}

bool UPCGComponent::CanPartition() const
{
	// Support/Force partitioning on non-PCG partition actors in WP worlds.
	return GetOwner() && GetOwner()->GetWorld() && GetOwner()->GetWorld()->GetWorldPartition() != nullptr && Cast<APCGPartitionActor>(GetOwner()) == nullptr;
}

bool UPCGComponent::IsPartitioned() const
{
	return bIsPartitioned && CanPartition();
}

void UPCGComponent::SetGraph(UPCGGraph* InGraph)
{
	if(Graph == InGraph)
	{
		return;
	}

#if WITH_EDITOR
	if (Graph)
	{
		Graph->OnGraphChangedDelegate.RemoveAll(this);
	}
#endif

	Graph = InGraph;

#if WITH_EDITOR
	if (InGraph)
	{
		Graph->OnGraphChangedDelegate.AddUObject(this, &UPCGComponent::OnGraphChanged);
	}

	OnGraphChanged(Graph, true, false);
#endif
}

void UPCGComponent::AddToManagedResources(UPCGManagedResource* InResource)
{
	if (InResource)
	{
		FScopeLock ResourcesLock(&GeneratedResourcesLock);
		GeneratedResources.Add(InResource);
	}
}

void UPCGComponent::ForEachManagedResource(TFunctionRef<void(UPCGManagedResource*)> Func)
{
	FScopeLock ResourcesLock(&GeneratedResourcesLock);
	for (TObjectPtr<UPCGManagedResource> ManagedResource : GeneratedResources)
	{
		Func(ManagedResource);
	}
}

bool UPCGComponent::ShouldGenerate(bool bForce) const
{
	if (!bActivated || !Graph || !GetSubsystem())
	{
		return false;
	}

	if (IsPartitioned())
	{
#if WITH_EDITOR
		return !GIsPlayInEditorWorld;
#else
		return false;
#endif
	}
	else
	{
		return (!bGenerated || 
#if WITH_EDITOR
			bDirtyGenerated || 
#endif
			bForce);
	}
}

void UPCGComponent::SetPropertiesFromOriginal(const UPCGComponent* Original)
{
	check(Original);

	EPCGComponentInput NewInputType = Original->InputType;

	// If we're inheriting properties from another component that would have targeted a "special" actor
	// then we must make sure we update the InputType appropriately
	if (NewInputType == EPCGComponentInput::Actor)
	{
		if(Cast<ALandscapeProxy>(Original->GetOwner()) != nullptr && Cast<ALandscapeProxy>(GetOwner()) == nullptr)
		{
			NewInputType = EPCGComponentInput::Landscape;
		}
	}

#if WITH_EDITOR
	const bool bHasDirtyInput = InputType != NewInputType;
	const bool bHasDirtyExclusions = !(ExcludedTags.Num() == Original->ExcludedTags.Num() && ExcludedTags.Includes(Original->ExcludedTags));
	const bool bIsDirty = bHasDirtyInput || bHasDirtyExclusions || Seed != Original->Seed || Graph != Original->Graph;

	if (bHasDirtyExclusions)
	{
		TeardownTrackingCallbacks();
		ExcludedTags = Original->ExcludedTags;
		SetupTrackingCallbacks();
		RefreshTrackingData();
	}
#else
	ExcludedTags = Original->ExcludedTags;
#endif

	InputType = NewInputType;
	Seed = Original->Seed;
	SetGraph(Original->Graph);

#if WITH_EDITOR
	// Note that while we dirty here, we won't trigger a refresh since we don't have the required context
	if (bIsDirty)
	{
		Modify();
		DirtyGenerated(bHasDirtyInput);
	}
#endif
}

void UPCGComponent::Generate()
{
#if WITH_EDITOR
	if (bIsGenerating)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("PCGGenerate", "Execute generation on PCG component"));
#endif

	Generate(/*bForce=*/PCGComponent::bSaveOnCleanupAndGenerate);
}

void UPCGComponent::Generate(bool bForce)
{
#if WITH_EDITOR
	if (bIsGenerating)
	{
		return;
	}
#endif

	FPCGTaskId TaskId = GenerateInternal(bForce, {});

#if WITH_EDITOR
	if (TaskId != InvalidTaskId)
	{
		bIsGenerating = true;
	}
#endif
}

FPCGTaskId UPCGComponent::GenerateInternal(bool bForce, const TArray<FPCGTaskId>& TaskDependencies)
{
	FPCGTaskId TaskId = InvalidTaskId;

	if (!ShouldGenerate(bForce))
	{
		return InvalidTaskId;
	}

#if WITH_EDITOR
	if (bForce && bGenerated && !bDirtyGenerated)
	{
		// TODO: generate new seed
		++Seed;
	}
#endif

	Modify();

	if (IsPartitioned())
	{
#if WITH_EDITOR
		TaskId = GetSubsystem()->DelayGenerateGraph(this, /*bSave=*/bForce);
#endif
	}
	else
	{
		// Immediate operation: cleanup beforehand
		if (bGenerated)
		{
			CleanupInternal(/*bRemoveComponents=*/false);
		}

		const FBox NewBounds = GetGridBounds();
		if (NewBounds.IsValid)
		{
			TaskId = GetSubsystem()->ScheduleComponent(this, TaskDependencies);
		}
	}

	return TaskId;
}

bool UPCGComponent::GetActorsFromTags(const TSet<FName>& InTags, TSet<TWeakObjectPtr<AActor>>& OutActors, bool bCullAgainstLocalBounds)
{
	UWorld* World = GetWorld();

	if (!World)
	{
		return false;
	}

	FBox LocalBounds = bCullAgainstLocalBounds ? GetGridBounds() : FBox(EForceInit::ForceInit);

	TArray<AActor*> PerTagActors;

	OutActors.Reset();

	bool bHasValidTag = false;
	for (const FName& Tag : InTags)
	{
		if (Tag != NAME_None)
		{
			bHasValidTag = true;
			UGameplayStatics::GetAllActorsWithTag(World, Tag, PerTagActors);

			for (AActor* Actor : PerTagActors)
			{
				if (!bCullAgainstLocalBounds || LocalBounds.Intersect(GetGridBounds(Actor)))
				{
					OutActors.Emplace(Actor);
				}
			}

			PerTagActors.Reset();
		}
	}

	return bHasValidTag;
}

void UPCGComponent::PostProcessGraph(const FBox& InNewBounds, bool bInGenerated)
{
	LastGeneratedBounds = InNewBounds;

	if (bInGenerated)
	{
		CleanupUnusedManagedResources();

		bGenerated = true;

#if WITH_EDITOR
		bDirtyGenerated = false;
		bIsGenerating = false;
		OnPCGGraphGeneratedDelegate.Broadcast(this);
#endif
	}
}

void UPCGComponent::Cleanup()
{
	if (!bGenerated || !GetSubsystem())
	{
		return;
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(LOCTEXT("PCGCleanup", "Clean up PCG component"));
#endif

	Cleanup(/*bRemoveComponents=*/true, /*bSave=*/PCGComponent::bSaveOnCleanupAndGenerate);
}

void UPCGComponent::Cleanup(bool bRemoveComponents, bool bSave)
{
	if (!bGenerated || !GetSubsystem())
	{
		return;
	}

	if (IsPartitioned())
	{
#if WITH_EDITOR
		if (!GIsPlayInEditorWorld)
		{
			Modify();
			GetSubsystem()->CleanupGraph(this, LastGeneratedBounds, bRemoveComponents, bSave);
			bGenerated = false;
		}
#endif
	}
	else
	{
		CleanupInternal(bRemoveComponents);
	}

#if WITH_EDITOR
	OnPCGGraphCleanedDelegate.Broadcast(this);
#endif
}

void UPCGComponent::CleanupInternal(bool bRemoveComponents)
{
	TSet<TSoftObjectPtr<AActor>> ActorsToDelete;
	CleanupInternal(bRemoveComponents, ActorsToDelete);
	UPCGActorHelpers::DeleteActors(GetWorld(), ActorsToDelete.Array());
}

void UPCGComponent::CleanupInternal(bool bHardCleanup, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete)
{
	if (!bGenerated || IsPartitioned())
	{
		return;
	}

	Modify();
	bGenerated = false;

	FScopeLock ResourcesLock(&GeneratedResourcesLock);
	for (int32 ResourceIndex = GeneratedResources.Num() - 1; ResourceIndex >= 0; --ResourceIndex)
	{
		check(GeneratedResources[ResourceIndex]);
		if (GeneratedResources[ResourceIndex]->Release(bHardCleanup, OutActorsToDelete))
		{
			GeneratedResources.RemoveAtSwap(ResourceIndex);
		}
	}
}

void UPCGComponent::CleanupUnusedManagedResources()
{
	FScopeLock ResourcesLock(&GeneratedResourcesLock);
	for (int32 ResourceIndex = GeneratedResources.Num() - 1; ResourceIndex >= 0; --ResourceIndex)
	{
		check(GeneratedResources[ResourceIndex]);
		if (GeneratedResources[ResourceIndex]->ReleaseIfUnused())
		{
			GeneratedResources.RemoveAtSwap(ResourceIndex);
		}
	}
}

void UPCGComponent::BeginPlay()
{
	Super::BeginPlay();

	if(bActivated && !bGenerated && !IsPartitioned())
	{
		Generate(/*bForce=*/false);
		bRuntimeGenerated = true;
	}
}

void UPCGComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

#if WITH_EDITOR
	SetupActorCallbacks();
#endif
}

void UPCGComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
#if WITH_EDITOR
	// This is inspired by UChildActorComponent::DestroyChildActor()
	// In the case of level change or exit, the subsystem will be null
	if (UPCGSubsystem* Subsystem = GetSubsystem())
	{
		// The RF_BeginDestroyed flag is set when the object is being unloaded, but not in the editor-destroy context we're interested in.
		if (!HasAnyFlags(RF_BeginDestroyed) && !IsUnreachable() && IsPartitioned() && !GetOwner()->GetWorld()->IsGameWorld())
		{
			Subsystem->CleanupPartitionActors(LastGeneratedBounds);
		}
	}
#endif

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UPCGComponent::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if (!ExclusionTags_DEPRECATED.IsEmpty() && ExcludedTags.IsEmpty())
	{
		ExcludedTags.Append(ExclusionTags_DEPRECATED);
		ExclusionTags_DEPRECATED.Reset();
	}

	/** Deprecation code, should be removed once generated data has been updated */
	if (bGenerated && GeneratedResources.Num() == 0)
	{
		TArray<UInstancedStaticMeshComponent*> ISMCs;
		GetOwner()->GetComponents<UInstancedStaticMeshComponent>(ISMCs);

		for (UInstancedStaticMeshComponent* ISMC : ISMCs)
		{
			if (ISMC->ComponentTags.Contains(GetFName()))
			{
				UPCGManagedISMComponent* ManagedComponent = NewObject<UPCGManagedISMComponent>(this);
				ManagedComponent->GeneratedComponent = ISMC;
				GeneratedResources.Add(ManagedComponent);
			}
		}

		if (GeneratedActors_DEPRECATED.Num() > 0)
		{
			UPCGManagedActors* ManagedActors = NewObject<UPCGManagedActors>(this);
			ManagedActors->GeneratedActors = GeneratedActors_DEPRECATED;
			GeneratedResources.Add(ManagedActors);
			GeneratedActors_DEPRECATED.Reset();
		}
	}
#endif

#if WITH_EDITOR
	SetupActorCallbacks();
	SetupTrackingCallbacks();

	if (TrackedLandscape.IsValid())
	{
		SetupLandscapeTracking();
	}
	else
	{
		UpdateTrackedLandscape(/*bBoundsCheck=*/false);
	}	

	if (Graph)
	{
		Graph->OnGraphChangedDelegate.AddUObject(this, &UPCGComponent::OnGraphChanged);
	}
#endif
}

void UPCGComponent::BeginDestroy()
{
#if WITH_EDITOR
	if (Graph)
	{
		Graph->OnGraphChangedDelegate.RemoveAll(this);
	}

	TeardownLandscapeTracking();
	TeardownTrackingCallbacks();
	TeardownActorCallbacks();
#endif

	Super::BeginDestroy();
}

#if WITH_EDITOR
void UPCGComponent::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange)
	{
		const FName PropName = PropertyAboutToChange->GetFName();

		if (PropName == GET_MEMBER_NAME_CHECKED(UPCGComponent, Graph) && Graph)
		{
			Graph->OnGraphChangedDelegate.RemoveAll(this);
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UPCGComponent, ExcludedTags))
		{
			TeardownTrackingCallbacks();
		}
	}

	Super::PreEditChange(PropertyAboutToChange);
}

void UPCGComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!PropertyChangedEvent.Property)
	{
		return;
	}

	const FName PropName = PropertyChangedEvent.Property->GetFName();

	// Important note: all property changes already go through the OnObjectPropertyChanged, 
	// So there is no need to add cases that do simple Refresh() calls
	if (PropName == GET_MEMBER_NAME_CHECKED(UPCGComponent, bIsPartitioned))
	{
		if (CanPartition())
		{
			if (bActivated)
			{
				bool bIsNowPartitioned = bIsPartitioned;
				bIsPartitioned = !bIsPartitioned;
				// First, we'll cleanup
				bActivated = false;
				Refresh();
				// Then do a normal refresh
				bActivated = true;
				bIsPartitioned = bIsNowPartitioned;
				DirtyGenerated();
				Refresh();
			}
		}
		else
		{
			// Just ignore the change
			bIsPartitioned = false;
		}
	}
	else if (PropName == GET_MEMBER_NAME_CHECKED(UPCGComponent, Graph))
	{
		if (Graph)
		{
			Graph->OnGraphChangedDelegate.AddUObject(this, &UPCGComponent::OnGraphChanged);
		}

		OnGraphChanged(Graph, /*bIsStructural=*/true, /*bShouldRefresh=*/true);
	}
	else if (PropName == GET_MEMBER_NAME_CHECKED(UPCGComponent, InputType))
	{
		UpdateTrackedLandscape();
		DirtyGenerated(/*bDirtyCachedInput=*/true);
		Refresh();
	}
	// General properties that don't affect behavior
	else if (PropName == GET_MEMBER_NAME_CHECKED(UPCGComponent, Seed))
	{
		DirtyGenerated();
		Refresh();
	}
	else if (PropName == GET_MEMBER_NAME_CHECKED(UPCGComponent, ExcludedTags))
	{
		SetupTrackingCallbacks();
		RefreshTrackingData();

		const bool bHadExclusionData = !CachedExclusionData.IsEmpty();
		const bool bHasExcludedActors = !CachedExcludedActors.IsEmpty();

		if(bHadExclusionData || bHasExcludedActors)
		{
			DirtyGenerated();
			Refresh();
		}
	}
}

void UPCGComponent::PreEditUndo()
{
	// Here we will keep a copy of flags that we require to keep through the undo
	// so we can have a consistent state
	LastGeneratedBoundsPriorToUndo = LastGeneratedBounds;

	// We don't know what is changing so remove all callbacks
	if (Graph)
	{
		Graph->OnGraphChangedDelegate.RemoveAll(this);
	}

	if (bGenerated)
	{
		// Cleanup so managed resources are cleaned in all cases
		Cleanup(/*bRemoveComponents=*/true, /*bSave=*/PCGComponent::bSaveOnCleanupAndGenerate);
		// Put back generated flag to its original value so it is captured properly
		bGenerated = true;
	}	
	
	TeardownTrackingCallbacks();
}

void UPCGComponent::PostEditUndo()
{
	LastGeneratedBounds = LastGeneratedBoundsPriorToUndo;

	if (Graph)
	{
		Graph->OnGraphChangedDelegate.AddUObject(this, &UPCGComponent::OnGraphChanged);
	}

	SetupTrackingCallbacks();
	RefreshTrackingData();
	UpdateTrackedLandscape();
	DirtyGenerated(/*bDirtyCachedInput=*/true);
	DirtyCacheForAllTrackedTags();

	if (bGenerated)
	{
		Refresh();
	}
}

void UPCGComponent::SetupActorCallbacks()
{
	GEngine->OnActorMoved().AddUObject(this, &UPCGComponent::OnActorMoved);
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UPCGComponent::OnObjectPropertyChanged);
}

void UPCGComponent::TeardownActorCallbacks()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	GEngine->OnActorMoved().RemoveAll(this);
}

void UPCGComponent::SetupTrackingCallbacks()
{
	CachedTrackedTagsToSettings.Reset();
	if (Graph)
	{
		CachedTrackedTagsToSettings = Graph->GetTrackedTagsToSettings();
	}

	if(!ExcludedTags.IsEmpty() || !CachedTrackedTagsToSettings.IsEmpty())
	{
		GEngine->OnLevelActorAdded().AddUObject(this, &UPCGComponent::OnActorAdded);
		GEngine->OnLevelActorDeleted().AddUObject(this, &UPCGComponent::OnActorDeleted);
	}
}

void UPCGComponent::RefreshTrackingData()
{
	GetActorsFromTags(ExcludedTags, CachedExcludedActors, /*bCullAgainstLocalBounds=*/true);

	TSet<FName> TrackedTags;
	CachedTrackedTagsToSettings.GetKeys(TrackedTags);
	GetActorsFromTags(TrackedTags, CachedTrackedActors, /*bCullAgainstLocalBounds=*/false);
	PopulateTrackedActorToTagsMap(/*bForce=*/true);
}

void UPCGComponent::TeardownTrackingCallbacks()
{
	GEngine->OnLevelActorAdded().RemoveAll(this);
	GEngine->OnLevelActorDeleted().RemoveAll(this);
}

bool UPCGComponent::ActorHasExcludedTag(AActor* InActor) const
{
	if (!InActor)
	{
		return false;
	}

	bool bHasExcludedTag = false;

	for (const FName& Tag : InActor->Tags)
	{
		if (ExcludedTags.Contains(Tag))
		{
			bHasExcludedTag = true;
			break;
		}
	}

	return bHasExcludedTag;
}

bool UPCGComponent::UpdateExcludedActor(AActor* InActor)
{
	// Dirty data in all cases - the tag or positional changes will be picked up in the test later
	if (CachedExcludedActors.Contains(InActor))
	{
		if (UPCGData** ExclusionData = CachedExclusionData.Find(InActor))
		{
			*ExclusionData = nullptr;
		}

		CachedPCGData = nullptr;
		return true;
	}
	// Dirty only if the impact actor is inside the bounds
	else if (ActorHasExcludedTag(InActor) && GetGridBounds().Intersect(GetGridBounds(InActor)))
	{
		CachedPCGData = nullptr;
		return true;
	}
	else
	{
		return false;
	}
}

bool UPCGComponent::ActorIsTracked(AActor* InActor) const
{
	if (!InActor || !Graph)
	{
		return false;
	}

	bool bIsTracked = false;
	for (const FName& Tag : InActor->Tags)
	{
		if (CachedTrackedTagsToSettings.Contains(Tag))
		{
			bIsTracked = true;
			break;
		}
	}

	return bIsTracked;
}

void UPCGComponent::OnActorAdded(AActor* InActor)
{
	const bool bIsExcluded = UpdateExcludedActor(InActor);
	const bool bIsTracked = AddTrackedActor(InActor);

	if (bIsExcluded || bIsTracked)
	{
		DirtyGenerated();
		Refresh();
	}
}

void UPCGComponent::OnActorDeleted(AActor* InActor)
{
	const bool bWasExcluded = UpdateExcludedActor(InActor);
	const bool bWasTracked = RemoveTrackedActor(InActor);

	if (bWasExcluded || bWasTracked)
	{
		DirtyGenerated();
		Refresh();
	}
}

void UPCGComponent::OnActorMoved(AActor* InActor)
{
	if (InActor == GetOwner() || (InActor && InActor == TrackedLandscape))
	{
		// TODO: find better metrics to dirty the inputs. 
		// TODO: this should dirty only the actor pcg data.
		{
			UpdateTrackedLandscape();
			DirtyGenerated(true);
			Refresh();
		}
	}
	else
	{
		bool bDirtyAndRefresh = false;

		if (UpdateExcludedActor(InActor))
		{
			bDirtyAndRefresh = true;
		}

		if (DirtyTrackedActor(InActor))
		{
			bDirtyAndRefresh = true;
		}

		if (bDirtyAndRefresh)
		{
			DirtyGenerated();
			Refresh();
		}
	}
}

void UPCGComponent::UpdateTrackedLandscape(bool bBoundsCheck)
{
	TeardownLandscapeTracking();
	TrackedLandscape = nullptr;

	if (InputType == EPCGComponentInput::Landscape)
	{
		if (bBoundsCheck)
		{
			UPCGData* ActorData = GetActorPCGData();
			if (const UPCGSpatialData* ActorSpatialData = Cast<const UPCGSpatialData>(ActorData))
			{
				TrackedLandscape = PCGHelpers::GetLandscape(GetOwner()->GetWorld(), ActorSpatialData->GetBounds());
			}
		}
		else
		{
			TrackedLandscape = PCGHelpers::GetAnyLandscape(GetOwner()->GetWorld());
		}
	}
	else if (InputType == EPCGComponentInput::Actor)
	{
		if (ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(GetOwner()))
		{
			TrackedLandscape = Landscape;
		}
	}

	SetupLandscapeTracking();
}

void UPCGComponent::SetupLandscapeTracking()
{
	if (TrackedLandscape.IsValid())
	{
		TrackedLandscape->OnComponentDataChanged.AddUObject(this, &UPCGComponent::OnLandscapeChanged);
	}
}

void UPCGComponent::TeardownLandscapeTracking()
{
	if (TrackedLandscape.IsValid())
	{
		TrackedLandscape->OnComponentDataChanged.RemoveAll(this);
	}
}

void UPCGComponent::OnLandscapeChanged(ALandscapeProxy* Landscape, const FLandscapeProxyComponentDataChangedParams& ChangeParams)
{
	if (Landscape == TrackedLandscape)
	{
		// Check if there is an overlap in the changed components vs. the current actor data
		bool bDirtyInput = (GetOwner() == TrackedLandscape);

		if (!bDirtyInput && InputType == EPCGComponentInput::Landscape)
		{
			UPCGData* ActorData = GetActorPCGData();
			if (const UPCGSpatialData* ActorSpatialData = Cast<const UPCGSpatialData>(ActorData))
			{
				const FBox ActorBounds = ActorSpatialData->GetBounds();

				ChangeParams.ForEachComponent([&bDirtyInput, &ActorBounds](const ULandscapeComponent* LandscapeComponent)
				{
					if(LandscapeComponent && ActorBounds.Intersect(LandscapeComponent->Bounds.GetBox()))
					{
						bDirtyInput = true;
					}
				});
			}
		}

		if (bDirtyInput)
		{
			DirtyGenerated(true);
			Refresh();
		}
	}
}

void UPCGComponent::OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InEvent)
{
	bool bValueNotInteractive = (InEvent.ChangeType != EPropertyChangeType::Interactive);
	// Special exception for actor tags, as we can't track otherwise an actor "losing" a tag
	bool bActorTagChange = (InEvent.Property && InEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AActor, Tags));

	if(!bValueNotInteractive && !bActorTagChange)
	{
		return;
	}

	// First, check if it's an actor
	AActor* Actor = Cast<AActor>(InObject);

	// Otherwise, if it's an actor component, track it as well
	if (!Actor)
	{
		if (UActorComponent* ActorComponent = Cast<UActorComponent>(InObject))
		{
			Actor = ActorComponent->GetOwner();
		}
	}

	// Finally, if it's neither an actor or an actor component, it might be a dependency of a tracked actor
	if (!Actor)
	{
		for(const TPair<TWeakObjectPtr<AActor>, TSet<TObjectPtr<UObject>>>& TrackedActor : CachedTrackedActorToDependencies)
		{
			if (TrackedActor.Value.Contains(InObject))
			{
				OnActorChanged(TrackedActor.Key.Get(), InObject, bActorTagChange);
			}
		}
	}
	else
	{
		OnActorChanged(Actor, InObject, bActorTagChange);
	}
}

void UPCGComponent::OnActorChanged(AActor* Actor, UObject* InObject, bool bActorTagChange)
{
	if (Actor == GetOwner())
	{
		// Something has changed on the owner (including properties of this component)
		// In the case of splines, this is where we'd get notified if some component properties (incl. spline vertices) have changed
		// TODO: this should dirty only the actor pcg data.
		DirtyGenerated(true);
		Refresh();
	}
	else if(Actor)
	{
		bool bDirtyAndRefresh = false;

		if (UpdateExcludedActor(Actor))
		{
			bDirtyAndRefresh = true;
		}

		if ((bActorTagChange && Actor == InObject && UpdateTrackedActor(Actor)) || DirtyTrackedActor(Actor))
		{
			bDirtyAndRefresh = true;
		}

		if (bDirtyAndRefresh)
		{
			DirtyGenerated();
			Refresh();
		}
	}
}

void UPCGComponent::OnGraphChanged(UPCGGraph* InGraph, bool bIsStructural)
{
	OnGraphChanged(InGraph, bIsStructural, true);
}

void UPCGComponent::OnGraphChanged(UPCGGraph* InGraph, bool bIsStructural, bool bShouldRefresh)
{
	if (InGraph == Graph)
	{
		// Since we've changed the graph, we might have changed the tracked actor tags as well
		TeardownTrackingCallbacks();
		SetupTrackingCallbacks();
		RefreshTrackingData();
		DirtyCacheForAllTrackedTags();

		DirtyGenerated();
		if (bShouldRefresh)
		{
			Refresh();
		}
	}
}

void UPCGComponent::DirtyGenerated(bool bInDirtyCachedInput)
{
	bDirtyGenerated = true;

	if (bInDirtyCachedInput)
	{
		CachedInputData = nullptr;
		CachedActorData = nullptr;
		CachedPCGData = nullptr;
		CachedExclusionData.Reset();
	}

	// For partitioned graph, we must forward the call to the partition actor
	// Note that we do not need to forward "normal" dirty as these will be picked up by the local PCG components
	// However, input changes / moves of the partitioned object will not be caught
	// It would be possible for partitioned actors to add callbacks to their original component, but that inverses the processing flow
	if (bInDirtyCachedInput && bActivated && IsPartitioned())
	{
		if (GetSubsystem())
		{
			GetSubsystem()->DirtyGraph(this, LastGeneratedBounds, bInDirtyCachedInput);
		}
	}
}

void UPCGComponent::Refresh()
{
	// Following a change in some properties or in some spatial information related to this component,
	// We need to regenerate the graph, depending of the state in the editor.
	// In the case of a non-partitioned graph, we need to generate the graph only if it was previously generated & tagged for regeneration
	// In the partitioned graph case, however, we need to do a bit more:
	// 1. Regenerate the graph if it was previously generated & tagged for regeneration;
	//  notice that the associated partition actors will not (and should not) have the regenerate flag on.
	// 2. Otherwise, we need to update the partitioning if the spatial data has changed.
	if (!bActivated)
	{
		if (IsPartitioned() && GetSubsystem())
		{
			GetSubsystem()->DelayUnpartitionGraph(this);
		}
		else
		{
			bool bWasGenerated = bGenerated;
			Cleanup(/*bRemoveComponents=*/false);
			bGenerated = bWasGenerated;
		}
	}
	else
	{
		if (bGenerated && bRegenerateInEditor)
		{
			Generate(/*bForce=*/false);
		}
		else if (IsPartitioned() && GetSubsystem())
		{
			GetSubsystem()->DelayPartitionGraph(this);
		}
	}
}
#endif

UPCGData* UPCGComponent::GetPCGData()
{
	if (!CachedPCGData)
	{
		CachedPCGData = CreatePCGData();
	}

	return CachedPCGData;
}

UPCGData* UPCGComponent::GetInputPCGData()
{
	if (!CachedInputData)
	{
		CachedInputData = CreateInputPCGData();
	}

	return CachedInputData;
}

UPCGData* UPCGComponent::GetActorPCGData()
{
	if (!CachedActorData)
	{
		CachedActorData = CreateActorPCGData();
	}

	return CachedActorData;
}

UPCGData* UPCGComponent::GetOriginalActorPCGData()
{
	if (APCGPartitionActor* PartitionActor = Cast<APCGPartitionActor>(GetOwner()))
	{
		if (UPCGComponent* OriginalComponent = PartitionActor->GetOriginalComponent(this))
		{
			return OriginalComponent->GetActorPCGData();
		}
	}

	return nullptr;
}

TArray<UPCGData*> UPCGComponent::GetPCGExclusionData()
{
	// TODO: replace with a boolean, unify.
	UpdatePCGExclusionData();

	TArray<UPCGData*> ExclusionData;
	CachedExclusionData.GenerateValueArray(ExclusionData);
	return ExclusionData;
}

void UPCGComponent::UpdatePCGExclusionData()
{
	const UPCGData* InputData = GetInputPCGData();
	const UPCGSpatialData* InputSpatialData = Cast<const UPCGSpatialData>(InputData);

	// Update the list of cached excluded actors here, since we might not have picked up everything on map load (due to WP)
	GetActorsFromTags(ExcludedTags, CachedExcludedActors, /*bCullAgainstLocalBounds=*/true);

	// Build exclusion data based on the CachedExcludedActors
	TMap<AActor*, UPCGData*> ExclusionData;

	for(TWeakObjectPtr<AActor> ExcludedActorWeakPtr : CachedExcludedActors)
	{
		if(!ExcludedActorWeakPtr.IsValid())
		{
			continue;
		}

		AActor* ExcludedActor = ExcludedActorWeakPtr.Get();

		UPCGData** PreviousExclusionData = CachedExclusionData.Find(ExcludedActor);

		if (PreviousExclusionData && *PreviousExclusionData)
		{
			ExclusionData.Add(ExcludedActor, *PreviousExclusionData);
		}
		else
		{
			// Create the new exclusion data
			UPCGData* ActorData = CreateActorPCGData(ExcludedActor);
			UPCGSpatialData* ActorSpatialData = Cast<UPCGSpatialData>(ActorData);

			if (InputSpatialData && ActorSpatialData)
			{
				// Change the target actor to this - otherwise we could push changes on another actor
				ActorSpatialData->TargetActor = GetOwner();

				// Create intersection or projection depending on the dimension
				// TODO: there's an ambiguity here when it's the same dimension.
				// For volumes, we'd expect an intersection, for surfaces we'd expect a projection
				if (ActorSpatialData->GetDimension() > InputSpatialData->GetDimension())
				{
					ExclusionData.Add(ExcludedActor, ActorSpatialData->IntersectWith(InputSpatialData));
				}
				else
				{
					ExclusionData.Add(ExcludedActor, ActorSpatialData->ProjectOn(InputSpatialData));
				}
			}
		}
	}

	CachedExclusionData = ExclusionData;
}

UPCGData* UPCGComponent::CreateActorPCGData()
{
	return CreateActorPCGData(GetOwner());
}

UPCGData* UPCGComponent::CreateActorPCGData(AActor* Actor)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGComponent::CreateActorPCGData)
	// In this case, we'll build the data type that's closest to known actor types
	// TODO: add factory for extensibility
	if (APCGPartitionActor* PartitionActor = Cast<APCGPartitionActor>(Actor))
	{
		check(GetOwner() == Actor); // Invalid processing otherwise because of the this usage
		if (UPCGComponent* OriginalComponent = PartitionActor->GetOriginalComponent(this))
		{
			check(OriginalComponent->IsPartitioned());
			// TODO: cache/share the original component's actor pcg data
			if (const UPCGSpatialData* OriginalComponentSpatialData = Cast<const UPCGSpatialData>(OriginalComponent->GetActorPCGData()))
			{
				UPCGVolumeData* Data = NewObject<UPCGVolumeData>(this);
				Data->Initialize(PartitionActor->GetFixedBounds(), PartitionActor);

				return Data->IntersectWith(OriginalComponentSpatialData);
			}
		}

		// TODO: review this once we support non-spatial data?
		return nullptr;
	}
	else if (ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(Actor))
	{
		UPCGLandscapeData* Data = NewObject<UPCGLandscapeData>(this);
		Data->Initialize(Landscape, GetGridBounds(Actor));

		return Data;
	}
	else if (AVolume* Volume = Cast<AVolume>(Actor))
	{
		UPCGVolumeData* Data = NewObject<UPCGVolumeData>(this);
		Data->Initialize(Volume);

		return Data;
	}
	else // Prepare data on a component basis
	{
		TInlineComponentArray<ULandscapeSplinesComponent*, 1> LandscapeSplines;
		Actor->GetComponents<ULandscapeSplinesComponent>(LandscapeSplines);

		TInlineComponentArray<USplineComponent*, 1> Splines;
		Actor->GetComponents<USplineComponent>(Splines);

		TInlineComponentArray<UShapeComponent*, 1> Shapes;
		Actor->GetComponents<UShapeComponent>(Shapes);

		// Don't get generic primitives unless it's the only thing we can find.
		TInlineComponentArray<UPrimitiveComponent*, 1> OtherPrimitives;
		if (LandscapeSplines.Num() == 0 && Splines.Num() == 0 && Shapes.Num() == 0)
		{
			Actor->GetComponents<UPrimitiveComponent>(OtherPrimitives);
		}

		UPCGUnionData* Union = nullptr;
		if (LandscapeSplines.Num() + Splines.Num() + Shapes.Num() + OtherPrimitives.Num() > 1)
		{
			Union = NewObject<UPCGUnionData>(this);
		}

		for (ULandscapeSplinesComponent* SplineComponent : LandscapeSplines)
		{
			UPCGLandscapeSplineData* SplineData = NewObject<UPCGLandscapeSplineData>(this);
			SplineData->Initialize(SplineComponent);

			if (Union)
			{
				Union->AddData(SplineData);
			}
			else
			{
				return SplineData;
			}
		}

		for (USplineComponent* SplineComponent : Splines)
		{
			UPCGSplineData* SplineData = NewObject<UPCGSplineData>(this);
			SplineData->Initialize(SplineComponent);

			if (Union)
			{
				Union->AddData(SplineData);
			}
			else
			{
				return SplineData;
			}
		}

		for (UShapeComponent* ShapeComponent : Shapes)
		{
			UPCGPrimitiveData* ShapeData = NewObject<UPCGPrimitiveData>(this);
			ShapeData->Initialize(ShapeComponent);
			
			if (Union)
			{
				Union->AddData(ShapeData);
			}
			else
			{
				return ShapeData;
			}
		}

		for (UPrimitiveComponent* PrimitiveComponent : OtherPrimitives)
		{
			UPCGPrimitiveData* PrimitiveData = NewObject<UPCGPrimitiveData>(this);
			PrimitiveData->Initialize(PrimitiveComponent);

			if (Union)
			{
				Union->AddData(PrimitiveData);
			}
			else
			{
				return PrimitiveData;
			}
		}

		if (Union)
		{
			return Union;
		}
		else // No parsed components: default
		{
			//Default behavior on unknown actors is to write a single point at the actor location
			UPCGPointData* Data = NewObject<UPCGPointData>(this);
			Data->InitializeFromActor(Actor);
			return Data;
		}
	}
}

UPCGData* UPCGComponent::CreatePCGData()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGComponent::CreatePCGData);
	UPCGData* InputData = GetInputPCGData();
	UPCGSpatialData* SpatialInput = Cast<UPCGSpatialData>(InputData);
	
	// Early out: incompatible data
	if (!SpatialInput)
	{
		return InputData;
	}

	UPCGDifferenceData* Difference = nullptr;
	TArray<UPCGData*> ExclusionData = GetPCGExclusionData();

	for (UPCGData* Exclusion : ExclusionData)
	{
		if (UPCGSpatialData* SpatialExclusion = Cast<UPCGSpatialData>(Exclusion))
		{
			if (!Difference)
			{
				Difference = SpatialInput->Subtract(SpatialExclusion);
			}
			else
			{
				Difference->AddDifference(SpatialExclusion);
			}
		}
	}

	return Difference ? Difference : InputData;
}

UPCGData* UPCGComponent::CreateInputPCGData()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGComponent::CreateInputPCGData);
	AActor* Actor = GetOwner();
	check(Actor);

	// Construct proper input based on input type
	if (InputType == EPCGComponentInput::Actor)
	{
		return GetActorPCGData();
	}
	else if (InputType == EPCGComponentInput::Landscape)
	{
		UPCGData* ActorData = GetActorPCGData();

		if (ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(GetOwner()))
		{
			// Current actor is a landscape, we don't need to do an additional projection
			return ActorData;
		}

		const UPCGSpatialData* ActorSpatialData = Cast<const UPCGSpatialData>(ActorData);

		if (!ActorSpatialData)
		{
			// TODO ? support non-spatial data on landscape?
			return nullptr;
		}

		const FBox ActorDataBounds = ActorSpatialData->GetBounds();
		ALandscapeProxy* Landscape = PCGHelpers::GetLandscape(Actor->GetWorld(), ActorDataBounds);

		if (!Landscape)
		{
			// No landscape found
			return nullptr;
		}

		const FBox LandscapeBounds = GetGridBounds(Landscape);
		check(LandscapeBounds.IsValid);

		const FBox OverlappedBounds = LandscapeBounds.Overlap(ActorDataBounds);

		if (!OverlappedBounds.IsValid)
		{
			return nullptr;
		}

		UPCGLandscapeData* LandscapeData = NewObject<UPCGLandscapeData>(this);
		// Since we're not sharing this landscape data with any other users, we can limit the bounds earlier
		LandscapeData->Initialize(Landscape, OverlappedBounds);

		// Need to override target actor for this one, not the landscape
		LandscapeData->TargetActor = Actor;

		// Decide whether to intersect or project
		// Currently, it makes sense to intersect only for volumes;
		// Note that we don't currently check for a volume object but only on dimension
		// so intersections (such as volume X partition actor) get picked up properly
		if (ActorSpatialData->GetDimension() >= 3)
		{
			return LandscapeData->IntersectWith(ActorSpatialData);
		}
		else
		{
			return ActorSpatialData->ProjectOn(LandscapeData);
		}
	}
	else
	{
		// In this case, the input data will be provided in some other form,
		// Most likely to be stored in the PCG data grid.
		return nullptr;
	}
}

FBox UPCGComponent::GetGridBounds() const
{
	return GetGridBounds(GetOwner());
}

FBox UPCGComponent::GetGridBounds(AActor* Actor) const
{
	check(Actor);

	FBox Bounds(EForceInit::ForceInit);

	if (APCGPartitionActor* PartitionActor = Cast<APCGPartitionActor>(Actor))
	{
		// First, get the bounds from the partition actor
		Bounds = PartitionActor->GetFixedBounds();

		// Then intersect with the original component's bounds.
		if (const UPCGComponent* OriginalComponent = PartitionActor->GetOriginalComponent(this))
		{
			Bounds = Bounds.Overlap(OriginalComponent->GetGridBounds());
		}
	}
	// TODO: this might not work in non-editor builds
#if WITH_EDITOR
	else if (ALandscapeProxy* LandscapeActor = Cast<ALandscape>(Actor))
	{
		Bounds = PCGHelpers::GetLandscapeBounds(LandscapeActor);
	}
#endif
	else
	{
		Bounds = PCGHelpers::GetActorBounds(Actor);
	}

	return Bounds;
}

UPCGSubsystem* UPCGComponent::GetSubsystem() const
{
	return (GetOwner() && GetOwner()->GetWorld()) ? GetOwner()->GetWorld()->GetSubsystem<UPCGSubsystem>() : nullptr;
}

#if WITH_EDITOR
bool UPCGComponent::PopulateTrackedActorToTagsMap(bool bForce)
{
	if (bActorToTagsMapPopulated && !bForce)
	{
		return false;
	}

	CachedTrackedActorToTags.Reset();
	CachedTrackedActorToDependencies.Reset();
	for (TWeakObjectPtr<AActor> Actor : CachedTrackedActors)
	{
		if (Actor.IsValid())
		{
			AddTrackedActor(Actor.Get(), /*bForce=*/true);
		}
	}

	bActorToTagsMapPopulated = true;
	return true;
}

bool UPCGComponent::AddTrackedActor(AActor* InActor, bool bForce)
{
	if (!bForce)
	{
		PopulateTrackedActorToTagsMap();
	}	

	check(InActor);
	bool bAppliedChange = false;

	for (const FName& Tag : InActor->Tags)
	{
		if (!CachedTrackedTagsToSettings.Contains(Tag))
		{
			continue;
		}

		bAppliedChange = true;
		CachedTrackedActorToTags.FindOrAdd(InActor).Add(Tag);
		PCGHelpers::GatherDependencies(InActor, CachedTrackedActorToDependencies.FindOrAdd(InActor));

		if (!bForce)
		{
			DirtyCacheFromTag(Tag);
		}
	}

	return bAppliedChange;
}

bool UPCGComponent::RemoveTrackedActor(AActor* InActor)
{
	PopulateTrackedActorToTagsMap();

	check(InActor);
	bool bAppliedChange = false;

	if(CachedTrackedActorToTags.Contains(InActor))
	{
		for (const FName& Tag : CachedTrackedActorToTags[InActor])
		{
			DirtyCacheFromTag(Tag);
		}

		CachedTrackedActorToTags.Remove(InActor);
		CachedTrackedActorToDependencies.Remove(InActor);
		bAppliedChange = true;
	}

	return bAppliedChange;
}

bool UPCGComponent::UpdateTrackedActor(AActor* InActor)
{
	check(InActor);
	// If the tracked data wasn't initialized before, then it is not possible to know if we need to update or not - take no chances
	bool bAppliedChange = PopulateTrackedActorToTagsMap();

	// Update the contents of the tracked actor vs. its current tags, and dirty accordingly
	if (CachedTrackedActorToTags.Contains(InActor))
	{
		// Any tags that aren't on the actor and were in the cached actor to tags -> remove & dirty
		TSet<FName> CachedTags = CachedTrackedActorToTags[InActor];
		for (const FName& CachedTag : CachedTags)
		{
			if (!InActor->Tags.Contains(CachedTag))
			{
				CachedTrackedActorToTags[InActor].Remove(CachedTag);
				DirtyCacheFromTag(CachedTag);
				bAppliedChange = true;
			}
		}
	}
		
	// Any tags that are new on the actor and not in the cached actor to tags -> add & dirty
	for (const FName& Tag : InActor->Tags)
	{
		if (!CachedTrackedTagsToSettings.Contains(Tag))
		{
			continue;
		}

		if (!CachedTrackedActorToTags.FindOrAdd(InActor).Find(Tag))
		{
			CachedTrackedActorToTags[InActor].Add(Tag);
			PCGHelpers::GatherDependencies(InActor, CachedTrackedActorToDependencies.FindOrAdd(InActor));
			DirtyCacheFromTag(Tag);
			bAppliedChange = true;
		}
	}

	// Finally, if the current has no tag anymore, we can remove it from the map
	if (CachedTrackedActorToTags.Contains(InActor) && CachedTrackedActorToTags[InActor].IsEmpty())
	{
		CachedTrackedActorToTags.Remove(InActor);
		CachedTrackedActorToDependencies.Remove(InActor);
	}

	return bAppliedChange;
}

bool UPCGComponent::DirtyTrackedActor(AActor* InActor)
{
	PopulateTrackedActorToTagsMap();

	check(InActor);
	bool bAppliedChange = false;

	if (CachedTrackedActorToTags.Contains(InActor))
	{
		for (const FName& Tag : CachedTrackedActorToTags[InActor])
		{
			DirtyCacheFromTag(Tag);
		}

		bAppliedChange = true;
	}
	else if (AddTrackedActor(InActor))
	{
		bAppliedChange = true;
	}

	return bAppliedChange;
}

void UPCGComponent::DirtyCacheFromTag(const FName& InTag)
{
	if (CachedTrackedTagsToSettings.Contains(InTag))
	{
		for (TWeakObjectPtr<const UPCGSettings> Settings : CachedTrackedTagsToSettings[InTag])
		{
			if (Settings.IsValid() && GetSubsystem())
			{
				GetSubsystem()->CleanFromCache(Settings->GetElement().Get());
			}
		}
	}
}

void UPCGComponent::DirtyCacheForAllTrackedTags()
{
	for (const auto& TagToSettings : CachedTrackedTagsToSettings)
	{
		for (TWeakObjectPtr<const UPCGSettings> Settings : TagToSettings.Value)
		{
			if (Settings.IsValid() && GetSubsystem())
			{
				GetSubsystem()->CleanFromCache(Settings->GetElement().Get());
			}
		}
	}
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE