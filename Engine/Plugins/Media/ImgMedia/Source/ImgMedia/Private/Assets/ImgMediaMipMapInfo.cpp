// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaMipMapInfo.h"

#include "IImgMediaModule.h"
#include "ImgMediaPrivate.h"
#include "ImgMediaSceneViewExtension.h"

#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Set.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Math/UnrealMathUtility.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

DECLARE_CYCLE_STAT(TEXT("ImgMedia MipMap Update Cache"), STAT_ImgMedia_MipMapUpdateCache, STATGROUP_Media);

static TAutoConsoleVariable<bool> CVarImgMediaMipMapDebugEnable(
	TEXT("ImgMedia.MipMapDebug"),
	0,
	TEXT("Display debug on mipmaps used by the ImgMedia plugin.\n")
	TEXT("   0: off (default)\n")
	TEXT("   1: on\n"),
	ECVF_Default);

FImgMediaTileSelection::FImgMediaTileSelection(int32 NumTilesX, int32 NumTilesY, bool bDefaultVisibility)
	: Tiles(bDefaultVisibility, NumTilesX * NumTilesY)
	, Dimensions(NumTilesX, NumTilesY)
	, CachedVisibleRegion()
	, bCachedVisibleRegionDirty(true)
{
}

FImgMediaTileSelection FImgMediaTileSelection::CreateForTargetMipLevel(int32 BaseNumTilesX, int32 BaseNumTilesY, int32 TargetMipLevel, bool bDefaultVisibility)
{
	ensure(TargetMipLevel >= 0);

	const int MipLevelDiv = 1 << TargetMipLevel;
	int32 NumTilesX = FMath::Max(1, FMath::CeilToInt(float(BaseNumTilesX) / MipLevelDiv));
	int32 NumTilesY = FMath::Max(1, FMath::CeilToInt(float(BaseNumTilesY) / MipLevelDiv));

	return FImgMediaTileSelection(NumTilesX, NumTilesY, bDefaultVisibility);
}

bool FImgMediaTileSelection::IsAnyVisible() const
{
	return Tiles.Contains(true);
};

bool FImgMediaTileSelection::IsVisible(int32 TileCoordX, int32 TileCoordY) const
{
	return Tiles[ToIndex(TileCoordX, TileCoordY, Dimensions)];
}

bool FImgMediaTileSelection::Contains(const FImgMediaTileSelection& Other) const
{
	//Modified version of TBitArray's CompareSetBits() method.

	TBitArray<>::FConstWordIterator ThisIterator(Tiles);
	TBitArray<>::FConstWordIterator OtherIterator(Other.Tiles);

	ThisIterator.FillMissingBits(0u);
	OtherIterator.FillMissingBits(0u);

	while (ThisIterator || OtherIterator)
	{
		const uint32 A = ThisIterator ? ThisIterator.GetWord() : 0u;
		const uint32 B = OtherIterator ? OtherIterator.GetWord() : 0u;
		if (A != B)
		{
			// Check if A contains all of the ones in B.
			if ((A & B) != B)
			{
				return false;
			}
		}

		++ThisIterator;
		++OtherIterator;
	}

	return true;
}

void FImgMediaTileSelection::SetVisible(int32 TileCoordX, int32 TileCoordY)
{
	Tiles[ToIndex(TileCoordX, TileCoordY, Dimensions)] = true;
	bCachedVisibleRegionDirty = true;
}

TArray<FIntPoint> FImgMediaTileSelection::GetVisibleCoordinates() const
{
	TArray<FIntPoint> OutCoordinates;

	for (int32 CoordY = 0; CoordY < Dimensions.Y; ++CoordY)
	{
		for (int32 CoordX = 0; CoordX < Dimensions.X; ++CoordX)
		{
			if (Tiles[ToIndex(CoordX, CoordY, Dimensions)])
			{
				OutCoordinates.Emplace(CoordX, CoordY);
			}
		}
	}

	return OutCoordinates;
}

