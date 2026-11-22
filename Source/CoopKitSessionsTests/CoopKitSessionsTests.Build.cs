// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// Editor-only automation tests of CoopKitSessions (filter "CoopKit"). The module never ships in a Game or Server build.
public class CoopKitSessionsTests : ModuleRules
{
	public CoopKitSessionsTests(ReadOnlyTargetRules Target) : base(Target)
	{
		// Fab 4.3.6.2.c: plugins must be IWYU compatible.
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Tests reach the runtime module's private header-only helpers (CKSessionsActorTags.h) without making them public.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "CoopKitSessions", "Private"));

		// The workspace tests reach the editor module's widgets (SCKSessionsCard.h, SCKSessionsHub.h) the same way.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "CoopKitSessionsEditor", "Private"));

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"AssetTools",
			"Core",
			"CoreUObject",
			"Engine",
			"EngineSettings",
			"GameplayTags",
			"Json",
			"MessageLog",
			"NetCore",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"Slate",
			"SlateCore",
			"UMG",
			"UnrealEd",
			"CoopKitSessions",
			"CoopKitSessionsEditor"
		});
	}
}
