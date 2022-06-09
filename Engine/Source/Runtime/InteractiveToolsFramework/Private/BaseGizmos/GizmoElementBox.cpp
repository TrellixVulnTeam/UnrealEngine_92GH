// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementBox.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "Materials/MaterialInterface.h"
#include "InputState.h"
#include "Intersection/IntrRay3OrientedBox3.h"
#include "SceneManagement.h"

void UGizmoElementBox::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
{
	if (!IsVisible())
	{
		return;
	}

	check(RenderAPI);
	const FSceneView* View = RenderAPI->GetSceneView();

	FTransform LocalToWorldTransform = RenderState.LocalToWorldTransform;

	bool bVisibleViewDependent = GetViewDependentVisibility(View, LocalToWorldTransform, Center);

	if (bVisibleViewDependent)
	{
		const UMaterialInterface* UseMaterial = GetCurrentMaterial(RenderState);

		if (UseMaterial)
		{
			FQuat AlignRot;
			FVector AdjustedSideDir, AdjustedUpDir;
			if (GetViewAlignRot(View, LocalToWorldTransform, Center, AlignRot))
			{
				AdjustedSideDir = AlignRot.RotateVector(SideDirection);
				AdjustedUpDir = AlignRot.RotateVector(UpDirection);
			}
			else
			{
				AdjustedSideDir = SideDirection;
				AdjustedUpDir = UpDirection;
			}

			FQuat Rotation = FRotationMatrix::MakeFromYZ(AdjustedSideDir, AdjustedUpDir).ToQuat();
			FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
			FTransform DrawTransform = FTransform(Rotation, Center) * LocalToWorldTransform;
			const FVector HalfDimensions = Dimensions * 0.5;
			DrawBox(PDI, DrawTransform.ToMatrixWithScale(), HalfDimensions, UseMaterial->GetRenderProxy(), SDPG_Foreground);
		}
	}

	CacheRenderState(LocalToWorldTransform, RenderState.PixelToWorldScale, bVisibleViewDependent);
}

FInputRayHit UGizmoElementBox::LineTrace(const FVector RayOrigin, const FVector RayDirection)
{
	if (IsHittableInView())
	{
		const FVector YAxis = CachedLocalToWorldTransform.TransformVectorNoScale(SideDirection);
		const FVector ZAxis = CachedLocalToWorldTransform.TransformVectorNoScale(UpDirection);
		const FVector XAxis = FVector::CrossProduct(YAxis, ZAxis);
		const FVector WorldCenter = CachedLocalToWorldTransform.TransformPosition(Center);
		const double Scale = CachedLocalToWorldTransform.GetScale3D().X;
		const FVector WorldExtent = Dimensions * Scale * 0.5;

		double HitDepth = 0.0;
		UE::Geometry::TRay<double> Ray(RayOrigin, RayDirection);
		UE::Geometry::TFrame3<double> Frame(WorldCenter, XAxis, YAxis, ZAxis);
		UE::Geometry::TOrientedBox3<double> Box(Frame, WorldExtent);
		if (UE::Geometry::TIntrRay3OrientedBox3<double>::FindIntersection(Ray, Box, HitDepth))
		{
			FInputRayHit RayHit(HitDepth);
			RayHit.SetHitObject(this);
			RayHit.HitIdentifier = PartIdentifier;
			return RayHit;
		}
	}

	return FInputRayHit();
}

FBoxSphereBounds UGizmoElementBox::CalcBounds(const FTransform& LocalToWorld) const
{
	// @todo - implement box-sphere bounds calculation
	return FBoxSphereBounds();
}

void UGizmoElementBox::SetCenter(const FVector& InCenter)
{
	Center = InCenter;
}

FVector UGizmoElementBox::GetCenter() const
{
	return Center;
}

void UGizmoElementBox::SetUpDirection(const FVector& InUpDirection)
{
	UpDirection = InUpDirection;
	UpDirection.Normalize();
}

FVector UGizmoElementBox::GetUpDirection() const
{
	return UpDirection;
}

void UGizmoElementBox::SetSideDirection(const FVector& InSideDirection)
{
	SideDirection = InSideDirection;
	SideDirection.Normalize();
}

FVector UGizmoElementBox::GetSideDirection() const
{
	return SideDirection;
}

FVector UGizmoElementBox::GetDimensions() const
{
	return Dimensions;
}

void UGizmoElementBox::SetDimensions(const FVector& InDimensions)
{
	Dimensions = InDimensions;
}