TArray<FIntRect> FImgMediaTileSelection::GetVisibleRegions(const FImgMediaTileSelection* CurrentTileSelection) const
{
	/**
	 * This is a two-pass algorithm to batch tiles into contiguous regions, with a bias for row groupings.
	 * First, we iterate through visible tiles, and create regions for (horizontally) contiguous tiles in each row.
	 * Second, we create the final regions out of (vertically) contiguous row regions of matching width & position.
	*/

	TArray<TArray<FIntRect>> RowsOfRegions;

	for (int32 CoordY = 0; CoordY < Dimensions.Y; ++CoordY)
	{
		TArray<FIntRect> RegionsPerRow;
		TBitArray<> PreviousVisibleTiles(0, Dimensions.X);
		for (int32 CoordX = 0; CoordX < Dimensions.X; ++CoordX)
		{
			int32 TileIndex = ToIndex(CoordX, CoordY, Dimensions);

			bool bOnlyIncludeMissingTiles = CurrentTileSelection != nullptr;

			// Interpretation: If cached selection doesn't have a tile and CurrentTileSelection (latest) does, then we need to count it as a missing tile.
			bool bIsThisTileMissing = bOnlyIncludeMissingTiles ? (!Tiles[TileIndex] && CurrentTileSelection->Tiles[TileIndex]) : (Tiles[TileIndex]);
			if (bIsThisTileMissing)
			{
				FIntPoint TileCoord(CoordX, CoordY);
				PreviousVisibleTiles[CoordX] = true;
				bool bIsPreviousRowTileVisible = (CoordX > 0) ? PreviousVisibleTiles[CoordX - 1] : false;

				if (bIsPreviousRowTileVisible)
				{
					RegionsPerRow.Last().Include(TileCoord + 1);
				}
				else
				{
					RegionsPerRow.Emplace(TileCoord, TileCoord + 1);
				}
			}
		}

		if (RegionsPerRow.Num() > 0)
		{
			RowsOfRegions.Add(MoveTemp(RegionsPerRow));
		}
	}

	TArray<FIntRect> FinalRegions;

	for (const TArray<FIntRect>& RegionsPerRow : RowsOfRegions)
	{
		for (const FIntRect& Region : RegionsPerRow)
		{
			FIntRect* ContiguousRegion = FinalRegions.FindByPredicate([&Region](const FIntRect& BatchedRegion)
				{
					// Batch row regions if their width matches and if they are vertically contiguous.
					return (Region.Min.X == BatchedRegion.Min.X) && (Region.Max.X == BatchedRegion.Max.X) && (Region.Min.Y == BatchedRegion.Max.Y);
				});

			if (ContiguousRegion != nullptr)
			{
				ContiguousRegion->Max.Y++;
			}
			else
			{
				FinalRegions.Add(Region);
			}
		}
	}

	return FinalRegions;
}

FIntRect FImgMediaTileSelection::GetVisibleRegion() const
{
	// We offload the region calculation to the loader workers, instead of constantly updating it during SetVisible().
	// Not thread safe, but only accessed sequentially in individual worker thread copies.

	if (bCachedVisibleRegionDirty)
	{
		FIntPoint Min = TNumericLimits<int32>::Max();
		FIntPoint Max = TNumericLimits<int32>::Min();

		for (int32 CoordY = 0; CoordY < Dimensions.Y; ++CoordY)
		{
			for (int32 CoordX = 0; CoordX < Dimensions.X; ++CoordX)
			{
				if (Tiles[ToIndex(CoordX, CoordY, Dimensions)])
				{
					Min.X = FMath::Min(Min.X, CoordX);
					Min.Y = FMath::Min(Min.Y, CoordY);
					Max.X = FMath::Max(Max.X, CoordX);
					Max.Y = FMath::Max(Max.Y, CoordY);
				}
			}
		}

		if (Max.X >= Min.X && Max.Y >= Min.Y)
		{
			CachedVisibleRegion = FIntRect(Min, Max + 1);
		}
		else
		{
			CachedVisibleRegion = FIntRect();
		}

		bCachedVisibleRegionDirty = false;
	}

	return CachedVisibleRegion;
}

int32 FImgMediaTileSelection::NumVisibleTiles() const
{
	int32 NumVisibleTiles = 0;

	for (int32 CoordY = 0; CoordY < Dimensions.Y; ++CoordY)
	{
		for (int32 CoordX = 0; CoordX < Dimensions.X; ++CoordX)
		{
			if (Tiles[ToIndex(CoordX, CoordY, Dimensions)])
			{
				++NumVisibleTiles;
			}
		}
	}

	return NumVisibleTiles;
}

