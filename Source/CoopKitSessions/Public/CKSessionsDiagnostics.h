// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"
#include "CKSessionsDiagnostics.generated.h"

class UWorld;

/** How serious a problem found by the setup check is. */
UENUM(BlueprintType)
enum class ECKSessionsFindingSeverity : uint8
{
	/** A note: nothing is wrong, but the result may surprise you (for example Steam in Play In Editor). */
	Info,
	/** Something may break on some engine versions or machines. */
	Warning,
	/** The online service or the join will not work as configured. */
	Error
};

/** What a finding of the setup check is about. */
UENUM(BlueprintType)
enum class ECKSessionsFindingCode : uint8
{
	/** The game has no online subsystem at all. */
	NoOnlineSubsystem,
	/** DefaultPlatformService names Steam or EOS, but the engine loaded another subsystem (usually Null). */
	SubsystemFellBack,
	/** Steam is configured, and the game runs in Play In Editor, where Steam does not start. */
	SteamNotInEditor,
	/** An engine plugin the configured service needs is not enabled in the project. */
	PluginNotEnabled,
	/** GameNetDriver is not the net driver of the configured service, so clients cannot join through it. */
	DriverWrong,
	/** The class named for GameNetDriver does not load, so the engine uses the fallback. */
	DriverNotLoadable,
	/** A packaged Development build has no steam_appid.txt next to the executable, so Steam does not start. */
	SteamAppIdMissing,
	/** The session settings differ in Uses Presence and Use Lobbies If Available; Steam refuses such sessions in UE 5.8. */
	SessionFlagsMismatch,
	/** The menu map or the lobby's match map is missing from the game or will not be packaged. */
	MapNotPackaged,
	/** The service is EOS, but the product settings (artifact, client, product, sandbox, deployment) are not filled in. */
	EosProductMissing
};

/** One result of the setup check: what is wrong, how serious it is and what to do about it. */
USTRUCT(BlueprintType)
struct COOPKITSESSIONS_API FCKSessionsFinding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	ECKSessionsFindingCode Code = ECKSessionsFindingCode::NoOnlineSubsystem;

	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	ECKSessionsFindingSeverity Severity = ECKSessionsFindingSeverity::Info;

	/** What is wrong, in one or two sentences. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FString Message;

	/** What to change; empty for a note. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FString Fix;
};

/**
 * Checks of the Steam and EOS setup, written as pure functions of a snapshot of the settings, so that every engine version
 * and every mistake is covered by automation tests without Steam or EOS. The runtime gathers the snapshot from the running
 * game (GatherSnapshot), the editor window gathers it from the project; both call Evaluate.
 */
namespace CKSessions::Diagnostics
{
	/** The online service the project asks for with [OnlineSubsystem] DefaultPlatformService. */
	enum class EService : uint8
	{
		/** Null, or nothing configured: sessions on the local network. */
		Null,
		Steam,
		Eos,
		/** Any other service; the check only reports the net driver that does not load. */
		Other
	};

	/** Everything the check needs to know. The defaults describe a plain project on UE 5.4 with Null. */
	struct FSnapshot
	{
		/** Minor version of Unreal Engine 5: 4 for 5.4 ... 8 for 5.8. */
		int32 EngineMinor = 4;

		/** [OnlineSubsystem] DefaultPlatformService; empty means Null. */
		FString DefaultPlatformService;

		/** False in the editor window outside Play: the online subsystem is not asked, so a fall back cannot be seen. */
		bool bLoadedSubsystemKnown = false;

		/** Name of the online subsystem the game's world uses (NULL, STEAM, EOS); none when it has none. */
		FName LoadedSubsystem;

		/** Class path of the net driver definition named GameNetDriver (the first one wins in the engine); empty when none. */
		FString GameNetDriverClass;

		/** True when GameNetDriverClass loads. */
		bool bGameNetDriverLoads = true;

		/** Names of the enabled plugins. */
		TArray<FString> EnabledPlugins;

		/** True when steam_appid.txt lies next to the executable. */
		bool bSteamAppIdFilePresent = false;

		/** The check runs in a Shipping build. */
		bool bShippingBuild = false;

		/** The check runs inside the editor (Play In Editor or not). */
		bool bEditor = false;

		/** The game is a Play In Editor world. */
		bool bPlayInEditor = false;

