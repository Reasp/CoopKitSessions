// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsDiagnostics.h"
#include "CKSessionsLobbyRulesData.h"
#include "CKSessionsOnlineSettings.h"
#include "CKSessionsProfileData.h"
#include "CKSessionsSettings.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/UObjectGlobals.h"

namespace CKSessions::Diagnostics
{
	namespace
	{
		const TCHAR* const IpDriver = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
		const TCHAR* const SteamDriver = TEXT("/Script/SteamSockets.SteamSocketsNetDriver");
		const TCHAR* const EosDriverOld = TEXT("/Script/OnlineSubsystemEOS.NetDriverEOS");
		const TCHAR* const EosDriverNew = TEXT("/Script/SocketSubsystemEOS.NetDriverEOS");
		const TCHAR* const NetDriverSection = TEXT("/Script/Engine.GameEngine");

		FString GetServiceLabel(const EService Service)
		{
			switch (Service)
			{
			case EService::Steam:
				return TEXT("Steam");
			case EService::Eos:
				return TEXT("EOS");
			case EService::Null:
				return TEXT("Null");
			default:
				return TEXT("the configured service");
			}
		}

		/** "/Script/Module.Class" and "Module.Class" are the same class: drop the prefix, compare without case. */
		FString NormalizeClassPath(const FString& Path)
		{
			FString Normalized = Path.TrimStartAndEnd();
			Normalized.RemoveFromStart(TEXT("/Script/"), ESearchCase::IgnoreCase);
			return Normalized.ToLower();
		}

		FString EngineLabel(const int32 EngineMinor)
		{
			return FString::Printf(TEXT("5.%d"), EngineMinor);
		}

		bool IsPluginEnabled(const FSnapshot& Snapshot, const FString& Name)
		{
			return Snapshot.EnabledPlugins.ContainsByPredicate([&Name](const FString& Enabled) { return Enabled.Equals(Name, ESearchCase::IgnoreCase); });
		}

		void AddFinding(TArray<FCKSessionsFinding>& Findings, const ECKSessionsFindingCode Code, const ECKSessionsFindingSeverity Severity, const FString& Message, const FString& Fix)
		{
			FCKSessionsFinding& Finding = Findings.AddDefaulted_GetRef();
			Finding.Code = Code;
			Finding.Severity = Severity;
			Finding.Message = Message;
			Finding.Fix = Fix;
		}

		/** Value of Key in a struct text such as (A="x",B=y); empty when absent. */
		FString ReadStructField(const FString& Text, const TCHAR* Key)
		{
			const FString Needle = FString(Key) + TEXT("=");
			int32 From = 0;
			while (true)
			{
				const int32 At = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
				if (At == INDEX_NONE)
				{
					return FString();
				}
				From = At + Needle.Len();
				if (At > 0 && Text[At - 1] != TEXT('(') && Text[At - 1] != TEXT(','))
				{
					continue;
				}
				if (From < Text.Len() && Text[From] == TEXT('"'))
				{
					const int32 Close = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, From + 1);
					return Close == INDEX_NONE ? Text.Mid(From + 1) : Text.Mid(From + 1, Close - From - 1);
				}
				int32 End = From;
				while (End < Text.Len() && Text[End] != TEXT(',') && Text[End] != TEXT(')'))
				{
					++End;
				}
				return Text.Mid(From, End - From).TrimStartAndEnd();
			}
		}

		FString NormalizeMapPath(const FString& Path)
		{
			FString Normalized = Path.TrimStartAndEnd();
			int32 Dot = INDEX_NONE;
			if (Normalized.FindChar(TEXT('.'), Dot))
			{
				Normalized.LeftInline(Dot);
			}
			return Normalized.ToLower();
		}

		// ---- DefaultEngine.ini text editing ----