namespace {
	bool IsPrimitiveComponentHidden(FPrimitiveComponentId ComponentId, const FImgMediaViewInfo& ViewInfo)
	{
		bool bIsPrimitiveContained = ViewInfo.PrimitiveComponentIds.Contains(ComponentId);

		// The primitive component id is either part of the hidden list, or not in the show-only list.
		return ViewInfo.bPrimitiveHiddenMode ? bIsPrimitiveContained : !bIsPrimitiveContained;
	}
}


FImgMediaMipMapObjectInfo::FImgMediaMipMapObjectInfo(UMeshComponent* InMeshComponent, float InLODBias)
	: MeshComponent(InMeshComponent)
	, LODBias(InLODBias)
{

}

UMeshComponent* FImgMediaMipMapObjectInfo::GetMeshComponent() const
{
	return MeshComponent.Get(true);
}

void FImgMediaMipMapObjectInfo::CalculateVisibleTiles(const TArray<FImgMediaViewInfo>& InViewInfos, const FSequenceInfo& InSequenceInfo, TMap<int32, FImgMediaTileSelection>& VisibleTiles) const
{
	// We simply add fully visible regions for all mip levels
	for (int32 MipLevel = 0; MipLevel < InSequenceInfo.NumMipLevels; ++MipLevel)
	{
		VisibleTiles.Add(MipLevel, FImgMediaTileSelection::CreateForTargetMipLevel(InSequenceInfo.NumTiles.X, InSequenceInfo.NumTiles.Y, MipLevel, true));
	}
}

namespace {
	// Minimalized version of FSceneView::ProjectWorldToScreen
	FORCEINLINE bool ProjectWorldToScreenFast(const FVector& WorldPosition, const FIntRect& ViewRect, const FMatrix& ViewProjectionMatrix, FVector2D& out_ScreenPos)
	{
		FPlane Result = ViewProjectionMatrix.TransformFVector4(FVector4(WorldPosition, 1.f));
		if (Result.W > 0.0f)
		{
			float NormalizedX = (Result.X / (Result.W * 2.f)) + 0.5f;
			float NormalizedY = 1.f - (Result.Y / (Result.W * 2.f)) - 0.5f;
			out_ScreenPos = FVector2D(NormalizedX * (float)ViewRect.Width(), NormalizedY * (float)ViewRect.Height());

			return true;
		}

		return false;
	}

	// Approximates hardware mip level selection.
	bool CalculateMipLevel(const FImgMediaViewInfo& ViewInfo, const FVector& TexelWS, const FVector& OffsetXWS, const FVector& OffsetYWS, float& OutMipLevel)
	{
		FVector2D TexelScreenSpace[3];

		bool bValid = true;
		bValid &= ProjectWorldToScreenFast(TexelWS, ViewInfo.ViewportRect, ViewInfo.ViewProjectionMatrix, TexelScreenSpace[0]);
		bValid &= ProjectWorldToScreenFast(TexelWS + OffsetXWS, ViewInfo.ViewportRect, ViewInfo.ViewProjectionMatrix, TexelScreenSpace[1]);
		bValid &= ProjectWorldToScreenFast(TexelWS + OffsetYWS, ViewInfo.ViewportRect, ViewInfo.ViewProjectionMatrix, TexelScreenSpace[2]);

		if (bValid)
		{
			float DistX = FVector2D::DistSquared(TexelScreenSpace[0], TexelScreenSpace[1]);
			float DistY = FVector2D::DistSquared(TexelScreenSpace[0], TexelScreenSpace[2]);
			OutMipLevel = FMath::Max(0.5f * (float)FMath::Log2(1.0f / FMath::Max(DistX, DistY)), 0.0f); // ~ log2(sqrt(delta))
		}

		return bValid;
	}

	class FPlaneObjectInfo : public FImgMediaMipMapObjectInfo
	{
	public:
		FPlaneObjectInfo(UMeshComponent* InMeshComponent, float InLODBias = 0.0f)
			: FImgMediaMipMapObjectInfo(InMeshComponent, InLODBias)
			, PlaneSize(FVector::ZeroVector)
		{
			// Get size of object.
			if (MeshComponent != nullptr)
			{
				PlaneSize = 2.0f * MeshComponent->CalcLocalBounds().BoxExtent;
			}
			else
			{
				UE_LOG(LogImgMedia, Error, TEXT("FPlaneImgMediaMipMapObjectInfo is missing its plane mesh component."));
			}
		}