		/** The game is a dedicated server. */
		bool bDedicatedServer = false;

		/** Uses Presence of the session settings a listen server hosts with. */
		bool bSessionUsesPresence = false;

		/** Use Lobbies If Available of the same settings. */
		bool bSessionUsesLobbies = false;

		/** The EOS artifact of the project has a client, product, sandbox and deployment id. */
		bool bEosProductConfigured = false;

		/** Long package names of the maps the plugin needs: the menu map and the default lobby rules' match map. */
		TArray<FString> RequiredMaps;

		/** Maps of RequiredMaps that are missing or will not be packaged. */
		TArray<FString> MissingMaps;
	};

	/** Service named by DefaultPlatformService (case-insensitive); empty and NULL are Null. */
	COOPKITSESSIONS_API EService ParseService(const FString& DefaultPlatformService);

	/** Name of the online subsystem of a service, as IOnlineSubsystem::GetSubsystemName returns it. */
	COOPKITSESSIONS_API FName GetSubsystemName(EService Service);

	/**
	 * Class path of the net driver GameNetDriver should use with the service on the engine version, verified in the
	 * UE 5.4-5.8 sources: SteamSocketsNetDriver for Steam; NetDriverEOS of OnlineSubsystemEOS in 5.4 and 5.5 and of
	 * SocketSubsystemEOS from 5.6; IpNetDriver for Null.
	 */
	COOPKITSESSIONS_API FString GetRecommendedDriver(EService Service, int32 EngineMinor);

	/** Engine plugins the service needs on the engine version. Empty for Null. */
	COOPKITSESSIONS_API TArray<FString> GetRequiredPlugins(EService Service, int32 EngineMinor);

	/**
	 * True when two net driver class paths name the same class: "/Script/" and the module prefix spelling do not matter,
	 * and from 5.6 the engine redirects the old EOS class (OnlineSubsystemEOS.NetDriverEOS) to the new one.
	 */
	COOPKITSESSIONS_API bool IsSameDriverClass(const FString& A, const FString& B, int32 EngineMinor);

	/**
	 * True when the EOS artifact settings name a default artifact that has a client id, product id, sandbox id and deployment id.
	 * @param Artifacts  Values of the Artifacts array of [/Script/OnlineSubsystemEOS.EOSSettings], one per entry.
	 */
	COOPKITSESSIONS_API bool IsEosProductConfigured(const FString& DefaultArtifactName, const TArray<FString>& Artifacts);

	/**
	 * Maps of Required that a packaged build does not contain: not listed in the project's maps to cook, not under a folder
	 * that is always cooked, and not a map the cooker takes by itself (the game default map and the like).
	 * Every path is a long package name such as /Game/Maps/L_Menu.
	 */
	COOPKITSESSIONS_API TArray<FString> FindMapsNotCooked(const TArray<FString>& Required, const TArray<FString>& MapsToCook,
		const TArray<FString>& AlwaysCookFolders, bool bCookAll, const TArray<FString>& AutoCookedMaps);

	/** Findings of the snapshot, most serious first; empty when the setup is fine. */
	COOPKITSESSIONS_API TArray<FCKSessionsFinding> Evaluate(const FSnapshot& Snapshot);

	/** One line for a log or a window: "Error: message Fix: fix". */
	COOPKITSESSIONS_API FString FormatFinding(const FCKSessionsFinding& Finding);

	/**
	 * Snapshot of the running game. Fills everything except the editor-only parts (bEditor stays as GIsEditor says,
	 * MissingMaps comes from the packages that exist, not from the cook rules).
	 * @param World  World of the game; null leaves the loaded subsystem unknown.
	 */
	COOPKITSESSIONS_API FSnapshot GatherSnapshot(const UWorld* World);

	/**
	 * The rewritten text of a project's DefaultEngine.ini with the recommended setup of a service on the engine version:
	 * DefaultPlatformService, the service's bEnabled, SteamDevAppId=480 when none is set, and the GameNetDriver definition
	 * (the engine's IP driver entry removed, the service's driver added; none for Null). Other lines stay. Applying the
	 * result again changes nothing. EOS product credentials are never written.
	 * @param Ini  Current text; empty for a project without DefaultEngine.ini.
	 */
	COOPKITSESSIONS_API FString ApplySetupToIni(const FString& Ini, EService Service, int32 EngineMinor);
}
