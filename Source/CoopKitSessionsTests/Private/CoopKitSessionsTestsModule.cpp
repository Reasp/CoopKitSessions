// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsCompat.h"
#include "CKSessionsTestTags.h"
#include "GameplayTagsManager.h"
#include "Modules/ModuleManager.h"

namespace CKSessions::Tests
{
	FGameplayTag GetTestTag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(TagName), false);
	}
}

/**
 * Editor-only tests module. Registers the test-only Gameplay Tags while the tag manager accepts them: a native tag
 * (UE_DEFINE_GAMEPLAY_TAG) in an Editor module trips an engine ensure (NativeGameplayTags.cpp,
 * VerifyModuleCanContainGameplayTag: native tags must live in Runtime modules so client and server tags match).
 */
class FCoopKitSessionsTestsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		CKSessions::Compat::CallOrRegisterOnAddNativeTags(FSimpleMulticastDelegate::FDelegate::CreateStatic(&FCoopKitSessionsTestsModule::RegisterTestTags));
	}

private:
	static void RegisterTestTags()
	{
		UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
		for (const TCHAR* TagName : CKSessions::Tests::TestTagNames)
		{
			Manager.AddNativeGameplayTag(FName(TagName), TEXT("Test only: CoopKitSessions automation tests."));
		}
	}
};

IMPLEMENT_MODULE(FCoopKitSessionsTestsModule, CoopKitSessionsTests);