		void CalculateVisibleTiles(const TArray<FImgMediaViewInfo>& InViewInfos, const FSequenceInfo& InSequenceInfo, TMap<int32, FImgMediaTileSelection>& VisibleTiles) const override
		{
			UMeshComponent* Mesh = MeshComponent.Get();
			if (Mesh == nullptr)
			{
				return;
			}

			// To avoid calculating tile corner mip levels multiple times over, we cache them in this array.
			CornerMipLevelsCached.SetNum((InSequenceInfo.NumTiles.X + 1) * (InSequenceInfo.NumTiles.Y + 1));

			const FTransform MeshTransform = Mesh->GetComponentTransform();
			const FVector MeshScale = Mesh->GetComponentScale();

			FVector PlaneCornerWS = MeshTransform.TransformPosition(FVector(0, -0.5f * PlaneSize.Y, 0.5f * PlaneSize.Z));
			FVector DirXWS = MeshTransform.TransformVector(FVector(0, PlaneSize.Y, 0));
			FVector DirYWS = MeshTransform.TransformVector(FVector(0, 0, -PlaneSize.Z));
			FVector TexelOffsetXWS = MeshTransform.TransformVector(FVector(0, PlaneSize.Y / InSequenceInfo.Dim.X, 0));
			FVector TexelOffsetYWS = MeshTransform.TransformVector(FVector(0, 0, -PlaneSize.Z / InSequenceInfo.Dim.Y));

			for (const FImgMediaViewInfo& ViewInfo : InViewInfos)
			{
				if (IsPrimitiveComponentHidden(Mesh->ComponentId, ViewInfo))
				{
					continue;
				}

				ResetMipLevelCache();

				// Get frustum.
				FConvexVolume ViewFrustum;
				GetViewFrustumBounds(ViewFrustum, ViewInfo.OverscanViewProjectionMatrix, false, false);

				int32 MaxLevel = InSequenceInfo.NumMipLevels - 1;
				int MipLevelDiv = 1 << MaxLevel;

				FIntPoint CurrentNumTiles;
				CurrentNumTiles.X = FMath::Max(1, FMath::CeilToInt(float(InSequenceInfo.NumTiles.X) / MipLevelDiv));
				CurrentNumTiles.Y = FMath::Max(1, FMath::CeilToInt(float(InSequenceInfo.NumTiles.Y) / MipLevelDiv));

				// Starting with tiles at the highest mip level
				TQueue<FIntVector> Tiles;
				for (int32 TileY = 0; TileY < CurrentNumTiles.Y; ++TileY)
				{
					for (int32 TileX = 0; TileX < CurrentNumTiles.X; ++TileX)
					{
						Tiles.Enqueue(FIntVector(TileX, TileY, MaxLevel));
					}
				}

				// Process all visible tiles with a (quadtree) breadth-first search
				while (!Tiles.IsEmpty())
				{
					FIntVector Tile;
					Tiles.Dequeue(Tile);

					int32 CurrentMipLevel = Tile.Z;
					MipLevelDiv = 1 << CurrentMipLevel;
					// Calculate the number of tiles at this mip level
					CurrentNumTiles.X = FMath::Max(1, FMath::CeilToInt(float(InSequenceInfo.NumTiles.X) / MipLevelDiv));
					CurrentNumTiles.Y = FMath::Max(1, FMath::CeilToInt(float(InSequenceInfo.NumTiles.Y) / MipLevelDiv));

					// Exclude subdivided tiles (enqueued below) that are not present (i.e. mipped sequences with odd number of tiles)
					if (Tile.X >= CurrentNumTiles.X || Tile.Y >= CurrentNumTiles.Y)
					{
						continue;
					}

					// Calculate the tile location in world-space
					float StepX = float(Tile.X + 0.5f) / CurrentNumTiles.X;
					float StepY = float(Tile.Y + 0.5f) / CurrentNumTiles.Y;
					FVector TileCenterWS = PlaneCornerWS + (DirXWS * StepX + DirYWS * StepY);

					// Calculate the tile radius in world space
					FVector TileSizeWS = (PlaneSize * MeshScale) / FVector(1, CurrentNumTiles.X, CurrentNumTiles.Y);
					float TileRadiusInWS = 0.5f * (float)FMath::Sqrt(2 * FMath::Square(TileSizeWS.GetAbsMax()));

					// Now we check if tile spherical bounds are in view.
					if (ViewFrustum.IntersectSphere(TileCenterWS, TileRadiusInWS))
					{
						// Calculate the visible mip level range over all tile corners.
						int32 NumVisibleCorners = 0;
						FIntVector2 MipLevelRange = FIntVector2(TNumericLimits<int32>::Max(), 0);
						for (int32 CornerY = 0; CornerY < 2; ++CornerY)
						{
							for (int32 CornerX = 0; CornerX < 2; ++CornerX)
							{
								float CalculatedLevel;
								int32 TileCornerX = Tile.X + CornerX;
								int32 TileCornerY = Tile.Y + CornerY;

								// First we query the cached corner mip levels.
								int32 MaxCornerX = InSequenceInfo.NumTiles.X + 1;
								int32 MaxCornerY = InSequenceInfo.NumTiles.Y + 1;
								FIntPoint BaseLevelCorner;
								BaseLevelCorner.X = FMath::Clamp(TileCornerX << CurrentMipLevel, 0, InSequenceInfo.NumTiles.X);
								BaseLevelCorner.Y = FMath::Clamp(TileCornerY << CurrentMipLevel, 0, InSequenceInfo.NumTiles.Y);
								bool bValidLevel = GetCachedMipLevel(BaseLevelCorner.X, BaseLevelCorner.Y, MaxCornerX, CalculatedLevel);

								// If not found, calculate and cache it.
								if (!bValidLevel)
								{
									float CornerStepX = TileCornerX / (float)CurrentNumTiles.X;
									float CornerStepY = TileCornerY / (float)CurrentNumTiles.Y;
									FVector CornersWS = PlaneCornerWS + (DirXWS * CornerStepX + DirYWS * CornerStepY);

									if (CalculateMipLevel(ViewInfo, CornersWS, TexelOffsetXWS, TexelOffsetYWS, CalculatedLevel))
									{
										CalculatedLevel += LODBias + ViewInfo.MaterialTextureMipBias;

										SetCachedMipLevel(BaseLevelCorner.X, BaseLevelCorner.Y, MaxCornerX, CalculatedLevel);
										bValidLevel = true;
									}
								}
								
								if (bValidLevel)
								{
									MipLevelRange[0] = FMath::Min(MipLevelRange[0], FMath::Clamp((int32)CalculatedLevel, 0, MaxLevel));
									MipLevelRange[1] = FMath::Max(MipLevelRange[1], FMath::Clamp(FMath::CeilToInt32(CalculatedLevel), 0, MaxLevel));
									NumVisibleCorners++;
								}
							}
						}

						// As an approximation, we force the lowest mip to 0 if only some corners are behind camera.
						if (NumVisibleCorners > 0 && NumVisibleCorners < 4)
						{
							MipLevelRange[0] = 0;
						}

						// If the lowest (calculated) mip level is below our current mip level, enqueue all 4 sub-tiles for further processing.
						if (MipLevelRange[0] < CurrentMipLevel)
						{
							for (int32 SubY = 0; SubY < FMath::Min(InSequenceInfo.NumTiles.Y, 2); ++SubY)
							{
								for (int32 SubX = 0; SubX < FMath::Min(InSequenceInfo.NumTiles.X, 2); ++SubX)
								{
									FIntVector SubTile = FIntVector((Tile.X << 1) + SubX, (Tile.Y << 1) + SubY, CurrentMipLevel - 1);
									Tiles.Enqueue(SubTile);
								}
							}
						}

						// If the highest (calculated) mip level equals or exceeds our current mip level, we register the tile as visible.
						if (MipLevelRange[1] >= CurrentMipLevel)
						{
							if (!VisibleTiles.Contains(CurrentMipLevel))
							{
								VisibleTiles.Emplace(CurrentMipLevel, FImgMediaTileSelection(CurrentNumTiles.X, CurrentNumTiles.Y));
							}

							VisibleTiles[CurrentMipLevel].SetVisible(Tile.X, Tile.Y);
						}
#if false
#if WITH_EDITOR
						// Enable this to draw a sphere where each tile is.
						Async(EAsyncExecution::TaskGraphMainThread, [TileCenterWS, TileRadiusInWS]()
							{
								UWorld* World = GEditor->GetEditorWorldContext().World();
								DrawDebugSphere(World, TileCenterWS, TileRadiusInWS, 8, FColor::Red, false, 0.05f);
							});
#endif // WITH_EDITOR
#endif // false
					}
				}
			}
		}