		int32 FindSection(const TArray<FString>& Lines, const FString& Section)
		{
			const FString Header = FString::Printf(TEXT("[%s]"), *Section);
			for (int32 Index = 0; Index < Lines.Num(); ++Index)
			{
				if (Lines[Index].TrimStartAndEnd().Equals(Header, ESearchCase::IgnoreCase))
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}

		/** Index after the last line with content of the section that starts at HeaderIndex: where a new line goes. */
		int32 FindSectionEnd(const TArray<FString>& Lines, const int32 HeaderIndex)
		{
			int32 End = HeaderIndex + 1;
			for (int32 Index = HeaderIndex + 1; Index < Lines.Num() && !Lines[Index].TrimStart().StartsWith(TEXT("[")); ++Index)
			{
				if (!Lines[Index].TrimStartAndEnd().IsEmpty())
				{
					End = Index + 1;
				}
			}
			return End;
		}

		int32 EnsureSection(TArray<FString>& Lines, const FString& Section)
		{
			const int32 Existing = FindSection(Lines, Section);
			if (Existing != INDEX_NONE)
			{
				return Existing;
			}
			if (!Lines.IsEmpty() && !Lines.Last().TrimStartAndEnd().IsEmpty())
			{
				Lines.Add(FString());
			}
			return Lines.Add(FString::Printf(TEXT("[%s]"), *Section));
		}

		bool IsKeyLine(const FString& Line, const FString& Key)
		{
			const FString Trimmed = Line.TrimStart();
			return Trimmed.StartsWith(Key + TEXT("="), ESearchCase::IgnoreCase) || Trimmed.StartsWith(Key + TEXT(" ="), ESearchCase::IgnoreCase);
		}

		void SetKey(TArray<FString>& Lines, const FString& Section, const FString& Key, const FString& Value, const bool bOnlyIfMissing)
		{
			const int32 Header = EnsureSection(Lines, Section);
			const int32 End = FindSectionEnd(Lines, Header);
			for (int32 Index = Header + 1; Index < End; ++Index)
			{
				if (IsKeyLine(Lines[Index], Key))
				{
					if (!bOnlyIfMissing)
					{
						Lines[Index] = FString::Printf(TEXT("%s=%s"), *Key, *Value);
					}
					return;
				}
			}
			Lines.Insert(FString::Printf(TEXT("%s=%s"), *Key, *Value), End);
		}

		bool IsGameNetDriverLine(const FString& Line)
		{
			const FString Trimmed = Line.TrimStart();
			return (Trimmed.StartsWith(TEXT("+NetDriverDefinitions=")) || Trimmed.StartsWith(TEXT("-NetDriverDefinitions=")))
				&& Trimmed.Contains(TEXT("DefName=\"GameNetDriver\""));
		}
	}

	EService ParseService(const FString& DefaultPlatformService)
	{
		const FString Name = DefaultPlatformService.TrimStartAndEnd();
		if (Name.IsEmpty() || Name.Equals(TEXT("Null"), ESearchCase::IgnoreCase))
		{
			return EService::Null;
		}
		if (Name.Equals(TEXT("Steam"), ESearchCase::IgnoreCase))
		{
			return EService::Steam;
		}
		if (Name.Equals(TEXT("EOS"), ESearchCase::IgnoreCase))
		{
			return EService::Eos;
		}
		return EService::Other;
	}

	FName GetSubsystemName(const EService Service)
	{
		switch (Service)
		{
		case EService::Steam:
			return FName(TEXT("STEAM"));
		case EService::Eos:
			return FName(TEXT("EOS"));
		default:
			return FName(TEXT("NULL"));
		}
	}

	FString GetRecommendedDriver(const EService Service, const int32 EngineMinor)
	{
		switch (Service)
		{
		case EService::Steam:
			return SteamDriver;
		case EService::Eos:
			return EngineMinor >= 6 ? EosDriverNew : EosDriverOld;
		default:
			return IpDriver;
		}
	}

	TArray<FString> GetRequiredPlugins(const EService Service, const int32 EngineMinor)
	{
		switch (Service)
		{
		case EService::Steam:
			return { TEXT("OnlineSubsystemSteam"), TEXT("SteamSockets") };
		case EService::Eos:
			// The net driver moved to the SocketSubsystemEOS plugin in 5.6; the plugin exists since 5.4 as a dependency.
			return EngineMinor >= 6 ? TArray<FString>{ TEXT("OnlineSubsystemEOS"), TEXT("SocketSubsystemEOS") } : TArray<FString>{ TEXT("OnlineSubsystemEOS") };
		default:
			return {};
		}
	}

	bool IsSameDriverClass(const FString& A, const FString& B, const int32 EngineMinor)
	{
		const FString NormalizedA = NormalizeClassPath(A);
		const FString NormalizedB = NormalizeClassPath(B);
		if (NormalizedA == NormalizedB)
		{
			return true;
		}
		if (EngineMinor >= 6)
		{
			const FString Old = NormalizeClassPath(EosDriverOld);
			const FString New = NormalizeClassPath(EosDriverNew);
			return (NormalizedA == Old && NormalizedB == New) || (NormalizedA == New && NormalizedB == Old);
		}
		return false;
	}

