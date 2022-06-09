// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoElementLineBase.h"
#include "InputState.h"
#include "UObject/ObjectMacros.h"
#include "GizmoElementCircle.generated.h"

/**
 * Simple object intended to be used as part of 3D Gizmos.
 * Draws a filled or line circle based on parameters.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UGizmoElementCircle : public UGizmoElementLineBase
{
	GENERATED_BODY()

public:
	//~ Begin UGizmoElementBase Interface.
	virtual void Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState) override;
	virtual FInputRayHit LineTrace(const FVector Start, const FVector Direction) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UGizmoElementBase Interface.

	// Circle center.
	virtual void SetCenter(FVector InCenter);
	virtual FVector GetCenter() const;

	// Normal to circle.
	virtual void SetNormal(FVector InNormal);
	virtual FVector GetNormal() const;

	// Circle radius.
	virtual void SetRadius(float Radius);
	virtual float GetRadius() const;

	// Number of sides for rendering circle.
	virtual void SetNumSides(int InNumSides);
	virtual int GetNumSides() const;

	// Line color when bDrawLine is true.
	virtual void SetLineColor(const FLinearColor& InColor);
	virtual FLinearColor GetLineColor() const;

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

	// Screen space, when true orients the circle to the screen up and side vectors
	virtual void SetScreenSpace(bool InScreenSpace);
	virtual bool GetScreenSpace();

protected:

	// Circle center.
	UPROPERTY()
	FVector Center = FVector::ZeroVector;

	// Normal to circle.
	UPROPERTY()
	FVector Normal = FVector::ForwardVector;

	// Circle radius.
	UPROPERTY()
	float Radius = 100.0f;

	// Number of sides for rendering circle.
	UPROPERTY()
	int NumSides = 64;

	// Line color when bFill is false.
	UPROPERTY()
	FLinearColor LineColor = FLinearColor::White;

	// Whether to render solid circle.
	UPROPERTY()
	bool bDrawMesh = true;

	// Whether to render line circle.
	UPROPERTY()
	bool bDrawLine = false;

	// Whether to perform hit test on mesh.
	UPROPERTY()
	bool bHitMesh = true;

	// Whether to perform hit test on line.
	UPROPERTY()
	bool bHitLine = false;

	// Screen space flag
	UPROPERTY()
	bool bScreenSpace = false;
};