	private:

		/** Convenience function to get a cached calculated mip level (in mip0 tile address space). */
		FORCEINLINE bool GetCachedMipLevel(int32 Address0X, int32 Address0Y, int32 RowSize, float& OutCalculatedLevel) const
		{
			const int32 Index = Address0Y * RowSize + Address0X;

			if (CornerMipLevelsCached[Index] >= 0.0f)
			{
				OutCalculatedLevel = CornerMipLevelsCached[Index];
				
				return true;
			}

			return false;
		}

		/** Convenience function to cache a calculated mip level (in mip0 tile address space). */
		FORCEINLINE void SetCachedMipLevel(int32 Address0X, int32 Address0Y, int32 RowSize, float InCalculatedLevel) const
		{
			CornerMipLevelsCached[Address0Y * RowSize + Address0X] = InCalculatedLevel;
		}

		/** Convenience function to reset the cache. */
		FORCEINLINE void ResetMipLevelCache() const
		{
			for (float& Level : CornerMipLevelsCached)
			{
				Level = -1.0f;
			}
		}

		/** Local size of this mesh component. */
		FVector PlaneSize;

		/** Cached calculating mip levels (at mip0). */
		mutable TArray<float> CornerMipLevelsCached;
	};

	class FSphereObjectInfo : public FImgMediaMipMapObjectInfo
	{
	public:
		using FImgMediaMipMapObjectInfo::FImgMediaMipMapObjectInfo;