	bool IsEosProductConfigured(const FString& DefaultArtifactName, const TArray<FString>& Artifacts)
	{
		if (DefaultArtifactName.TrimStartAndEnd().IsEmpty())
		{
			return false;
		}
		for (const FString& Artifact : Artifacts)
		{
			if (ReadStructField(Artifact, TEXT("ArtifactName")).Equals(DefaultArtifactName.TrimStartAndEnd(), ESearchCase::IgnoreCase)
				&& !ReadStructField(Artifact, TEXT("ClientId")).IsEmpty() && !ReadStructField(Artifact, TEXT("ProductId")).IsEmpty()
				&& !ReadStructField(Artifact, TEXT("SandboxId")).IsEmpty() && !ReadStructField(Artifact, TEXT("DeploymentId")).IsEmpty())
			{
				return true;
			}
		}
		return false;
	}

	TArray<FString> FindMapsNotCooked(const TArray<FString>& Required, const TArray<FString>& MapsToCook, const TArray<FString>& AlwaysCookFolders,
		const bool bCookAll, const TArray<FString>& AutoCookedMaps)
	{
		TArray<FString> Missing;
		if (bCookAll)
		{
			return Missing;
		}
		TSet<FString> Listed;
		for (const FString& Map : MapsToCook)
		{
			Listed.Add(NormalizeMapPath(Map));
		}
		for (const FString& Map : AutoCookedMaps)
		{
			Listed.Add(NormalizeMapPath(Map));
		}
		for (const FString& Map : Required)
		{
			const FString Normalized = NormalizeMapPath(Map);
			bool bCooked = Listed.Contains(Normalized);
			for (const FString& Folder : AlwaysCookFolders)
			{
				FString Prefix = Folder.TrimStartAndEnd().ToLower();
				Prefix.RemoveFromEnd(TEXT("/"));
				bCooked = bCooked || (!Prefix.IsEmpty() && Normalized.StartsWith(Prefix + TEXT("/")));
			}
			if (!bCooked)
			{
				Missing.AddUnique(Map);
			}
		}
		return Missing;
	}

