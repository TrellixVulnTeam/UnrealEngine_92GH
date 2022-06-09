// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class NaniteDisplacedMeshEditor : ModuleRules
	{
		public NaniteDisplacedMeshEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("NaniteDisplacedMeshEditor/Private");
			PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetTools",
					"Core",
					"CoreUObject",
					"EditorFramework",
					"EditorStyle",
					"Engine",
					"PropertyEditor",
					"RHI",
					"Slate",
					"Slate",
					"SlateCore",
					"TargetPlatform",
					"UnrealEd",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"EditorSubsystem",
					"NaniteDisplacedMesh"
				}
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"DerivedDataCache",
				}
			);
		}
	}
}
