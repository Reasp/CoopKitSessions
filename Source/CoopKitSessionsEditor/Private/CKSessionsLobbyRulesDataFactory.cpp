// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsLobbyRulesDataFactory.h"
#include "CKSessionsLobbyRulesData.h"

UCKSessionsLobbyRulesDataFactory::UCKSessionsLobbyRulesDataFactory()
{
	SupportedClass = UCKSessionsLobbyRulesData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UCKSessionsLobbyRulesDataFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, const FName InName, const EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UCKSessionsLobbyRulesData>(InParent, InClass, InName, Flags);
}
