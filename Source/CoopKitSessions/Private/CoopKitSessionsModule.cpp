// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CoopKitSessionsModule.h"
#include "CKSessionsDebug.h"

DEFINE_LOG_CATEGORY(LogCKSessions);

void FCoopKitSessionsModule::StartupModule()
{
	CKSessions::Debug::Register();
}

void FCoopKitSessionsModule::ShutdownModule()
{
	CKSessions::Debug::Unregister();
}

IMPLEMENT_MODULE(FCoopKitSessionsModule, CoopKitSessions)
