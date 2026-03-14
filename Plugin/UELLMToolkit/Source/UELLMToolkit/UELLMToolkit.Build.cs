// Copyright Natali Caggiano. All Rights Reserved.

using UnrealBuildTool;

public class UELLMToolkit : ModuleRules
{
	public UELLMToolkit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false; // UE 5.7: UserWidget.h + EnhancedInputComponent.h FOnInputAction redefinition in unity builds

		PublicIncludePaths.AddRange(
			new string[] {
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"UnrealEd",
				"ToolMenus",
				"Projects",
				"EditorFramework",
				"WorkspaceMenuStructure"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Json",
				"JsonUtilities",
				"HTTP",
				"HTTPServer",
				"Sockets",
				"Networking",
				"ImageWrapper",
				// Blueprint manipulation
				"Kismet",
				"KismetCompiler",
				"BlueprintGraph",
				"GraphEditor",
				"AssetRegistry",
				"AssetTools",
				// Animation Blueprint manipulation
				"AnimGraph",
				"AnimGraphRuntime",
				"ControlRig",
				"ControlRigDeveloper",
				"RigVM",
				"RigVMDeveloper",
				// IK Rig / Retargeting
				"IKRig",
				"IKRigEditor",
				// Asset saving
				"EditorScriptingUtilities",
				// Interchange (reimport transform overrides)
				"InterchangeEngine",
				"InterchangeCore",
				"InterchangePipelines",
				// Enhanced Input
				"EnhancedInput",
				"InputBlueprintNodes",
				// WidgetComponent lives in UMG
				"UMG",
				// Widget Blueprint editor (UWidgetBlueprint, UWidgetTree)
				"UMGEditor",
				// Persona (asset editor preview viewport tab IDs)
				"Persona",
				// RenderCore (FlushRenderingCommands for viewport capture sync)
				"RenderCore",
				// RHI (FRHIGPUTextureReadback for high-speed PIE frame capture)
				"RHI",
				// MovieSceneCapture (FFrameGrabber for async PIE auto-capture)
				"MovieSceneCapture",
				// Skeletal mesh bone editing (USkeletonModifier)
				"SkeletalMeshModifiers",
				// Skeletal mesh vertex retransformation (FSkeletalMeshOperations)
				"SkeletalMeshDescription",
				// Static mesh vertex transform (FStaticMeshOperations::ApplyTransform)
				"StaticMeshDescription",
				// Sequencer / Take Recorder
				"LevelSequence",
				"LevelSequenceEditor",
				"MovieScene",
				"MovieSceneTracks",
				"Sequencer",
				"TakesCore",
				"TakeRecorder",
				"TakeRecorderSources"
			}
		);

		// Clipboard support (FPlatformApplicationMisc) on all platforms
		PrivateDependencyModuleNames.Add("ApplicationCore");

		// Windows only
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// LiveCoding is only available in editor builds on Windows
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("LiveCoding");
			}
		}
	}
}
