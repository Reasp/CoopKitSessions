// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

/** Log category of the CoopKitSessions plugin. */
DECLARE_LOG_CATEGORY_EXTERN(LogCKSessions, Log, All);

/**
 * Runtime module of the CoopKitSessions plugin.
 * Registers the plugin with the engine; gameplay functionality lives in the plugin's components and subsystems.
 */
class FCoopKitSessionsModule : public IModuleInterface
{
public:
	/** Called after the module DLL is loaded. */
	virtual void StartupModule() override;

	/** Called before the module is unloaded. */
	virtual void ShutdownModule() override;
};
