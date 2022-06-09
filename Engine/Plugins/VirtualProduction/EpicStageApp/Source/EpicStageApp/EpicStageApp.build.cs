// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EpicStageApp : ModuleRules
{
	public EpicStageApp(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Serialization"
			}
		);

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"DeveloperSettings",
				"DisplayCluster",
				"DisplayClusterLightCardEditorShaders",
				"DisplayClusterScenePreview",
				"Engine",
				"ImageWrapper",
				"Networking",
				"RemoteControl",
				"RemoteControlCommon",
				"Sockets",
				"WebRemoteControl"
			}
        );
	}
}
