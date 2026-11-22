// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

using UnrealBuildTool;

public class CoopKitSessions : ModuleRules
{
	public CoopKitSessions(ReadOnlyTargetRules Target) : base(Target)
	{
		// Fab 4.3.6.2.c: plugins must be IWYU compatible.
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// OnlineSubsystem is public: the plugin's public types wrap its session settings and search results (decision-16).
		// GameplayTags (engine module, not a plugin) is public: the lobby components publish their state as Gameplay Tags.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "DeveloperSettings", "Engine", "GameplayTags", "OnlineSubsystem" });

		// CoreOnline: unique net ids of players. OnlineSubsystemUtils: Online::GetSubsystem(World) picks the online subsystem instance of a world.
		// EngineSettings: the game default map, where a player returns without a menu map.
		// Projects: the enabled plugins, for the setup check of Steam and EOS (CKSessionsDiagnostics).
		PrivateDependencyModuleNames.AddRange(new string[] { "CoreOnline", "EngineSettings", "OnlineSubsystemUtils", "Projects" });

		// UMG, Slate, SlateCore: the demo's panel of buttons (CKSessionsDemoPanelWidget), built in C++ without Widget Blueprints.
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "UMG" });

		// Engine-version-specific module dependencies go here, keyed on Target.Version, for example:
		// if (Target.Version.MajorVersion > 5 || Target.Version.MinorVersion >= 6) { PrivateDependencyModuleNames.Add("..."); }
		// C++ API differences between engine versions live only in Public/CKSessionsCompat.h.
	}
}
