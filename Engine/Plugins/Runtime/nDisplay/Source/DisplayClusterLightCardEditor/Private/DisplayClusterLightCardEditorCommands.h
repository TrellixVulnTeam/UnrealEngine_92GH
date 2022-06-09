// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

#include "DisplayClusterLightCardEditorStyle.h"


class FDisplayClusterLightCardEditorCommands 
	: public TCommands<FDisplayClusterLightCardEditorCommands>
{
public:
	FDisplayClusterLightCardEditorCommands()
		: TCommands<FDisplayClusterLightCardEditorCommands>(TEXT("DisplayClusterLightCardEditor"), 
			NSLOCTEXT("Contexts", "DisplayClusterLightCardEditor", "Display Cluster LightCard Editor"),
			NAME_None,
			FDisplayClusterLightCardEditorStyle::Get().GetStyleSetName())
	{ }

	virtual void RegisterCommands() override;

public:
	// Viewport commands
	TSharedPtr<FUICommandInfo> ResetCamera;

	TSharedPtr<FUICommandInfo> PerspectiveProjection;
	TSharedPtr<FUICommandInfo> OrthographicProjection;
	TSharedPtr<FUICommandInfo> AzimuthalProjection;

	TSharedPtr<FUICommandInfo> ViewOrientationTop;
	TSharedPtr<FUICommandInfo> ViewOrientationBottom;
	TSharedPtr<FUICommandInfo> ViewOrientationLeft;
	TSharedPtr<FUICommandInfo> ViewOrientationRight;
	TSharedPtr<FUICommandInfo> ViewOrientationFront;
	TSharedPtr<FUICommandInfo> ViewOrientationBack;

	TSharedPtr<FUICommandInfo> AddNewLightCard;
	TSharedPtr<FUICommandInfo> AddExistingLightCard;
	TSharedPtr<FUICommandInfo> RemoveLightCard;
	TSharedPtr<FUICommandInfo> PasteHere;

	TSharedPtr<FUICommandInfo> DrawLightCard;
};