		void CalculateVisibleTiles(const TArray<FImgMediaViewInfo>& InViewInfos, const FSequenceInfo& InSequenceInfo, TMap<int32, FImgMediaTileSelection>& VisibleTiles) const override
		{
			UMeshComponent* Mesh = MeshComponent.Get();
			if (Mesh == nullptr)
			{
				return;
			}

			const float DefaultSphereRadius = 50.0f;

			for (const FImgMediaViewInfo& ViewInfo : InViewInfos)
			{
				if (IsPrimitiveComponentHidden(Mesh->ComponentId, ViewInfo))
				{
					continue;
				}

				// Analytical derivation of visible tiles from the view frustum, given a sphere presumed to be infinitely large
				FConvexVolume ViewFrustum;
				GetViewFrustumBounds(ViewFrustum, ViewInfo.OverscanViewProjectionMatrix, false, false);
				
				// Include all tiles containted in the visible UV region
				int32 NumX = InSequenceInfo.NumTiles.X;
				int32 NumY = InSequenceInfo.NumTiles.Y;
				for (int32 TileY = 0; TileY < NumY; ++TileY)
				{
					for (int32 TileX = 0; TileX < NumX; ++TileX)
					{
						FVector2D TileCornerUV = FVector2D((float)TileX / NumX, (float)TileY / NumY);
						
						// Convert from latlong UV to spherical coordinates
						FVector2D TileCornerSpherical = FVector2D(UE_PI * TileCornerUV.Y, UE_TWO_PI * TileCornerUV.X);
						
						// Adjust spherical coordinates to default sphere UVs
						TileCornerSpherical.Y = -TileCornerSpherical.Y;

						FVector TileCorner = TileCornerSpherical.SphericalToUnitCartesian() * DefaultSphereRadius;
						TileCorner = Mesh->GetComponentTransform().TransformPosition(TileCorner);

						// For each tile corner, we include all adjacent tiles
						if (ViewFrustum.IntersectPoint(TileCorner))
						{
							if (!VisibleTiles.Contains(0))
							{
								VisibleTiles.Emplace(0, FImgMediaTileSelection(NumX, NumY));
							}

							int32 AdjacentX = TileX > 0 ? TileX - 1 : NumX - 1;
							int32 AdjacentY = TileY > 0 ? TileY - 1 : NumY - 1;
							VisibleTiles[0].SetVisible(TileX, TileY);
							VisibleTiles[0].SetVisible(AdjacentX, TileY);
							VisibleTiles[0].SetVisible(TileX, AdjacentY);
							VisibleTiles[0].SetVisible(AdjacentX, AdjacentY);
						}

#if false
#if WITH_EDITOR
						// Enable this to draw a sphere where each tile is.
						Async(EAsyncExecution::TaskGraphMainThread, [TileCorner]()
							{
								UWorld* World = GEditor->GetEditorWorldContext().World();
								DrawDebugPoint(World, TileCorner, 5.0f, FColor::Red, false, 0.05f);
							});
#endif // WITH_EDITOR
#endif // false
					}
				}

				if (VisibleTiles.Contains(0))
				{
					FIntPoint BaseDim = VisibleTiles[0].GetDimensions();
					TArray<FIntPoint> BaseVisibleCoordinates = VisibleTiles[0].GetVisibleCoordinates();

					// Include tiles visible at the base level in higher mip levels.
					for (int32 Level = 1; Level < InSequenceInfo.NumMipLevels; ++Level)
					{
						const int32 MipLevelDiv = 1 << Level;

						if (!VisibleTiles.Contains(Level))
						{
							NumX = FMath::CeilToInt((float)BaseDim.X / MipLevelDiv);
							NumY = FMath::CeilToInt((float)BaseDim.Y / MipLevelDiv);
							VisibleTiles.Emplace(Level, FImgMediaTileSelection(NumX, NumY));
						}

						for (const FIntPoint& Coord : BaseVisibleCoordinates)
						{
							VisibleTiles[Level].SetVisible(Coord.X / MipLevelDiv, Coord.Y / MipLevelDiv);
						}
					}
				}
			}
		}
	};

} //end anonymous namespace

