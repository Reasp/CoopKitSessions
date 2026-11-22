// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Install AI Agent Skills (addition to contract version 1 of the CoopKit workspace, decision-68 of the CoopKit line).
 * Every CoopKit plugin ships skills for AI coding agents as plain text in Extras/AgentSkills/<skill_name>/ (a folder with
 * SKILL.md in the open Agent Skills format). This plugin's copy of the hub finds the skills of every enabled CoopKit
 * plugin and copies them into the project: <Project>/.claude/skills/ (Claude Code) and <Project>/.agents/skills/
 * (Antigravity, Codex). Plugins share no code: each copy of the hub does the same work and any copy gives the same result.
 */
namespace CKSessions::Skills
{
	/** Folder of the skills inside a plugin, relative to the plugin folder. */
	inline constexpr const TCHAR* PluginSkillsFolder = TEXT("Extras/AgentSkills");

	/** One skill found in an enabled CoopKit plugin. */
	struct FSkillSource
	{
		/** Name of the plugin that ships the skill. */
		FString Plugin;

		/**
		 * Skill name from the frontmatter of SKILL.md (the slash command, the installed folder name), e.g. coopkit-sessions.
		 * The folder in the plugin spells hyphens as underscores (coopkit_sessions): Fab allows only letters, digits and
		 * underscores in file names.
		 */
		FString Name;

		/** Absolute folder of the skill (the one with SKILL.md). */
		FString Directory;
	};

	/** What an installation did. */
	struct FInstallResult
	{
		/** Skill folders written, absolute paths. */
		TArray<FString> Written;

		/** Skill folders that existed and were replaced, absolute paths (subset of Written). */
		TArray<FString> Replaced;

		/** Problems, e.g. a folder that could not be written. */
		TArray<FString> Errors;
	};

	/** What the command tells the user after an installation: the dialog's icon and text. */
	struct FInstallReport
	{
		/** Info when every skill folder was written; Warning when something failed (partly or wholly). */
		EAppMsgCategory Category = EAppMsgCategory::Warning;

		FText Message;
	};

	/** Skills of every enabled plugin whose name starts with CoopKit, sorted by skill name; the first plugin wins a name. */
	COOPKITSESSIONSEDITOR_API TArray<FSkillSource> FindSkills();

	/** The folders the skills are installed into: <Project>/.claude/skills and <Project>/.agents/skills. */
	COOPKITSESSIONSEDITOR_API TArray<FString> GetTargetRoots(const FString& ProjectDir);

	/** Folders of the skills that already exist in the project (installing replaces them). */
	COOPKITSESSIONSEDITOR_API TArray<FString> FindExisting(TConstArrayView<FSkillSource> Skills, const FString& ProjectDir);

	/** Copies the skills into both target folders; an existing skill folder is deleted first, so no stale file stays. */
	COOPKITSESSIONSEDITOR_API FInstallResult Install(TConstArrayView<FSkillSource> Skills, const FString& ProjectDir);

	/**
	 * The "Install AI Agent Skills" command of the hub and of the Tools > CoopKit menu. Finds the skills, asks before
	 * replacing existing ones (no dialog in an unattended run: it replaces), installs and reports. Returns true when at
	 * least one skill was installed and nothing failed.
	 */
	COOPKITSESSIONSEDITOR_API bool RunInstallCommand();

	/**
	 * The report of an installation: success (folders written, no error) is information, with the folders and how to call
	 * a skill; an installation with errors is a warning that lists them, also when some folders were written.
	 */
	COOPKITSESSIONSEDITOR_API FInstallReport MakeInstallReport(const FInstallResult& Result, TConstArrayView<FSkillSource> Skills);

	/** Project folder the command installs into: the project directory, or the folder set by SetProjectDirOverrideForTests. */
	COOPKITSESSIONSEDITOR_API FString GetProjectDir();

	/** For tests: installs into this folder instead of the project; an empty string restores the project folder. */
	COOPKITSESSIONSEDITOR_API void SetProjectDirOverrideForTests(const FString& ProjectDir);

	/** Adds Tools > CoopKit > Install AI Agent Skills (the hub owner calls this). */
	void RegisterMenu();

	/** Removes the menu entry. */
	void UnregisterMenu();
}
