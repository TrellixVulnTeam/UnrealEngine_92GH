// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Containers/Set.h"
#include "Misc/Guid.h"

#if WITH_EDITOR
// Struct used to create actor descriptor
struct FWorldPartitionActorDescInitData
{
	UClass* NativeClass;
	FName PackageName;
	FName ActorPath;
	TArray<uint8> SerializedData;
};

class UActorDescContainer;
struct FActorContainerID;

enum class EContainerClusterMode : uint8
{
	Partitioned, // Per Actor Partitioning
};

template <typename T, class F>
inline bool CompareUnsortedArrays(const TArray<T>& Array1, const TArray<T>& Array2, F Func)
{
	if (Array1.Num() == Array2.Num())
	{
		TArray<T> SortedArray1(Array1);
		TArray<T> SortedArray2(Array2);
		SortedArray1.Sort(Func);
		SortedArray2.Sort(Func);
		return SortedArray1 == SortedArray2;
	}
	return false;
}

template <typename T>
inline bool CompareUnsortedArrays(const TArray<T>& Array1, const TArray<T>& Array2)
{
	return CompareUnsortedArrays(Array1, Array2, [](const T& A, const T& B) { return A < B; });
}

template <>
inline bool CompareUnsortedArrays(const TArray<FName>& Array1, const TArray<FName>& Array2)
{
	return CompareUnsortedArrays(Array1, Array2, [](const FName& A, const FName& B) { return A.LexicalLess(B); });
}
#endif

/**
 * Represents a potentially unloaded actor (editor-only)
 */
class ENGINE_API FWorldPartitionActorDesc 
{
#if WITH_EDITOR
	friend class AActor;
	friend class UActorDescContainer;
	friend class UWorldPartition;
	friend struct FWorldPartitionHandleImpl;
	friend struct FWorldPartitionReferenceImpl;

public:
	virtual ~FWorldPartitionActorDesc() {}

	inline const FGuid& GetGuid() const { return Guid; }
	
	UE_DEPRECATED(5.1, "GetClass is deprecated, GetBaseClass or GetNativeClass should be used instead.")
	inline FName GetClass() const { return GetNativeClass(); }

	inline FName GetBaseClass() const { return BaseClass; }
	inline FName GetNativeClass() const { return NativeClass; }
	inline UClass* GetActorNativeClass() const { return ActorNativeClass; }
	inline FVector GetOrigin() const { return GetBounds().GetCenter(); }
	inline FName GetRuntimeGrid() const { return RuntimeGrid; }
	inline bool GetIsSpatiallyLoaded() const { return bIsForcedNonSpatiallyLoaded ? false : bIsSpatiallyLoaded; }
	inline bool GetIsSpatiallyLoadedRaw() const { return bIsSpatiallyLoaded; }
	inline bool GetActorIsEditorOnly() const { return bActorIsEditorOnly; }
	inline bool GetLevelBoundsRelevant() const { return bLevelBoundsRelevant; }
	inline bool GetActorIsHLODRelevant() const { return bActorIsHLODRelevant; }
	inline FName GetHLODLayer() const { return HLODLayer; }
	inline const TArray<FName>& GetDataLayers() const { return DataLayers; }
	inline const TArray<FName>& GetDataLayerInstanceNames() const { return DataLayerInstanceNames; }
	inline const TArray<FName>& GetTags() const { return Tags; }
	inline void SetDataLayerInstanceNames(const TArray<FName>& InDataLayerInstanceNames) { DataLayerInstanceNames = InDataLayerInstanceNames; }
	inline FName GetActorPackage() const { return ActorPackage; }
	inline FName GetActorPath() const { return ActorPath; }
	inline FName GetActorLabel() const { return ActorLabel; }
	inline FName GetFolderPath() const { return FolderPath; }
	inline const FGuid& GetFolderGuid() const { return FolderGuid; }
	FBox GetBounds() const;
	inline const FGuid& GetParentActor() const { return ParentActor; }
	inline bool IsUsingDataLayerAsset() const { return bIsUsingDataLayerAsset; }
	inline FName GetProperty(FName PropertyName) const { return Properties.FindRef(PropertyName); }
	inline bool HasProperty(FName PropertyName) const { return Properties.Contains(PropertyName); }

	FName GetActorName() const;
	FName GetActorLabelOrName() const;
	FName GetDisplayClassName() const;

	virtual bool IsContainerInstance() const { return false; }
	virtual bool GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const { return false; }