	TArray<FCKSessionsFinding> Evaluate(const FSnapshot& Snapshot)
	{
		TArray<FCKSessionsFinding> Findings;
		const EService Service = ParseService(Snapshot.DefaultPlatformService);
		const bool bOnline = Service == EService::Steam || Service == EService::Eos;
		const FString ServiceLabel = GetServiceLabel(Service);
		const FString Engine = EngineLabel(Snapshot.EngineMinor);
		const FString Expected = GetRecommendedDriver(Service, Snapshot.EngineMinor);

		bool bPluginMissing = false;
		if (bOnline)
		{
			for (const FString& Plugin : GetRequiredPlugins(Service, Snapshot.EngineMinor))
			{
				if (!IsPluginEnabled(Snapshot, Plugin))
				{
					bPluginMissing = true;
					AddFinding(Findings, ECKSessionsFindingCode::PluginNotEnabled, ECKSessionsFindingSeverity::Error,
						FString::Printf(TEXT("%s is the online service, but the engine plugin %s is not enabled in the project."), *ServiceLabel, *Plugin),
						FString::Printf(TEXT("Enable the plugin %s in Edit > Plugins and restart the editor, or let the setup window enable it."), *Plugin));
				}
			}
		}

		if (Snapshot.GameNetDriverClass.IsEmpty())
		{
			if (bOnline)
			{
				AddFinding(Findings, ECKSessionsFindingCode::DriverWrong, ECKSessionsFindingSeverity::Error,
					FString::Printf(TEXT("No net driver is defined for GameNetDriver, but %s needs %s."), *ServiceLabel, *Expected),
					FString::Printf(TEXT("In DefaultEngine.ini, section [/Script/Engine.GameEngine], add +NetDriverDefinitions=(DefName=\"GameNetDriver\",DriverClassName=\"%s\",DriverClassNameFallback=\"%s\")."), *Expected, IpDriver));
			}
		}
		else if (!Snapshot.bGameNetDriverLoads)
		{
			AddFinding(Findings, ECKSessionsFindingCode::DriverNotLoadable, ECKSessionsFindingSeverity::Error,
				FString::Printf(TEXT("The net driver %s does not load on Unreal Engine %s; the engine uses the fallback driver, so clients cannot join through %s."), *Snapshot.GameNetDriverClass, *Engine, *ServiceLabel),
				bOnline && IsSameDriverClass(Snapshot.GameNetDriverClass, Expected, Snapshot.EngineMinor)
					? (bPluginMissing ? FString(TEXT("Enable the plugins named above; the driver lives in them.")) : FString::Printf(TEXT("Enable the plugin that holds %s and restart the editor."), *Expected))
					: FString::Printf(TEXT("Use the driver of this engine version: %s."), *Expected));
		}
		else if (bOnline && !IsSameDriverClass(Snapshot.GameNetDriverClass, Expected, Snapshot.EngineMinor))
		{
			AddFinding(Findings, ECKSessionsFindingCode::DriverWrong, ECKSessionsFindingSeverity::Error,
				FString::Printf(TEXT("GameNetDriver uses %s, but %s on Unreal Engine %s needs %s. With another driver the host creates the session, but the client cannot join."), *Snapshot.GameNetDriverClass, *ServiceLabel, *Engine, *Expected),
				FString::Printf(TEXT("In DefaultEngine.ini, section [/Script/Engine.GameEngine], remove the entry of the GameNetDriver you use now (-NetDriverDefinitions=(...)) and add +NetDriverDefinitions=(DefName=\"GameNetDriver\",DriverClassName=\"%s\",DriverClassNameFallback=\"%s\"), or let the setup window do it."), *Expected, IpDriver));
		}

		if (Snapshot.bLoadedSubsystemKnown)
		{
			if (Snapshot.LoadedSubsystem.IsNone())
			{
				AddFinding(Findings, ECKSessionsFindingCode::NoOnlineSubsystem, ECKSessionsFindingSeverity::Error,
					TEXT("The game has no online subsystem, so sessions cannot be hosted or found."),
					TEXT("Check [OnlineSubsystem] DefaultPlatformService in DefaultEngine.ini and that the plugin Online Subsystem Null (or the service's plugin) is enabled."));
			}
			else if (bOnline && !Snapshot.LoadedSubsystem.ToString().Equals(GetSubsystemName(Service).ToString(), ESearchCase::IgnoreCase))
			{
				if (Service == EService::Steam && Snapshot.bPlayInEditor)
				{
					AddFinding(Findings, ECKSessionsFindingCode::SteamNotInEditor, ECKSessionsFindingSeverity::Info,
						FString::Printf(TEXT("Play In Editor uses the %s subsystem: Steam does not start inside the editor. This is not an error."), *Snapshot.LoadedSubsystem.ToString()),
						FString());
				}
				else
				{
					const FString Hint = Service == EService::Steam
						? TEXT("Start the Steam client and log in. In a packaged Development build put steam_appid.txt next to the executable. Look for LogOnline lines about Steam in the log.")
						: TEXT("Fill in the EOS product settings (Project Settings > Plugins > Epic Online Services). Look for LogEOS lines in the log.");
					AddFinding(Findings, ECKSessionsFindingCode::SubsystemFellBack, ECKSessionsFindingSeverity::Error,
						FString::Printf(TEXT("DefaultPlatformService is %s, but the engine loaded %s instead: the service did not start, and sessions work on the local network only."), *ServiceLabel, *Snapshot.LoadedSubsystem.ToString()),
						Hint);
				}
			}
		}

		if (Service == EService::Steam && !Snapshot.bShippingBuild && !Snapshot.bEditor && !Snapshot.bDedicatedServer && !Snapshot.bSteamAppIdFilePresent)
		{
			AddFinding(Findings, ECKSessionsFindingCode::SteamAppIdMissing, ECKSessionsFindingSeverity::Error,
				TEXT("This build has no steam_appid.txt next to the executable. A packaged build that is not Shipping does not start Steam without it, and the engine silently uses another subsystem."),
				TEXT("Create steam_appid.txt next to the executable with your Steam app id on one line (480 is Valve's test id). A Shipping build started by Steam does not need it."));
		}

		if (Service == EService::Steam && Snapshot.bSessionUsesPresence != Snapshot.bSessionUsesLobbies)
		{
			const bool bRefused = Snapshot.EngineMinor >= 8;
			AddFinding(Findings, ECKSessionsFindingCode::SessionFlagsMismatch, bRefused ? ECKSessionsFindingSeverity::Error : ECKSessionsFindingSeverity::Warning,
				FString::Printf(TEXT("Uses Presence is %s and Use Lobbies If Available is %s in the session settings. %s"),
					Snapshot.bSessionUsesPresence ? TEXT("on") : TEXT("off"), Snapshot.bSessionUsesLobbies ? TEXT("on") : TEXT("off"),
					bRefused ? TEXT("Steam on Unreal Engine 5.8 refuses to create or join such a session.") : TEXT("Steam on Unreal Engine 5.8 refuses such a session, so a project moved there stops working.")),
				TEXT("Set both flags the same way in the session settings you build yourself. Settings built from a CoopKit Sessions Profile already do."));
		}

		if (Service == EService::Eos && !Snapshot.bEosProductConfigured)
		{
			AddFinding(Findings, ECKSessionsFindingCode::EosProductMissing, ECKSessionsFindingSeverity::Error,
				TEXT("The EOS product settings are not filled in: the default artifact needs a client id, product id, sandbox id and deployment id."),
				TEXT("Copy them from the Epic Developer Portal into Project Settings > Plugins > Epic Online Services, and set the default artifact name. The setup window never writes these values."));
		}

		if (!Snapshot.MissingMaps.IsEmpty())
		{
			AddFinding(Findings, ECKSessionsFindingCode::MapNotPackaged, ECKSessionsFindingSeverity::Error,
				FString::Printf(TEXT("A map CoopKit Sessions travels to is missing from the game or will not be packaged: %s."), *FString::Join(Snapshot.MissingMaps, TEXT(", "))),
				TEXT("Add the map in Project Settings > Packaging > Additional Asset Directories to Cook or List of maps to include, or fix the path in Project Settings > CoopKit Sessions."));
		}

		Findings.StableSort([](const FCKSessionsFinding& A, const FCKSessionsFinding& B) { return static_cast<uint8>(A.Severity) > static_cast<uint8>(B.Severity); });
		return Findings;
	}

