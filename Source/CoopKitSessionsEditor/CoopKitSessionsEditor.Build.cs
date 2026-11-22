// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

using UnrealBuildTool;

// Editor part of CoopKitSessions: factories, asset definitions and the data editor window of the plugin's data types,
// the CoopKit workspace (hub, card, New wizard, AI agent skills) and the demo's "Play with 2 windows" restart of Play In Editor.
// Never ships in a Game or Server build.
public class CoopKitSessionsEditor : ModuleRules
{
	public CoopKitSessionsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		// Fab 4.3.6.2.c: plugins must be IWYU compatible.
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// AssetRegistry is public: CKSessionsHub.h hands out FAssetData. GameplayTags: CKSessionsCompat.h of the runtime module.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"UnrealEd",
			"CoopKitSessions"
		});

		// The workspace (decision-63 of the CoopKit line, doc-57): the CoopKit Hub tab in the Tools menu (WorkspaceMenuStructure),
		// entity-wide operations and new assets (AssetTools), the card's part pickers (ContentBrowser), the plugin manager and the
		// Tools menu for Install AI Agent Skills (Projects, ToolMenus; decision-68 of the CoopKit line).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetDefinition",
			"AssetTools",
			"ContentBrowser",
			"DeveloperToolSettings",
			"EngineSettings",
			"InputCore",
			"Json",
			"OnlineSubsystem",
			"Projects",
			"PropertyEditor",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"WorkspaceMenuStructure"
		});
	}
}
