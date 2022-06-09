// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoElementLineBase.h"
#include "InputState.h"
#include "UObject/ObjectMacros.h"
#include "GizmoElementRectangle.generated.h"

class FPrimitiveDrawInterface;
class FMaterialRenderProxy;

/**
 * Simple object intended to be used as part of 3D Gizmos.
 * Draws a rectangle based on parameters.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UGizmoElementRectangle : public UGizmoElementLineBase
{
	GENERATED_BODY()

public:

	//~ Begin UGizmoElementBase Interface.
	virtual void Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState) override;
	virtual FInputRayHit LineTrace(const FVector Start, const FVector Direction) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UGizmoElementBase Interface.

	// Location of rectangle center
	virtual void SetCenter(FVector InCenter);
	virtual FVector GetCenter() const;

	// Width
	virtual void SetWidth(float InWidth);
	virtual float GetWidth() const;

	// Height
	virtual void SetHeight(float InHeight);
	virtual float GetHeight() const;

	// Up direction
	virtual void SetUpDirection(const FVector& InUpDirection);
	virtual FVector GetUpDirection() const;

	// Side direction
	virtual void SetSideDirection(const FVector& InSideDirection);
	virtual FVector GetSideDirection() const;

	// Screen space, when true orients the rectangle to the screen up and side vectors
	virtual void SetScreenSpace(bool InScreenSpace);
	virtual bool GetScreenSpace() const;

	// Line color
	virtual void SetLineColor(const FColor& InLineColor);
	virtual FColor GetLineColor() const;

	// Draw mesh
	virtual void SetDrawMesh(bool InDrawMesh);
	virtual bool GetDrawMesh() const;

	// Draw line
	virtual void SetDrawLine(bool InDrawLine);
	virtual bool GetDrawLine() const;

	// Hit mesh
	virtual void SetHitMesh(bool InHitMesh);
	virtual bool GetHitMesh() const;

	// Hit line
	virtual void SetHitLine(bool InHitLine);
	virtual bool GetHitLine() const;

protected:

	// Location of rectangle center
	UPROPERTY()
	FVector Center = FVector::ZeroVector;

	// Width
	UPROPERTY()
	float Width = 1.0f;

	// Height
	UPROPERTY()
	float Height = 1.0f;

	// Up direction
	UPROPERTY()
	FVector UpDirection = FVector(0.0f, 0.0f, 1.0f);

	// Side direction
	UPROPERTY()
	FVector SideDirection = FVector(0.0f, 1.0f, 0.0f);

	// Screen space flag
	UPROPERTY()
	bool bScreenSpace = false;

	// Line color when bShowOutline is false.
	UPROPERTY()
	FColor LineColor = FColor::White;

	UPROPERTY()
	bool bDrawMesh = true;

	UPROPERTY()
	bool bDrawLine = false;

	UPROPERTY()
	bool bHitMesh = true;

	UPROPERTY()
	bool bHitLine = false;
};