FImgMediaMipMapInfo::FImgMediaMipMapInfo()
	: bIsCacheValid(false)
{
}

FImgMediaMipMapInfo::~FImgMediaMipMapInfo()
{
	ClearAllObjects();
}

void FImgMediaMipMapInfo::AddObject(AActor* InActor, float Width, float LODBias, EMediaTextureVisibleMipsTiles MeshType)
{
	if (InActor != nullptr)
	{
		UMeshComponent* MeshComponent = Cast<UMeshComponent>(InActor->FindComponentByClass(UMeshComponent::StaticClass()));
		if (MeshComponent != nullptr)
		{
			switch (MeshType)
			{
			case EMediaTextureVisibleMipsTiles::Plane:
				Objects.Add(new FPlaneObjectInfo(MeshComponent, LODBias));
				break;
			case EMediaTextureVisibleMipsTiles::Sphere:
				Objects.Add(new FSphereObjectInfo(MeshComponent, LODBias));
				break;
			default:
				Objects.Add(new FImgMediaMipMapObjectInfo(MeshComponent, LODBias));
				break;
			}
		}
	}
}

void FImgMediaMipMapInfo::RemoveObject(AActor* InActor)
{
	if (InActor != nullptr)
	{
		for (int Index = 0; Index < Objects.Num(); ++Index)
		{
			FImgMediaMipMapObjectInfo* Info = Objects[Index];

			if (UMeshComponent* MeshComponent = Info->GetMeshComponent())
			{
				if (InActor == MeshComponent->GetOuter())
				{
					Objects.RemoveAtSwap(Index);
					delete Info;
					break;
				}
			}
		}
	}
}

void FImgMediaMipMapInfo::AddObjectsUsingThisMediaTexture(UMediaTexture* InMediaTexture)
{
	// Get objects using this texture.
	FMediaTextureTracker& TextureTracker = FMediaTextureTracker::Get();
	const TArray<TWeakPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe>>* ObjectInfos = TextureTracker.GetObjects(InMediaTexture);
	if (ObjectInfos != nullptr)
	{
		for (TWeakPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe> ObjectInfoPtr : *ObjectInfos)
		{
			TSharedPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe> ObjectInfo = ObjectInfoPtr.Pin();
			if (ObjectInfo.IsValid())
			{
				AActor* Owner = ObjectInfo->Object.Get();
				if (Owner != nullptr)
				{
					AddObject(Owner, 0.0f, ObjectInfo->MipMapLODBias, ObjectInfo->VisibleMipsTilesCalculations);
				}
			}
		}
	}
}

