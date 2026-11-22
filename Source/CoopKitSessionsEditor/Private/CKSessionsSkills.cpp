// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsSkills.h"
#include "CKSessionsHub.h"
#include "HAL/FileManager.h"
#include "Framework/Commands/UIAction.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "CKSessionsSkills"

namespace CKSessions::Skills
{
	namespace
	{
		const TCHAR* const SkillFileName = TEXT("SKILL.md");
		const FName MenuOwner(TEXT("CoopKitSessionsSkills"));
		const FName ToolsMenuName(TEXT("LevelEditor.MainMenu.Tools"));

		FString GProjectDirOverride;

		/**
		 * The name in the frontmatter of a SKILL.md: lowercase letters, digits and single hyphens (the Agent Skills format).
		 * Empty when the file has no valid name. Fab allows no hyphen in file names of a plugin, so the folder in the plugin
		 * spells it with underscores (coopkit_sessions) and the installed folder takes this name (coopkit-sessions).
		 */
		FString ReadSkillName(const FString& SkillFile)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *SkillFile))
			{
				return FString();
			}
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines, false);
			if (Lines.IsEmpty() || !Lines[0].TrimEnd().Equals(TEXT("---")))
			{
				return FString();
			}
			for (int32 Index = 1; Index < Lines.Num() && !Lines[Index].TrimEnd().Equals(TEXT("---")); ++Index)
			{
				if (Lines[Index].StartsWith(TEXT("name:")))
				{
					const FString Name = Lines[Index].RightChop(5).TrimStartAndEnd();
					bool bValid = !Name.IsEmpty() && Name.Len() <= 64 && !Name.StartsWith(TEXT("-")) && !Name.EndsWith(TEXT("-")) && !Name.Contains(TEXT("--"));
					for (const TCHAR Char : Name)
					{
						bValid = bValid && ((Char >= TEXT('a') && Char <= TEXT('z')) || (Char >= TEXT('0') && Char <= TEXT('9')) || Char == TEXT('-'));
					}
					return bValid ? Name : FString();
				}
			}
			return FString();
		}

		FString NameList(const TArray<FString>& Paths)
		{
			TArray<FString> Names;
			for (const FString& Path : Paths)
			{
				Names.AddUnique(FPaths::GetCleanFilename(Path));
			}
			return FString::Join(Names, TEXT(", "));
		}
	}

	TArray<FSkillSource> FindSkills()
	{
		TArray<FSkillSource> Skills;
		TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPlugins();
		Plugins.Sort([](const TSharedRef<IPlugin>& A, const TSharedRef<IPlugin>& B) { return A->GetName() < B->GetName(); });
		for (const TSharedRef<IPlugin>& Plugin : Plugins)
		{
			if (!Plugin->GetName().StartsWith(TEXT("CoopKit")))
			{
				continue;
			}
			const FString Root = FPaths::Combine(Plugin->GetBaseDir(), PluginSkillsFolder);
			TArray<FString> Folders;
			IFileManager::Get().FindFiles(Folders, *FPaths::Combine(Root, TEXT("*")), false, true);
			for (const FString& Folder : Folders)
			{
				const FString Directory = FPaths::Combine(Root, Folder);
				const FString Name = ReadSkillName(FPaths::Combine(Directory, SkillFileName));
				if (Name.IsEmpty())
				{
					UE_LOG(LogCKSessionsEditor, Warning, TEXT("Skill folder %s has no SKILL.md with a valid name; skipped."), *Directory);
				}
				else if (!Skills.ContainsByPredicate([&Name](const FSkillSource& Other) { return Other.Name == Name; }))
				{
					Skills.Add({ Plugin->GetName(), Name, Directory });
				}
			}
		}
		Skills.Sort([](const FSkillSource& A, const FSkillSource& B) { return A.Name < B.Name; });
		return Skills;
	}

	TArray<FString> GetTargetRoots(const FString& ProjectDir)
	{
		return { FPaths::Combine(ProjectDir, TEXT(".claude"), TEXT("skills")), FPaths::Combine(ProjectDir, TEXT(".agents"), TEXT("skills")) };
	}

	TArray<FString> FindExisting(const TConstArrayView<FSkillSource> Skills, const FString& ProjectDir)
	{
		TArray<FString> Existing;
		for (const FString& Root : GetTargetRoots(ProjectDir))
		{
			for (const FSkillSource& Skill : Skills)
			{
				const FString Target = FPaths::Combine(Root, Skill.Name);
				if (IFileManager::Get().DirectoryExists(*Target))
				{
					Existing.Add(Target);
				}
			}
		}
		return Existing;
	}

	FInstallResult Install(const TConstArrayView<FSkillSource> Skills, const FString& ProjectDir)
	{
		FInstallResult Result;
		IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
		for (const FString& Root : GetTargetRoots(ProjectDir))
		{
			for (const FSkillSource& Skill : Skills)
			{
				const FString Target = FPaths::Combine(Root, Skill.Name);
				const bool bExisted = IFileManager::Get().DirectoryExists(*Target);
				if (bExisted && !IFileManager::Get().DeleteDirectory(*Target, false, true))
				{
					Result.Errors.Add(FString::Printf(TEXT("Could not remove the old %s"), *Target));
					continue;
				}
				if (!Files.CopyDirectoryTree(*Target, *Skill.Directory, true) || !IFileManager::Get().FileExists(*FPaths::Combine(Target, SkillFileName)))
				{
					Result.Errors.Add(FString::Printf(TEXT("Could not copy %s to %s"), *Skill.Directory, *Target));
					continue;
				}
				Result.Written.Add(Target);
				if (bExisted)
				{
					Result.Replaced.Add(Target);
				}
			}
		}
		return Result;
	}

	FInstallReport MakeInstallReport(const FInstallResult& Result, const TConstArrayView<FSkillSource> Skills)
	{
		FInstallReport Report;
		if (Result.Errors.IsEmpty() && !Result.Written.IsEmpty())
		{
			Report.Category = EAppMsgCategory::Info;
			Report.Message = FText::Format(LOCTEXT("Installed", "Installed {0} skill folder(s) ({1}) into the project's .claude/skills and .agents/skills folders. Open your AI agent in the project folder and type the skill name with a slash, for example /{2}."),
				FText::AsNumber(Result.Written.Num()), FText::FromString(NameList(Result.Written)), FText::FromString(Skills.IsEmpty() ? FString() : Skills[0].Name));
			return Report;
		}
		Report.Category = EAppMsgCategory::Warning;
		Report.Message = Result.Written.IsEmpty()
			? FText::Format(LOCTEXT("InstallFailed", "Some skills could not be installed: {0}"), FText::FromString(FString::Join(Result.Errors, TEXT("; "))))
			: FText::Format(LOCTEXT("InstallPartly", "Installed {0} skill folder(s) ({1}), but some skills could not be installed: {2}"),
				FText::AsNumber(Result.Written.Num()), FText::FromString(NameList(Result.Written)), FText::FromString(FString::Join(Result.Errors, TEXT("; "))));
		return Report;
	}

	FString GetProjectDir()
	{
		return GProjectDirOverride.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) : GProjectDirOverride;
	}

	void SetProjectDirOverrideForTests(const FString& ProjectDir)
	{
		GProjectDirOverride = ProjectDir;
	}

	bool RunInstallCommand()
	{
		const bool bAsk = !FApp::IsUnattended();
		const TArray<FSkillSource> Skills = FindSkills();
		if (Skills.IsEmpty())
		{
			UE_LOG(LogCKSessionsEditor, Warning, TEXT("Install AI Agent Skills: no CoopKit plugin ships skills."));
			if (bAsk)
			{
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoSkills", "No enabled CoopKit plugin ships AI agent skills."));
			}
			return false;
		}
		const FString ProjectDir = GetProjectDir();
		const TArray<FString> Existing = FindExisting(Skills, ProjectDir);
		if (bAsk && !Existing.IsEmpty())
		{
			const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
				LOCTEXT("AskReplace", "These skills are already installed in the project: {0}.\n\nInstalling replaces them with the versions that come with the plugins, and removes any change you made to their files. Replace them?"),
				FText::FromString(NameList(Existing))));
			if (Answer != EAppReturnType::Yes)
			{
				return false;
			}
		}
		const FInstallResult Result = Install(Skills, ProjectDir);
		for (const FString& Error : Result.Errors)
		{
			UE_LOG(LogCKSessionsEditor, Error, TEXT("Install AI Agent Skills: %s"), *Error);
		}
		// Success shows the information icon; the dialog's default category (Warning) is for failures (TASK-18.17).
		const FInstallReport Report = MakeInstallReport(Result, Skills);
		UE_LOG(LogCKSessionsEditor, Display, TEXT("Install AI Agent Skills: %s"), *Report.Message.ToString());
		if (bAsk)
		{
			FMessageDialog::Open(Report.Category, EAppMsgType::Ok, Report.Message);
		}
		return !Result.Written.IsEmpty() && Result.Errors.IsEmpty();
	}

	void RegisterMenu()
	{
		if (IsRunningCommandlet())
		{
			return;
		}
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			FToolMenuOwnerScoped OwnerScoped(MenuOwner);
			UToolMenu* Tools = UToolMenus::Get()->ExtendMenu(ToolsMenuName);
			FToolMenuSection& Section = Tools->FindOrAddSection(TEXT("CoopKit"));
			Section.AddSubMenu(TEXT("CoopKit"), LOCTEXT("MenuTitle", "CoopKit"), LOCTEXT("MenuTooltip", "CoopKit tools."), FNewToolMenuDelegate::CreateLambda([](UToolMenu* Menu)
			{
				FToolMenuSection& Entries = Menu->FindOrAddSection(NAME_None);
				Entries.AddMenuEntry(TEXT("CoopKitInstallAgentSkills"), LOCTEXT("InstallTitle", "Install AI Agent Skills"),
					LOCTEXT("InstallTooltip", "Copies the AI agent skills of every enabled CoopKit plugin into this project (.claude/skills and .agents/skills)."),
					FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() { RunInstallCommand(); })));
			}));
		}));
	}

	void UnregisterMenu()
	{
		if (UToolMenus* Menus = UToolMenus::TryGet())
		{
			Menus->UnregisterOwnerByName(MenuOwner);
		}
	}
}

#undef LOCTEXT_NAMESPACE
