// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsDiagnostics.h"
#include "CoreMinimal.h"

/**
 * Setup check of the Steam and EOS configuration in the editor: gathers the project's settings into a snapshot for
 * CKSessions::Diagnostics::Evaluate, and applies the recommended setup of a service to the project.
 */
namespace CKSessions::Setup
{
	/** Id of the nomad tab of the setup window (Tools > CoopKit Sessions Setup). */
	extern COOPKITSESSIONSEDITOR_API const FName TabId;

	/**
	 * Snapshot of the open project: the editor's running configuration, the enabled plugins and whether the packaged build
	 * will contain the maps (the project's packaging settings). The loaded subsystem stays unknown: the editor does not start Steam.
	 */
	COOPKITSESSIONSEDITOR_API CKSessions::Diagnostics::FSnapshot GatherProjectSnapshot();

	/** Absolute path of the project's Config/DefaultEngine.ini. */
	COOPKITSESSIONSEDITOR_API FString GetProjectEngineIniPath();

	/** Engine plugins the service needs that the project has not enabled yet. */
	COOPKITSESSIONSEDITOR_API TArray<FString> GetPluginsToEnable(CKSessions::Diagnostics::EService Service);

	/**
	 * Writes the recommended setup of the service for this engine version into the ini file (see ApplySetupToIni); a missing
	 * file is created. Nothing else is touched.
	 * @return False with OutError when the file could not be read or written.
	 */
	COOPKITSESSIONSEDITOR_API bool ApplyToIniFile(const FString& IniPath, CKSessions::Diagnostics::EService Service, FString& OutError);

	/** Enables the plugins of GetPluginsToEnable in the project file (.uproject). False with OutError when a plugin could not be enabled or the project could not be saved. */
	COOPKITSESSIONSEDITOR_API bool EnablePlugins(CKSessions::Diagnostics::EService Service, FString& OutError);
}
