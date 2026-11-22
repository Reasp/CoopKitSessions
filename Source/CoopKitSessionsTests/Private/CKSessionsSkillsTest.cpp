// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsSkills.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "SCKSessionsHub.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		/** A project folder that lives for one test and is deleted with it. */
		struct FSkillsTestProject
		{
			FSkillsTestProject()
				: Dir(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("CoopKitSessionsTests"), TEXT("SkillsProject_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))))
			{
				IFileManager::Get().MakeDirectory(*Dir, true);
			}

			~FSkillsTestProject()
			{
				CKSessions::Skills::SetProjectDirOverrideForTests(FString());
				IFileManager::Get().DeleteDirectory(*Dir, false, true);
			}

			FString Dir;
		};

		/** Files below a folder, relative paths, sorted. */
		TArray<FString> SkillsTestListFiles(const FString& Folder)
		{
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*"), true, false);
			FString Base = Folder;
			FPaths::NormalizeFilename(Base);
			for (FString& File : Files)
			{
				FPaths::NormalizeFilename(File);
				File = File.RightChop(Base.Len() + 1);
			}
			Files.Sort();
			return Files;
		}

		/** SButton::ExecuteOnClick is protected in UE 5.4; the member pointer taken in a derived class reaches it (no instance is made). */
		struct FSkillsTestButtonAccess : public SButton
		{
			static FReply Click(SButton& Button)
			{
				FReply (SButton::*Method)() = &FSkillsTestButtonAccess::ExecuteOnClick;
				return (Button.*Method)();
			}
		};

		bool SkillsTestHasLabel(const TSharedRef<SWidget>& Root, const FText& Label)
		{
			if (Root->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Root)->GetText().EqualTo(Label))
			{
				return true;
			}
			bool bFound = false;
			Root->GetChildren()->ForEachWidget([&bFound, &Label](SWidget& Child)
			{
				bFound = bFound || SkillsTestHasLabel(Child.AsShared(), Label);
			});
			return bFound;
		}

		TSharedPtr<SButton> SkillsTestFindButton(const TSharedRef<SWidget>& Root, const FText& Label)
		{
			if (Root->GetType() == TEXT("SButton") && SkillsTestHasLabel(Root, Label))
			{
				return StaticCastSharedRef<SButton>(Root);
			}
			TSharedPtr<SButton> Found;
			Root->GetChildren()->ForEachWidget([&Found, &Label](SWidget& Child)
			{
				if (!Found.IsValid())
				{
					Found = SkillsTestFindButton(Child.AsShared(), Label);
				}
			});
			return Found;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsSkillsInstallTest, "CoopKit.Sessions.Skills.Install",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsSkillsInstallTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Skills;

	const TArray<FSkillSource> Skills = FindSkills();
	const FSkillSource* Own = Skills.FindByPredicate([](const FSkillSource& Skill) { return Skill.Name == TEXT("coopkit-sessions"); });
	if (!TestNotNull(TEXT("The enabled CoopKitSessions plugin ships the skill coopkit-sessions"), Own))
	{
		return false;
	}
	TestEqual(TEXT("The skill belongs to CoopKitSessions"), Own->Plugin, FString(TEXT("CoopKitSessions")));
	TestEqual(TEXT("The plugin folder spells the name with underscores (Fab allows no hyphen in file names)"), FPaths::GetCleanFilename(Own->Directory),
		Own->Name.Replace(TEXT("-"), TEXT("_")));

	// The skill is a folder in the open Agent Skills format: SKILL.md with the name of the installed folder and its
	// references, Markdown only. Fab accepts no executables or shell scripts in a package (Technical Requirements 2.2), so
	// the build commands are text in SKILL.md.
	FString SkillText;
	TestTrue(TEXT("SKILL.md loads"), FFileHelper::LoadFileToString(SkillText, *FPaths::Combine(Own->Directory, TEXT("SKILL.md"))));
	// A Windows checkout (core.autocrlf) gives CRLF line ends; the frontmatter is read by lines either way.
	SkillText.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	TestTrue(TEXT("SKILL.md starts with frontmatter"), SkillText.StartsWith(TEXT("---")));
	TestTrue(TEXT("The frontmatter name equals the folder name"), SkillText.Contains(TEXT("\nname: coopkit-sessions\n")));
	TestTrue(TEXT("The frontmatter has a description"), SkillText.Contains(TEXT("\ndescription: ")));
	TestTrue(TEXT("SKILL.md has the build commands as text: the engine's Build.bat, no script file"),
		SkillText.Contains(TEXT("coopkit-build:begin")) && SkillText.Contains(TEXT("Build.bat")) && !SkillText.Contains(TEXT("scripts/build")));
	const TArray<FString> SourceFiles = CKSessions::Tests::SkillsTestListFiles(Own->Directory);
	for (const TCHAR* Required : { TEXT("SKILL.md"), TEXT("reference/extension_map.md"), TEXT("reference/rules.md"), TEXT("reference/examples.md"),
		TEXT("reference/build_and_diagnose.md") })
	{
		TestTrue(*FString::Printf(TEXT("The skill folder contains %s"), Required), SourceFiles.Contains(Required));
	}
	for (const FString& File : SourceFiles)
	{
		TestTrue(*FString::Printf(TEXT("%s is a Markdown file (no scripts or binaries in a Fab package)"), *File), File.EndsWith(TEXT(".md")));
	}

	CKSessions::Tests::FSkillsTestProject Project;
	TestEqual(TEXT("A fresh project has no installed skills"), FindExisting(Skills, Project.Dir).Num(), 0);

	const FInstallResult First = Install(Skills, Project.Dir);
	TestEqual(TEXT("No errors on the first installation"), First.Errors.Num(), 0);
	TestEqual(TEXT("Every skill is written to both targets"), First.Written.Num(), Skills.Num() * 2);
	TestEqual(TEXT("Nothing was replaced on a fresh project"), First.Replaced.Num(), 0);
	const TArray<FString> Roots = GetTargetRoots(Project.Dir);
	if (TestEqual(TEXT("Two targets: Claude Code and the shared agents folder"), Roots.Num(), 2))
	{
		TestTrue(TEXT("Target one is .claude/skills"), Roots[0].EndsWith(TEXT(".claude/skills")));
		TestTrue(TEXT("Target two is .agents/skills"), Roots[1].EndsWith(TEXT(".agents/skills")));
		for (const FString& Root : Roots)
		{
			TestEqual(TEXT("The installed copy has the same files as the plugin's skill"), CKSessions::Tests::SkillsTestListFiles(FPaths::Combine(Root, Own->Name)), SourceFiles);
		}
	}

	// Install again: existing folders are found, replaced, and a stale file of the old copy is gone.
	const FString Stale = FPaths::Combine(Roots[0], Own->Name, TEXT("stale-note.txt"));
	FFileHelper::SaveStringToFile(TEXT("old"), *Stale);
	TestEqual(TEXT("The existing skill folders are found"), FindExisting(Skills, Project.Dir).Num(), Skills.Num() * 2);
	const FInstallResult Second = Install(Skills, Project.Dir);
	TestEqual(TEXT("No errors on the second installation"), Second.Errors.Num(), 0);
	TestEqual(TEXT("The second installation replaces every folder"), Second.Replaced.Num(), Skills.Num() * 2);
	TestFalse(TEXT("A stale file of the old copy is removed"), IFileManager::Get().FileExists(*Stale));
	TestEqual(TEXT("The reinstalled copy has the same files as the plugin's skill"), CKSessions::Tests::SkillsTestListFiles(FPaths::Combine(Roots[0], Own->Name)), SourceFiles);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsSkillsButtonTest, "CoopKit.Sessions.Skills.Button",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsSkillsButtonTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slate is not initialized (commandlet run): the hub window cannot be built."));
		return true;
	}
	const TSharedRef<SCKSessionsHub> Hub = SNew(SCKSessionsHub);
	const TSharedPtr<SButton> Button = CKSessions::Tests::SkillsTestFindButton(Hub, NSLOCTEXT("SCKSessionsHub", "InstallSkills", "Install AI Agent Skills"));
	if (!TestTrue(TEXT("The hub has the Install AI Agent Skills button"), Button.IsValid()))
	{
		return false;
	}
	if (!TestTrue(TEXT("The test runs unattended, so the command asks nothing"), FApp::IsUnattended()))
	{
		return false;
	}

	CKSessions::Tests::FSkillsTestProject Project;
	CKSessions::Skills::SetProjectDirOverrideForTests(Project.Dir);
	CKSessions::Tests::FSkillsTestButtonAccess::Click(*Button);
	for (const FString& Root : CKSessions::Skills::GetTargetRoots(Project.Dir))
	{
		TestTrue(*FString::Printf(TEXT("The button installed the skill into %s"), *Root),
			IFileManager::Get().FileExists(*FPaths::Combine(Root, TEXT("coopkit-sessions"), TEXT("SKILL.md"))));
	}

	// A second click replaces without a dialog in an unattended run.
	const FString Marker = FPaths::Combine(Project.Dir, TEXT(".claude"), TEXT("skills"), TEXT("coopkit-sessions"), TEXT("edited-by-user.txt"));
	FFileHelper::SaveStringToFile(TEXT("edit"), *Marker);
	TestTrue(TEXT("The command reports success"), CKSessions::Skills::RunInstallCommand());
	TestFalse(TEXT("The second run replaced the folder"), IFileManager::Get().FileExists(*Marker));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsSkillsReportTest, "CoopKit.Sessions.Skills.Report",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsSkillsReportTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Skills;

	// The dialog after Install AI Agent Skills: success is information, any error a warning (TASK-18.17).
	const TArray<FSkillSource> Skills = { { TEXT("CoopKitTest"), TEXT("coopkit-sessions"), TEXT("C:/Skills/coopkit-sessions") } };
	FInstallResult Success;
	Success.Written = { TEXT("C:/Project/.claude/skills/coopkit-sessions"), TEXT("C:/Project/.agents/skills/coopkit-sessions") };
	const FInstallReport Done = MakeInstallReport(Success, Skills);
	TestTrue(TEXT("Success shows the information icon"), Done.Category == EAppMsgCategory::Info);
	TestTrue(TEXT("Success says how many folders were installed"), Done.Message.ToString().StartsWith(TEXT("Installed 2 skill folder(s)")));
	TestTrue(TEXT("Success names a skill to call"), Done.Message.ToString().Contains(TEXT("/coopkit-sessions")));

	FInstallResult Partly = Success;
	Partly.Written.SetNum(1);
	Partly.Errors = { TEXT("Could not copy C:/Skills/coopkit-sessions to C:/Project/.agents/skills/coopkit-sessions") };
	const FInstallReport Part = MakeInstallReport(Partly, Skills);
	TestTrue(TEXT("A partial installation shows the warning icon"), Part.Category == EAppMsgCategory::Warning);
	TestTrue(TEXT("A partial installation names what was written and what failed"), Part.Message.ToString().Contains(TEXT("Installed 1 skill folder(s)")) && Part.Message.ToString().Contains(Partly.Errors[0]));

	FInstallResult Failed;
	Failed.Errors = Partly.Errors;
	const FInstallReport Fail = MakeInstallReport(Failed, Skills);
	TestTrue(TEXT("A failed installation shows the warning icon"), Fail.Category == EAppMsgCategory::Warning);
	TestTrue(TEXT("A failed installation lists the errors"), Fail.Message.ToString().StartsWith(TEXT("Some skills could not be installed")) && Fail.Message.ToString().Contains(Partly.Errors[0]));

	// The real installation into a test project reports success with the information icon.
	CKSessions::Tests::FSkillsTestProject Project;
	const TArray<FSkillSource> Found = FindSkills();
	const FInstallReport Real = MakeInstallReport(Install(Found, Project.Dir), Found);
	TestTrue(TEXT("Installing the enabled plugins' skills is a success with the information icon"), Real.Category == EAppMsgCategory::Info);
	return true;
}

#endif