	virtual const FGuid& GetSceneOutlinerParent() const { return GetParentActor(); }
	virtual bool IsResaveNeeded() const { return false; }
	virtual bool IsRuntimeRelevant(const FActorContainerID& InContainerID) const { return true; }

	bool operator==(const FWorldPartitionActorDesc& Other) const
	{
		return Guid == Other.Guid;
	}

	friend uint32 GetTypeHash(const FWorldPartitionActorDesc& Key)
	{
		return GetTypeHash(Key.Guid);
	}

protected:
	inline uint32 IncSoftRefCount() const
	{
		return ++SoftRefCount;
	}

	inline uint32 DecSoftRefCount() const
	{
		check(SoftRefCount > 0);
		return --SoftRefCount;
	}

	inline uint32 IncHardRefCount() const
	{
		return ++HardRefCount;
	}

	inline uint32 DecHardRefCount() const
	{
		check(HardRefCount > 0);
		return --HardRefCount;
	}

	virtual void SetContainer(UActorDescContainer* InContainer)
	{
		check(!Container || !InContainer);
		Container = InContainer;
	}

public:
	inline uint32 GetSoftRefCount() const
	{
		return SoftRefCount;
	}

	inline uint32 GetHardRefCount() const
	{
		return HardRefCount;
	}

	const TArray<FGuid>& GetReferences() const
	{
		return References;
	}

	UActorDescContainer* GetContainer() const
	{
		return Container;
	}

	FString ToString() const;

	bool IsLoaded(bool bEvenIfPendingKill=false) const;
	AActor* GetActor(bool bEvenIfPendingKill=true, bool bEvenIfUnreachable=false) const;
	AActor* Load() const;
	virtual void Unload();

	UE_DEPRECATED(5.1, "ShouldBeLoadedByEditorCells is deprecated, ShouldBeLoadedByEditor should be used instead.")
	bool ShouldBeLoadedByEditorCells() const { return ShouldBeLoadedByEditor(); }

	virtual bool ShouldBeLoadedByEditor() const { return true; }

	virtual void Init(const AActor* InActor);
	virtual void Init(const FWorldPartitionActorDescInitData& DescData);

	virtual bool Equals(const FWorldPartitionActorDesc* Other) const;

	void SerializeTo(TArray<uint8>& OutData);

	void TransformInstance(const FString& From, const FString& To, const FTransform& InstanceTransform);

protected:
	FWorldPartitionActorDesc();

	virtual void TransferFrom(const FWorldPartitionActorDesc* From)
	{
		Container = From->Container;
		SoftRefCount = From->SoftRefCount;
		HardRefCount = From->HardRefCount;
		bIsForcedNonSpatiallyLoaded = From->bIsForcedNonSpatiallyLoaded;
	}

	virtual void TransferWorldData(const FWorldPartitionActorDesc* From)
	{
		BoundsLocation = From->BoundsLocation;
		BoundsExtent = From->BoundsExtent;
	}

	virtual void Serialize(FArchive& Ar);

	// Persistent
	FGuid							Guid;
	FName							BaseClass;
	FName							NativeClass;
	FName							ActorPackage;
	FName							ActorPath;
	FName							ActorLabel;
	FVector							BoundsLocation;
	FVector							BoundsExtent;
	FName							RuntimeGrid;
	bool							bIsSpatiallyLoaded;
	bool							bActorIsEditorOnly;
	bool							bLevelBoundsRelevant;
	bool							bActorIsHLODRelevant;
	bool							bIsUsingDataLayerAsset; // Used to know if DataLayers array represents DataLayers Asset paths or the FNames of the deprecated version of Data Layers
	FName							HLODLayer;
	TArray<FName>					DataLayers;
	TArray<FGuid>					References;
	TArray<FName>					Tags;
	TMap<FName, FName>				Properties;
	FName							FolderPath;
	FGuid							FolderGuid;

	FGuid							ParentActor; // Used to validate settings against parent (to warn on layer/placement compatibility issues)
	
	// Transient
	mutable uint32					SoftRefCount;
	mutable uint32					HardRefCount;
	UClass*							ActorNativeClass;
	mutable TWeakObjectPtr<AActor>	ActorPtr;
	UActorDescContainer*			Container;
	TArray<FName>					DataLayerInstanceNames;
	bool							bIsForcedNonSpatiallyLoaded;
#endif
};
