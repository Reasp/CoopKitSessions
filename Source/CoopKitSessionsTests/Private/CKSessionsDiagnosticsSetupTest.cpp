// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "CKSessionsDemoPanelWidget.h"
#include "UObject/UObjectGlobals.h"
#include "CKSessionsDiagnostics.h"
#include "CKSessionsSetup.h"
#include "CKSessionsSubsystem.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SCKSessionsSetup.h"
#include "Widgets/SWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests::DiagnosticsSetupTest
{
	namespace
	{
		using namespace CKSessions::Diagnostics;

		const TCHAR* const SampleIni =
			TEXT("[/Script/EngineSettings.GeneralProjectSettings]\nProjectID=ABC\n\n")
			TEXT("[OnlineSubsystem]\nDefaultPlatformService=Null\nbHasVoiceEnabled=true\n\n")
			TEXT("[/Script/Engine.GameEngine]\n")
			TEXT("+NetDriverDefinitions=(DefName=\"GameNetDriver\",DriverClassName=\"OnlineSubsystemSteam.SteamNetDriver\",DriverClassNameFallback=\"OnlineSubsystemUtils.IpNetDriver\")\n")
			TEXT("+NetDriverDefinitions=(DefName=\"DemoNetDriver\",DriverClassName=\"/Script/Engine.DemoNetDriver\",DriverClassNameFallback=\"/Script/Engine.DemoNetDriver\")\n\n")
			TEXT("[OnlineSubsystemSteam]\nSteamDevAppId=1234\n\n")
			TEXT("[/Script/Foo.Bar]\nX=1\n");

		int32 CountLines(const FString& Text, const FString& Fragment)
		{
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines, false);
			int32 Count = 0;
			for (const FString& Line : Lines)
			{
				Count += Line.Contains(Fragment) ? 1 : 0;
			}
			return Count;
		}

		int32 IndexOfLine(const FString& Text, const FString& Fragment)
		{
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines, false);
			return Lines.IndexOfByPredicate([&Fragment](const FString& Line) { return Line.Contains(Fragment); });
		}

		bool HasTag(const TSharedRef<SWidget>& Root, const FName Tag)
		{
			if (Root->GetTag() == Tag)
			{
				return true;
			}
			bool bFound = false;
			Root->GetChildren()->ForEachWidget([&bFound, Tag](SWidget& Child) { bFound = bFound || HasTag(Child.AsShared(), Tag); });
			return bFound;
		}

		bool RunIniScenario(FAutomationTestBase& Test)
		{
			const FString GameLine = TEXT("DriverClassName=\"/Script/SteamSockets.SteamSocketsNetDriver\"");

			// Steam on 5.8 over a project that has the Steam driver of older guides and its own app id.
			const FString Steam = ApplySetupToIni(SampleIni, EService::Steam, 8);
			Test.TestTrue(TEXT("Steam: service"), Steam.Contains(TEXT("DefaultPlatformService=Steam")));
			Test.TestEqual(TEXT("Steam: one service line"), CountLines(Steam, TEXT("DefaultPlatformService=")), 1);
			Test.TestTrue(TEXT("Steam: subsystem enabled"), Steam.Contains(TEXT("bEnabled=true")));
			Test.TestTrue(TEXT("Steam: the project's app id stays"), Steam.Contains(TEXT("SteamDevAppId=1234")) && !Steam.Contains(TEXT("SteamDevAppId=480")));
			Test.TestEqual(TEXT("Steam: the old guide's driver is gone"), CountLines(Steam, TEXT("OnlineSubsystemSteam.SteamNetDriver")), 0);
			Test.TestEqual(TEXT("Steam: the new driver once"), CountLines(Steam, GameLine), 1);
			Test.TestEqual(TEXT("Steam: GameNetDriver lines: the IP entry removed and the Steam entry added"), CountLines(Steam, TEXT("DefName=\"GameNetDriver\"")), 2);
			Test.TestTrue(TEXT("Steam: the removal comes before the addition"), IndexOfLine(Steam, TEXT("-NetDriverDefinitions=")) < IndexOfLine(Steam, TEXT("+NetDriverDefinitions=(DefName=\"GameNetDriver\"")));
			Test.TestTrue(TEXT("Other lines stay"), Steam.Contains(TEXT("DemoNetDriver")) && Steam.Contains(TEXT("bHasVoiceEnabled=true")) && Steam.Contains(TEXT("[/Script/Foo.Bar]\nX=1")) && Steam.Contains(TEXT("ProjectID=ABC")));
			Test.TestEqual(TEXT("Steam: applying again changes nothing"), ApplySetupToIni(Steam, EService::Steam, 8), Steam);

			// EOS: the driver follows the engine version.
			const FString Eos55 = ApplySetupToIni(Steam, EService::Eos, 5);
			Test.TestTrue(TEXT("EOS 5.5: OnlineSubsystemEOS driver"), Eos55.Contains(TEXT("DriverClassName=\"/Script/OnlineSubsystemEOS.NetDriverEOS\"")));
			Test.TestEqual(TEXT("EOS 5.5: the Steam driver is gone"), CountLines(Eos55, TEXT("SteamSockets")), 0);
			Test.TestTrue(TEXT("EOS 5.5: service"), Eos55.Contains(TEXT("DefaultPlatformService=EOS")) && Eos55.Contains(TEXT("[OnlineSubsystemEOS]")));
			const FString Eos56 = ApplySetupToIni(Eos55, EService::Eos, 6);
			Test.TestTrue(TEXT("EOS 5.6: SocketSubsystemEOS driver"), Eos56.Contains(TEXT("DriverClassName=\"/Script/SocketSubsystemEOS.NetDriverEOS\"")) && !Eos56.Contains(TEXT("/Script/OnlineSubsystemEOS.NetDriverEOS")));
			Test.TestFalse(TEXT("EOS never writes product settings"), Eos56.Contains(TEXT("EOSSettings")) || Eos56.Contains(TEXT("ProductId")));

			// Null removes the overrides.
			const FString Null = ApplySetupToIni(Eos56, EService::Null, 8);
			Test.TestTrue(TEXT("Null: service"), Null.Contains(TEXT("DefaultPlatformService=Null")));
			Test.TestEqual(TEXT("Null: no GameNetDriver entry"), CountLines(Null, TEXT("DefName=\"GameNetDriver\"")), 0);
			Test.TestTrue(TEXT("Null: the demo driver stays"), Null.Contains(TEXT("DemoNetDriver")));

			// A project without DefaultEngine.ini, and line endings.
			const FString Fresh = ApplySetupToIni(FString(), EService::Steam, 4);
			Test.TestTrue(TEXT("Fresh: sections written"), Fresh.Contains(TEXT("[OnlineSubsystem]\nDefaultPlatformService=Steam")) && Fresh.Contains(TEXT("SteamDevAppId=480")) && Fresh.Contains(TEXT("[/Script/Engine.GameEngine]")));
			Test.TestEqual(TEXT("Fresh: applying again changes nothing"), ApplySetupToIni(Fresh, EService::Steam, 4), Fresh);
			const FString NullFresh = ApplySetupToIni(FString(), EService::Null, 4);
			Test.TestFalse(TEXT("Fresh Null: no net driver section"), NullFresh.Contains(TEXT("GameEngine")));
			const FString CrLf = ApplySetupToIni(FString(SampleIni).Replace(TEXT("\n"), TEXT("\r\n")), EService::Steam, 8);
			Test.TestTrue(TEXT("CRLF stays CRLF"), CrLf.Contains(TEXT("\r\n")) && !CrLf.Replace(TEXT("\r\n"), TEXT("")).Contains(TEXT("\n")));

			// The window writes a copy of the ini, never the project's file.
			const FString Dir = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("CKSessionsSetup"));
			const FString CopyPath = FPaths::Combine(Dir, TEXT("DefaultEngine.ini"));
			const FString MissingPath = FPaths::Combine(Dir, TEXT("Fresh"), TEXT("DefaultEngine.ini"));
			IFileManager::Get().Delete(*CopyPath);
			IFileManager::Get().Delete(*MissingPath);
			Test.TestTrue(TEXT("Sample ini copy written"), FFileHelper::SaveStringToFile(SampleIni, *CopyPath));
			Test.TestNotEqual(TEXT("The copy is not the project's file"), FPaths::ConvertRelativePathToFull(CopyPath), CKSessions::Setup::GetProjectEngineIniPath());

			const TSharedRef<SCKSessionsSetup> Window = SNew(SCKSessionsSetup).IniPath(CopyPath).bChangePlugins(false);
			for (const TCHAR* Tag : { TEXT("CoopKitSessionsSetup.Check"), TEXT("CoopKitSessionsSetup.Apply.Null"), TEXT("CoopKitSessionsSetup.Apply.Steam"), TEXT("CoopKitSessionsSetup.Apply.Eos") })
			{
				Test.TestTrue(FString::Printf(TEXT("Button %s is in the window"), Tag), HasTag(Window, FName(Tag)));
			}
			Window->Check();
			Test.TestEqual(TEXT("The window lists what Evaluate finds"), Window->GetFindings().Num(), Evaluate(CKSessions::Setup::GatherProjectSnapshot()).Num());

			Test.TestTrue(TEXT("Apply Steam"), Window->ApplySetup(EService::Steam));
			FString Written;
			Test.TestTrue(TEXT("The copy reads back"), FFileHelper::LoadFileToString(Written, *CopyPath));
			Test.TestEqual(TEXT("The copy has what ApplySetupToIni makes"), Written, ApplySetupToIni(SampleIni, EService::Steam, ENGINE_MINOR_VERSION));
			Test.TestTrue(TEXT("The report says to restart"), Window->GetReport().ToString().Contains(TEXT("Restart the editor")));
			Test.TestTrue(TEXT("Apply EOS"), Window->ApplySetup(EService::Eos));
			Test.TestTrue(TEXT("Apply Null"), Window->ApplySetup(EService::Null));
			FFileHelper::LoadFileToString(Written, *CopyPath);
			Test.TestTrue(TEXT("Back to Null"), Written.Contains(TEXT("DefaultPlatformService=Null")) && CountLines(Written, TEXT("DefName=\"GameNetDriver\"")) == 0);

			const TSharedRef<SCKSessionsSetup> NewFile = SNew(SCKSessionsSetup).IniPath(MissingPath).bChangePlugins(false);
			Test.TestTrue(TEXT("Apply creates a missing ini"), NewFile->ApplySetup(EService::Steam) && FPaths::FileExists(MissingPath));

			// A file that cannot be written is reported, not swallowed.
			const TSharedRef<SCKSessionsSetup> Broken = SNew(SCKSessionsSetup).IniPath(FPaths::Combine(Dir, TEXT("DefaultEngine.ini"), TEXT("Nope.ini"))).bChangePlugins(false);
			Test.TestFalse(TEXT("A path below a file fails"), Broken->ApplySetup(EService::Steam));
			Test.TestTrue(TEXT("and the report says so"), Broken->GetReport().ToString().Contains(TEXT("Not completed")));
			return true;
		}

		/** Makes a game-type world (what -game runs, not Play In Editor) with its own game instance and sessions subsystem. */
		UGameInstance* MakeGameInstance()
		{
			UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine, UGameInstance::StaticClass());
			GameInstance->AddToRoot();
			GameInstance->InitializeStandalone(TEXT("CKSessionsDiagnosticsWorld"));
			return GameInstance;
		}

		void DestroyGameInstance(UGameInstance* GameInstance)
		{
			UWorld* World = GameInstance->GetWorld();
			GameInstance->Shutdown();
			if (World != nullptr)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
			GameInstance->RemoveFromRoot();
		}

		bool RunGameWorldScenario(FAutomationTestBase& Test)
		{
			// The check logs an error for every finding that is an error; this test provokes one on purpose.
			Test.AddExpectedError(TEXT("Setup check:"), EAutomationExpectedErrorFlags::Contains, 0);

			UGameInstance* GameInstance = MakeGameInstance();
			UCKSessionsSubsystem* Sessions = GameInstance->GetSubsystem<UCKSessionsSubsystem>();
			if (!Test.TestNotNull(TEXT("The sessions subsystem of a game world"), Sessions))
			{
				DestroyGameInstance(GameInstance);
				return false;
			}
			UWorld* World = GameInstance->GetWorld();
			Test.TestTrue(TEXT("The world is a game world, not Play In Editor"), World != nullptr && World->WorldType == EWorldType::Game);

			// A healthy Null game: no finding of the driver.
			TArray<FCKSessionsFinding> Findings = Sessions->CheckSetup();
			Test.TestFalse(TEXT("A working driver is no finding"), Findings.ContainsByPredicate([](const FCKSessionsFinding& Finding) { return Finding.Code == ECKSessionsFindingCode::DriverNotLoadable || Finding.Code == ECKSessionsFindingCode::DriverWrong; }));

			// A class that does not exist for GameNetDriver, in front of the real definition (the first one counts).
			FNetDriverDefinition Wrong;
			Wrong.DefName = NAME_GameNetDriver;
			Wrong.DriverClassName = TEXT("/Script/CoopKitNoSuchModule.NoSuchNetDriver");
			Wrong.DriverClassNameFallback = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
			GEngine->NetDriverDefinitions.Insert(Wrong, 0);

			Findings = Sessions->CheckSetup();
			const FCKSessionsFinding* Found = Findings.FindByPredicate([](const FCKSessionsFinding& Finding) { return Finding.Code == ECKSessionsFindingCode::DriverNotLoadable; });
			Test.TestNotNull(TEXT("The unloadable GameNetDriver is found in a game world"), Found);
			Test.TestTrue(TEXT("It names the class and tells the fix"), Found != nullptr && Found->Message.Contains(TEXT("NoSuchNetDriver")) && !Found->Fix.IsEmpty());
			Test.TestEqual(TEXT("Get Setup Findings keeps the result"), Sessions->GetSetupFindings().Num(), Findings.Num());

			// The demo panel shows it as one line. Building its Slate widget constructs the panel, which refreshes it at once.
			const UClass* PanelClass = FindObject<UClass>(nullptr, TEXT("/Script/CoopKitSessions.CKSessionsDemoPanelWidget"));
			UUserWidget* Panel = PanelClass != nullptr ? CreateWidget<UUserWidget>(GameInstance, const_cast<UClass*>(PanelClass)) : nullptr;
			if (Test.TestNotNull(TEXT("The demo panel"), Panel))
			{
				Panel->TakeWidget();
				const UTextBlock* Line = Cast<UTextBlock>(Panel->GetWidgetFromName(FName(CKSessions::Demo::WidgetNames::SetupText)));
				Test.TestNotNull(TEXT("The panel has the setup line"), Line);
				Test.TestTrue(TEXT("The panel shows the problem"), Line != nullptr && Line->GetVisibility() != ESlateVisibility::Collapsed && Line->GetText().ToString().Contains(TEXT("Setup problem")));
				Panel->ReleaseSlateResources(true);
			}

			// Put the real definitions back: the finding is gone and the panel's line with it.
			GEngine->NetDriverDefinitions.RemoveAt(0);
			Findings = Sessions->CheckSetup();
			Test.TestFalse(TEXT("The finding is gone with the wrong driver"), Findings.ContainsByPredicate([](const FCKSessionsFinding& Finding) { return Finding.Code == ECKSessionsFindingCode::DriverNotLoadable; }));
			UUserWidget* Healthy = PanelClass != nullptr ? CreateWidget<UUserWidget>(GameInstance, const_cast<UClass*>(PanelClass)) : nullptr;
			if (Test.TestNotNull(TEXT("The demo panel again"), Healthy))
			{
				Healthy->TakeWidget();
				const UTextBlock* Line = Cast<UTextBlock>(Healthy->GetWidgetFromName(FName(CKSessions::Demo::WidgetNames::SetupText)));
				Test.TestTrue(TEXT("The panel's line is hidden again"), Line != nullptr && Line->GetVisibility() == ESlateVisibility::Collapsed);
				Healthy->ReleaseSlateResources(true);
			}

			DestroyGameInstance(GameInstance);
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsDiagnosticsIniTest, "CoopKit.Sessions.Diagnostics.ApplySetup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsDiagnosticsIniTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::DiagnosticsSetupTest::RunIniScenario(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsDiagnosticsGameWorldTest, "CoopKit.Sessions.Diagnostics.GameWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsDiagnosticsGameWorldTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::DiagnosticsSetupTest::RunGameWorldScenario(*this);
}

#endif // WITH_DEV_AUTOMATION_TESTS
