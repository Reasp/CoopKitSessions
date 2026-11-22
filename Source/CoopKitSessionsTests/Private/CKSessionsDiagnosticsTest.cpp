// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "Blueprint/UserWidget.h"
#include "CKSessionsDemoPanelWidget.h"
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
#include "SCKSessionsSetup.h"
#include "Widgets/SWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests::DiagnosticsTest
{
	namespace
	{
		using namespace CKSessions::Diagnostics;

		const TCHAR* const SteamOldDriver = TEXT("/Script/OnlineSubsystemSteam.SteamNetDriver");

		/** A snapshot of a healthy Steam project on the engine version: the check must find nothing in it. */
		FSnapshot MakeSteamSnapshot(const int32 Minor)
		{
			FSnapshot Snapshot;
			Snapshot.EngineMinor = Minor;
			Snapshot.DefaultPlatformService = TEXT("Steam");
			Snapshot.bLoadedSubsystemKnown = true;
			Snapshot.LoadedSubsystem = TEXT("STEAM");
			Snapshot.GameNetDriverClass = GetRecommendedDriver(EService::Steam, Minor);
			Snapshot.EnabledPlugins = GetRequiredPlugins(EService::Steam, Minor);
			Snapshot.bSteamAppIdFilePresent = true;
			Snapshot.bSessionUsesPresence = true;
			Snapshot.bSessionUsesLobbies = true;
			return Snapshot;
		}

		/** A snapshot of a healthy EOS project on the engine version. */
		FSnapshot MakeEosSnapshot(const int32 Minor)
		{
			FSnapshot Snapshot;
			Snapshot.EngineMinor = Minor;
			Snapshot.DefaultPlatformService = TEXT("EOS");
			Snapshot.bLoadedSubsystemKnown = true;
			Snapshot.LoadedSubsystem = TEXT("EOS");
			Snapshot.GameNetDriverClass = GetRecommendedDriver(EService::Eos, Minor);
			Snapshot.EnabledPlugins = GetRequiredPlugins(EService::Eos, Minor);
			Snapshot.bEosProductConfigured = true;
			return Snapshot;
		}

		const FCKSessionsFinding* FindCode(const TArray<FCKSessionsFinding>& Findings, const ECKSessionsFindingCode Code)
		{
			return Findings.FindByPredicate([Code](const FCKSessionsFinding& Finding) { return Finding.Code == Code; });
		}

		FString DescribeFindings(const TArray<FCKSessionsFinding>& Findings)
		{
			TArray<FString> Lines;
			for (const FCKSessionsFinding& Finding : Findings)
			{
				Lines.Add(FormatFinding(Finding));
			}
			return FString::Join(Lines, TEXT(" | "));
		}

		void ExpectCode(FAutomationTestBase& Test, const FString& What, const TArray<FCKSessionsFinding>& Findings, const ECKSessionsFindingCode Code, const ECKSessionsFindingSeverity Severity)
		{
			const FCKSessionsFinding* Finding = FindCode(Findings, Code);
			Test.TestNotNull(What + TEXT(": finding present"), Finding);
			if (Finding != nullptr)
			{
				Test.TestEqual(What + TEXT(": severity"), static_cast<int32>(Finding->Severity), static_cast<int32>(Severity));
				Test.TestFalse(What + TEXT(": has a message"), Finding->Message.IsEmpty());
				Test.TestEqual(What + TEXT(": has a fix unless it is a note"), Finding->Fix.IsEmpty(), Severity == ECKSessionsFindingSeverity::Info);
			}
		}

		void ExpectNoCode(FAutomationTestBase& Test, const FString& What, const TArray<FCKSessionsFinding>& Findings, const ECKSessionsFindingCode Code)
		{
			Test.TestNull(What + TEXT(": no such finding"), FindCode(Findings, Code));
		}

		/** Test.TestEqual for string arrays. */
		bool TestArrayEquals(FAutomationTestBase& Test, const FString& What, const TArray<FString>& Actual, const TArray<FString>& Expected)
		{
			return Test.TestEqual(What, FString::Join(Actual, TEXT(",")), FString::Join(Expected, TEXT(",")));
		}

		bool RunEvaluateScenario(FAutomationTestBase& Test)
		{
			// The recommended driver and plugins per engine version, as read from the UE 5.4-5.8 sources.
			Test.TestEqual(TEXT("Steam driver"), GetRecommendedDriver(EService::Steam, 4), FString(TEXT("/Script/SteamSockets.SteamSocketsNetDriver")));
			Test.TestEqual(TEXT("EOS driver 5.5"), GetRecommendedDriver(EService::Eos, 5), FString(TEXT("/Script/OnlineSubsystemEOS.NetDriverEOS")));
			Test.TestEqual(TEXT("EOS driver 5.6"), GetRecommendedDriver(EService::Eos, 6), FString(TEXT("/Script/SocketSubsystemEOS.NetDriverEOS")));
			Test.TestEqual(TEXT("Null driver"), GetRecommendedDriver(EService::Null, 8), FString(TEXT("/Script/OnlineSubsystemUtils.IpNetDriver")));
			Test.TestTrue(TEXT("Service names are not case sensitive"), ParseService(TEXT(" steam ")) == EService::Steam && ParseService(TEXT("eos")) == EService::Eos
				&& ParseService(TEXT("")) == EService::Null && ParseService(TEXT("NULL")) == EService::Null && ParseService(TEXT("GDK")) == EService::Other);
			Test.TestTrue(TEXT("The old EOS class is the new one from 5.6"), IsSameDriverClass(TEXT("OnlineSubsystemEOS.NetDriverEOS"), TEXT("/Script/SocketSubsystemEOS.NetDriverEOS"), 6));
			Test.TestFalse(TEXT("But not in 5.5"), IsSameDriverClass(TEXT("/Script/SocketSubsystemEOS.NetDriverEOS"), TEXT("/Script/OnlineSubsystemEOS.NetDriverEOS"), 5));

			for (int32 Minor = 4; Minor <= 8; ++Minor)
			{
				const FString Engine = FString::Printf(TEXT("5.%d"), Minor);

				// Healthy projects have no findings.
				{
					const TArray<FCKSessionsFinding> Findings = Evaluate(MakeSteamSnapshot(Minor));
					Test.TestTrue(Engine + TEXT(" healthy Steam: nothing found, got ") + DescribeFindings(Findings), Findings.IsEmpty());
					const TArray<FCKSessionsFinding> Eos = Evaluate(MakeEosSnapshot(Minor));
					Test.TestTrue(Engine + TEXT(" healthy EOS: nothing found, got ") + DescribeFindings(Eos), Eos.IsEmpty());
					Test.TestTrue(Engine + TEXT(" the defaults are a healthy Null project"), Evaluate(FSnapshot()).IsEmpty());
				}

				// The Steam driver of older guides loads in the snapshot, but is not the Steam Sockets driver.
				{
					FSnapshot Snapshot = MakeSteamSnapshot(Minor);
					Snapshot.GameNetDriverClass = SteamOldDriver;
					ExpectCode(Test, Engine + TEXT(" old Steam driver"), Evaluate(Snapshot), ECKSessionsFindingCode::DriverWrong, ECKSessionsFindingSeverity::Error);
					Snapshot.GameNetDriverClass = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
					ExpectCode(Test, Engine + TEXT(" IP driver with Steam"), Evaluate(Snapshot), ECKSessionsFindingCode::DriverWrong, ECKSessionsFindingSeverity::Error);
					Snapshot.GameNetDriverClass.Empty();
					ExpectCode(Test, Engine + TEXT(" no GameNetDriver with Steam"), Evaluate(Snapshot), ECKSessionsFindingCode::DriverWrong, ECKSessionsFindingSeverity::Error);
				}

				// A driver that does not load, and the plugins that would hold it.
				{
					FSnapshot Snapshot = MakeSteamSnapshot(Minor);
					Snapshot.bGameNetDriverLoads = false;
					Snapshot.EnabledPlugins.Empty();
					const TArray<FCKSessionsFinding> Findings = Evaluate(Snapshot);
					ExpectCode(Test, Engine + TEXT(" Steam driver not loadable"), Findings, ECKSessionsFindingCode::DriverNotLoadable, ECKSessionsFindingSeverity::Error);
					ExpectCode(Test, Engine + TEXT(" Steam plugins off"), Findings, ECKSessionsFindingCode::PluginNotEnabled, ECKSessionsFindingSeverity::Error);
					ExpectNoCode(Test, Engine + TEXT(" an unloadable driver is not also the wrong one"), Findings, ECKSessionsFindingCode::DriverWrong);
				}

				// Null with a driver of another service that does not load.
				{
					FSnapshot Snapshot;
					Snapshot.EngineMinor = Minor;
					Snapshot.GameNetDriverClass = GetRecommendedDriver(EService::Steam, Minor);
					Snapshot.bGameNetDriverLoads = false;
					const TArray<FCKSessionsFinding> Findings = Evaluate(Snapshot);
					ExpectCode(Test, Engine + TEXT(" Null with a dead driver"), Findings, ECKSessionsFindingCode::DriverNotLoadable, ECKSessionsFindingSeverity::Error);
					ExpectNoCode(Test, Engine + TEXT(" Null needs no plugins"), Findings, ECKSessionsFindingCode::PluginNotEnabled);
				}

				// The engine fell back to Null although Steam or EOS is configured.
				{
					FSnapshot Steam = MakeSteamSnapshot(Minor);
					Steam.LoadedSubsystem = TEXT("NULL");
					ExpectCode(Test, Engine + TEXT(" Steam fell back"), Evaluate(Steam), ECKSessionsFindingCode::SubsystemFellBack, ECKSessionsFindingSeverity::Error);
					Steam.bPlayInEditor = true;
					Steam.bEditor = true;
					const TArray<FCKSessionsFinding> InEditor = Evaluate(Steam);
					ExpectCode(Test, Engine + TEXT(" Steam in Play In Editor"), InEditor, ECKSessionsFindingCode::SteamNotInEditor, ECKSessionsFindingSeverity::Info);
					ExpectNoCode(Test, Engine + TEXT(" Steam in Play In Editor is no fall back"), InEditor, ECKSessionsFindingCode::SubsystemFellBack);

					FSnapshot Eos = MakeEosSnapshot(Minor);
					Eos.LoadedSubsystem = TEXT("NULL");
					Eos.bPlayInEditor = true;
					ExpectCode(Test, Engine + TEXT(" EOS fell back, also in the editor"), Evaluate(Eos), ECKSessionsFindingCode::SubsystemFellBack, ECKSessionsFindingSeverity::Error);

					FSnapshot None = MakeSteamSnapshot(Minor);
					None.LoadedSubsystem = NAME_None;
					ExpectCode(Test, Engine + TEXT(" no subsystem"), Evaluate(None), ECKSessionsFindingCode::NoOnlineSubsystem, ECKSessionsFindingSeverity::Error);

					FSnapshot Unknown = MakeSteamSnapshot(Minor);
					Unknown.bLoadedSubsystemKnown = false;
					Unknown.LoadedSubsystem = NAME_None;
					Test.TestTrue(Engine + TEXT(" an unknown subsystem is not a finding"), Evaluate(Unknown).IsEmpty());
				}

				// steam_appid.txt: only a packaged build that is not Shipping needs it.
				{
					FSnapshot Snapshot = MakeSteamSnapshot(Minor);
					Snapshot.bSteamAppIdFilePresent = false;
					ExpectCode(Test, Engine + TEXT(" packaged Development without steam_appid.txt"), Evaluate(Snapshot), ECKSessionsFindingCode::SteamAppIdMissing, ECKSessionsFindingSeverity::Error);
					Snapshot.bShippingBuild = true;
					ExpectNoCode(Test, Engine + TEXT(" Shipping"), Evaluate(Snapshot), ECKSessionsFindingCode::SteamAppIdMissing);
					Snapshot.bShippingBuild = false;
					Snapshot.bEditor = true;
					ExpectNoCode(Test, Engine + TEXT(" editor"), Evaluate(Snapshot), ECKSessionsFindingCode::SteamAppIdMissing);
					Snapshot.bEditor = false;
					Snapshot.bDedicatedServer = true;
					ExpectNoCode(Test, Engine + TEXT(" dedicated server"), Evaluate(Snapshot), ECKSessionsFindingCode::SteamAppIdMissing);
				}

				// Presence and lobbies must agree: Steam refuses a mismatch in 5.8 and accepts it before.
				{
					FSnapshot Snapshot = MakeSteamSnapshot(Minor);
					Snapshot.bSessionUsesLobbies = false;
					ExpectCode(Test, Engine + TEXT(" session flags differ"), Evaluate(Snapshot), ECKSessionsFindingCode::SessionFlagsMismatch,
						Minor >= 8 ? ECKSessionsFindingSeverity::Error : ECKSessionsFindingSeverity::Warning);
					Snapshot.bSessionUsesPresence = false;
					ExpectNoCode(Test, Engine + TEXT(" both flags off"), Evaluate(Snapshot), ECKSessionsFindingCode::SessionFlagsMismatch);
				}

				// EOS: the driver of the engine version, the alias, the product settings.
				{
					FSnapshot Eos = MakeEosSnapshot(Minor);
					Eos.bEosProductConfigured = false;
					ExpectCode(Test, Engine + TEXT(" EOS without product settings"), Evaluate(Eos), ECKSessionsFindingCode::EosProductMissing, ECKSessionsFindingSeverity::Error);

					FSnapshot Moved = MakeEosSnapshot(Minor);
					Moved.GameNetDriverClass = Minor >= 6 ? TEXT("/Script/OnlineSubsystemEOS.NetDriverEOS") : TEXT("/Script/SocketSubsystemEOS.NetDriverEOS");
					const TArray<FCKSessionsFinding> Findings = Evaluate(Moved);
					if (Minor >= 6)
					{
						Test.TestTrue(Engine + TEXT(" the old EOS class is redirected, so it is fine: ") + DescribeFindings(Findings), Findings.IsEmpty());
					}
					else
					{
						ExpectCode(Test, Engine + TEXT(" the 5.6 EOS class does not exist yet"), Findings, ECKSessionsFindingCode::DriverWrong, ECKSessionsFindingSeverity::Error);
					}

					FSnapshot Plugins = MakeEosSnapshot(Minor);
					Plugins.EnabledPlugins.Empty();
					ExpectCode(Test, Engine + TEXT(" EOS plugin off"), Evaluate(Plugins), ECKSessionsFindingCode::PluginNotEnabled, ECKSessionsFindingSeverity::Error);
				}

				// Maps that a packaged build lacks.
				{
					FSnapshot Snapshot = MakeSteamSnapshot(Minor);
					Snapshot.MissingMaps = { TEXT("/Game/Maps/L_Menu"), TEXT("/Game/Maps/L_Match") };
					const TArray<FCKSessionsFinding> Findings = Evaluate(Snapshot);
					ExpectCode(Test, Engine + TEXT(" maps missing"), Findings, ECKSessionsFindingCode::MapNotPackaged, ECKSessionsFindingSeverity::Error);
					const FCKSessionsFinding* Maps = FindCode(Findings, ECKSessionsFindingCode::MapNotPackaged);
					Test.TestTrue(Engine + TEXT(" the finding names both maps"), Maps != nullptr && Maps->Message.Contains(TEXT("L_Menu")) && Maps->Message.Contains(TEXT("L_Match")));
				}
			}

			// Most serious first.
			{
				FSnapshot Snapshot = MakeSteamSnapshot(8);
				Snapshot.LoadedSubsystem = TEXT("NULL");
				Snapshot.bPlayInEditor = true;
				Snapshot.bEditor = true;
				Snapshot.MissingMaps = { TEXT("/Game/Maps/L_Menu") };
				const TArray<FCKSessionsFinding> Findings = Evaluate(Snapshot);
				Test.TestEqual(TEXT("Two findings"), Findings.Num(), 2);
				Test.TestTrue(TEXT("The error comes before the note"), Findings.Num() == 2 && Findings[0].Severity == ECKSessionsFindingSeverity::Error && Findings[1].Severity == ECKSessionsFindingSeverity::Info);
			}

			// EOS product settings as the engine stores them.
			const FString Artifact = TEXT("(ArtifactName=\"Game\",ClientId=\"xyz1\",ClientSecret=\"s\",ProductId=\"p1\",SandboxId=\"s1\",DeploymentId=\"d1\",ClientEncryptionKey=\"\")");
			Test.TestTrue(TEXT("EOS product complete"), IsEosProductConfigured(TEXT("Game"), { Artifact }));
			Test.TestFalse(TEXT("EOS no default artifact"), IsEosProductConfigured(TEXT(""), { Artifact }));
			Test.TestFalse(TEXT("EOS another artifact"), IsEosProductConfigured(TEXT("Other"), { Artifact }));
			Test.TestFalse(TEXT("EOS empty deployment"), IsEosProductConfigured(TEXT("Game"), { Artifact.Replace(TEXT("DeploymentId=\"d1\""), TEXT("DeploymentId=\"\"")) }));
			Test.TestFalse(TEXT("EOS no artifacts"), IsEosProductConfigured(TEXT("Game"), {}));

			// Maps of the packaged build.
			const TArray<FString> Required = { TEXT("/Game/Maps/L_Menu"), TEXT("/Game/Maps/L_Match"), TEXT("/Game/Extra/L_Other") };
			TestArrayEquals(Test, TEXT("Nothing listed"), FindMapsNotCooked(Required, {}, {}, false, {}), Required);
			TestArrayEquals(Test, TEXT("Cook all"), FindMapsNotCooked(Required, {}, {}, true, {}), {});
			TestArrayEquals(Test, TEXT("Listed, folder and automatic"),
				FindMapsNotCooked(Required, { TEXT("/Game/Maps/L_Menu.L_Menu") }, { TEXT("/Game/Extra/") }, false, { TEXT("/game/maps/l_match") }), {});
			TestArrayEquals(Test, TEXT("A folder is not a prefix of a name"), FindMapsNotCooked({ TEXT("/Game/Maps2/L_X") }, {}, { TEXT("/Game/Maps") }, false, {}), { TEXT("/Game/Maps2/L_X") });
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsDiagnosticsEvaluateTest, "CoopKit.Sessions.Diagnostics.Evaluate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsDiagnosticsEvaluateTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::DiagnosticsTest::RunEvaluateScenario(*this);
}

#endif // WITH_DEV_AUTOMATION_TESTS