	FString FormatFinding(const FCKSessionsFinding& Finding)
	{
		const TCHAR* Level = Finding.Severity == ECKSessionsFindingSeverity::Error ? TEXT("Error")
			: Finding.Severity == ECKSessionsFindingSeverity::Warning ? TEXT("Warning") : TEXT("Note");
		return Finding.Fix.IsEmpty() ? FString::Printf(TEXT("%s: %s"), Level, *Finding.Message)
			: FString::Printf(TEXT("%s: %s Fix: %s"), Level, *Finding.Message, *Finding.Fix);
	}

	FSnapshot GatherSnapshot(const UWorld* World)
	{
		FSnapshot Snapshot;
		Snapshot.EngineMinor = ENGINE_MINOR_VERSION;
		Snapshot.bEditor = GIsEditor;
		Snapshot.bShippingBuild = UE_BUILD_SHIPPING != 0;
		Snapshot.bDedicatedServer = IsRunningDedicatedServer();

		if (GConfig != nullptr)
		{
			GConfig->GetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), Snapshot.DefaultPlatformService, GEngineIni);
		}

		if (World != nullptr)
		{
			Snapshot.bLoadedSubsystemKnown = true;
			Snapshot.bPlayInEditor = World->WorldType == EWorldType::PIE;
			Snapshot.bDedicatedServer = Snapshot.bDedicatedServer || World->GetNetMode() == NM_DedicatedServer;
			if (const IOnlineSubsystem* Subsystem = ::Online::GetSubsystem(World))
			{
				Snapshot.LoadedSubsystem = Subsystem->GetSubsystemName();
			}
		}

		// The engine takes the first definition named GameNetDriver (UEngine::CreateNamedNetDriver_Local).
		if (GEngine != nullptr)
		{
			for (const FNetDriverDefinition& Definition : GEngine->NetDriverDefinitions)
			{
				if (Definition.DefName == NAME_GameNetDriver)
				{
					Snapshot.GameNetDriverClass = Definition.DriverClassName.ToString();
					const UClass* DriverClass = StaticLoadClass(UNetDriver::StaticClass(), nullptr, *Snapshot.GameNetDriverClass, nullptr, LOAD_Quiet | LOAD_NoWarn);
					Snapshot.bGameNetDriverLoads = DriverClass != nullptr && DriverClass->GetDefaultObject<UNetDriver>()->IsAvailable();
					break;
				}
			}
		}

		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
		{
			Snapshot.EnabledPlugins.Add(Plugin->GetName());
		}

		Snapshot.bSteamAppIdFilePresent = FPaths::FileExists(FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("steam_appid.txt")));

		const UCKSessionsSettings& Settings = UCKSessionsSettings::Get();
		const UCKSessionsProfileData* Profile = Settings.LoadDefaultProfile();
		if (Profile == nullptr)
		{
			Profile = GetDefault<UCKSessionsProfileData>();
		}
		FOnlineSessionSettings SessionSettings;
		CKSessions::Online::BuildSessionSettings(*Profile, false, CKSessions::Online::EHostKind::ListenServer, FString(), SessionSettings);
		Snapshot.bSessionUsesPresence = SessionSettings.bUsesPresence;
		Snapshot.bSessionUsesLobbies = SessionSettings.bUseLobbiesIfAvailable;

		if (GConfig != nullptr)
		{
			FString ArtifactName;
			TArray<FString> Artifacts;
			GConfig->GetString(TEXT("/Script/OnlineSubsystemEOS.EOSSettings"), TEXT("DefaultArtifactName"), ArtifactName, GEngineIni);
			GConfig->GetArray(TEXT("/Script/OnlineSubsystemEOS.EOSSettings"), TEXT("Artifacts"), Artifacts, GEngineIni);
			Snapshot.bEosProductConfigured = IsEosProductConfigured(ArtifactName, Artifacts);
		}

		if (!Settings.GetMenuMap().IsNull())
		{
			Snapshot.RequiredMaps.AddUnique(Settings.GetMenuMap().GetLongPackageName());
		}
		if (const UCKSessionsLobbyRulesData* Rules = Settings.LoadDefaultLobbyRules())
		{
			if (!Rules->GetMatchMap().IsNull())
			{
				Snapshot.RequiredMaps.AddUnique(Rules->GetMatchMap().GetLongPackageName());
			}
		}
		for (const FString& Map : Snapshot.RequiredMaps)
		{
			if (!FPackageName::DoesPackageExist(Map))
			{
				Snapshot.MissingMaps.Add(Map);
			}
		}
		return Snapshot;
	}

	FString ApplySetupToIni(const FString& Ini, const EService Service, const int32 EngineMinor)
	{
		const bool bCrLf = Ini.Contains(TEXT("\r\n"));
		TArray<FString> Lines;
		FString(Ini).Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), false);
		while (!Lines.IsEmpty() && Lines.Last().TrimStartAndEnd().IsEmpty())
		{
			Lines.Pop();
		}

		const bool bOnline = Service == EService::Steam || Service == EService::Eos;
		SetKey(Lines, TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), bOnline ? GetServiceLabel(Service) : FString(TEXT("Null")), false);
		if (Service == EService::Steam)
		{
			SetKey(Lines, TEXT("OnlineSubsystemSteam"), TEXT("bEnabled"), TEXT("true"), false);
			SetKey(Lines, TEXT("OnlineSubsystemSteam"), TEXT("SteamDevAppId"), TEXT("480"), true);
		}
		else if (Service == EService::Eos)
		{
			SetKey(Lines, TEXT("OnlineSubsystemEOS"), TEXT("bEnabled"), TEXT("true"), false);
		}

		// Drop earlier GameNetDriver entries of the section (ours from a previous run, or the buyer's old ones).
		const int32 ExistingHeader = FindSection(Lines, NetDriverSection);
		if (ExistingHeader != INDEX_NONE)
		{
			int32 SectionLast = ExistingHeader + 1;
			while (SectionLast < Lines.Num() && !Lines[SectionLast].TrimStart().StartsWith(TEXT("[")))
			{
				++SectionLast;
			}
			for (int32 Index = SectionLast - 1; Index > ExistingHeader; --Index)
			{
				if (IsGameNetDriverLine(Lines[Index]))
				{
					Lines.RemoveAt(Index);
				}
			}
		}
		if (bOnline)
		{
			const int32 Header = EnsureSection(Lines, NetDriverSection);
			const int32 End = FindSectionEnd(Lines, Header);
			Lines.Insert(FString::Printf(TEXT("-NetDriverDefinitions=(DefName=\"GameNetDriver\",DriverClassName=\"%s\",DriverClassNameFallback=\"%s\")"), IpDriver, IpDriver), End);
			Lines.Insert(FString::Printf(TEXT("+NetDriverDefinitions=(DefName=\"GameNetDriver\",DriverClassName=\"%s\",DriverClassNameFallback=\"%s\")"),
				*GetRecommendedDriver(Service, EngineMinor), IpDriver), End + 1);
		}

		return FString::Join(Lines, bCrLf ? TEXT("\r\n") : TEXT("\n")) + (bCrLf ? TEXT("\r\n") : TEXT("\n"));
	}
}