void FImgMediaMipMapInfo::RemoveObjectsUsingThisMediaTexture(UMediaTexture* InMediaTexture)
{
	// Get objects using this texture.
	FMediaTextureTracker& TextureTracker = FMediaTextureTracker::Get();
	const TArray<TWeakPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe>>* ObjectInfos = TextureTracker.GetObjects(InMediaTexture);
	if (ObjectInfos != nullptr)
	{
		for (TWeakPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe> ObjectInfoPtr : *ObjectInfos)
		{
			TSharedPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe> ObjectInfo = ObjectInfoPtr.Pin();
			if (ObjectInfo.IsValid())
			{
				RemoveObject(ObjectInfo->Object.Get());
			}
		}
	}
}

void FImgMediaMipMapInfo::ClearAllObjects()
{
	for (FImgMediaMipMapObjectInfo* Info : Objects)
	{
		delete Info;
	}
	Objects.Empty();
}


void FImgMediaMipMapInfo::SetTextureInfo(FName InSequenceName, int32 InNumMipMaps,
	const FIntPoint& InNumTiles, const FIntPoint& InSequenceDim)
{
	SequenceInfo.Name = InSequenceName;
	SequenceInfo.Dim = InSequenceDim;

	// To simplify logic, we assume we always have at least one mip level and one tile.
	SequenceInfo.NumMipLevels = FMath::Max(1, InNumMipMaps);
	SequenceInfo.NumTiles.X = FMath::Max(1, InNumTiles.X);
	SequenceInfo.NumTiles.Y = FMath::Max(1, InNumTiles.Y);
}

TMap<int32, FImgMediaTileSelection> FImgMediaMipMapInfo::GetVisibleTiles()
{
	// This is called from the loader one thread at a time as the call is guarded by a critical section.
	// So no need for thread safety here with regards to this function.
	// However the Tick is called from a different thread so care must still be taken when
	// accessing things that are modified by code external to this function.
	
	// Do we need to update the cache?
	if (bIsCacheValid == false)
	{
		UpdateMipLevelCache();
	}

	return CachedVisibleTiles;
}

void FImgMediaMipMapInfo::UpdateMipLevelCache()
{
	SCOPE_CYCLE_COUNTER(STAT_ImgMedia_MipMapUpdateCache);

	{
		FScopeLock Lock(&InfoCriticalSection);

		CachedVisibleTiles.Reset();

		// Loop over all objects.
		for (FImgMediaMipMapObjectInfo* ObjectInfo : Objects)
		{
			ObjectInfo->CalculateVisibleTiles(ViewInfos, SequenceInfo, CachedVisibleTiles);
		}
	}
	
	// Mark cache as valid.
	bIsCacheValid = true;
}

void FImgMediaMipMapInfo::Tick(float DeltaTime)
{
	FScopeLock Lock(&InfoCriticalSection);

	const TSharedPtr<FImgMediaSceneViewExtension, ESPMode::ThreadSafe>& SVE = IImgMediaModule::Get().GetSceneViewExtension();
	if (SVE.IsValid())
	{
		ViewInfos = SVE->GetViewInfos();
	}

	// Let the cache update this frame.
	bIsCacheValid = false;

	// Display debug?
	if (CVarImgMediaMipMapDebugEnable.GetValueOnGameThread())
	{
		if (GEngine != nullptr)
		{
			TSet<int32> VisibleMips;
			int32 NumVisibleTiles = 0;

			for (const auto& MipTiles : CachedVisibleTiles)
			{
				VisibleMips.Add(MipTiles.Key);

				const FImgMediaTileSelection& TileSelection = MipTiles.Value;
				NumVisibleTiles += TileSelection.NumVisibleTiles();
			}

			if (VisibleMips.Num() > 0)
			{
				auto VisibleMipsIt = VisibleMips.CreateConstIterator();
				FString Mips = FString::FromInt(*VisibleMipsIt);
				
				for (++VisibleMipsIt; VisibleMipsIt; ++VisibleMipsIt)
				{
					Mips += FString(TEXT(", ")) + FString::FromInt(*VisibleMipsIt);
				}

				GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Yellow, *FString::Printf(TEXT("%s Mip Level(s): [%s]"), *SequenceInfo.Name.ToString(), *Mips));
				GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Yellow, *FString::Printf(TEXT("%s Num Tile(s): %d"), *SequenceInfo.Name.ToString(), NumVisibleTiles));
			}
		}
	}
}


