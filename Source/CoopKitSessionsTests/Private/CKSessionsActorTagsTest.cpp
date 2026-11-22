// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsActorTags.h"
#include "CKSessionsTestTagComponent.h"
#include "CKSessionsTestTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		UCKSessionsTestTagComponent* AddTagComponent(AActor& Actor, const FGameplayTag Tag)
		{
			UCKSessionsTestTagComponent* Component = NewObject<UCKSessionsTestTagComponent>(&Actor);
			Component->SetPublishedTags(FGameplayTagContainer(Tag));
			Component->RegisterComponent();
			return Component;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsActorTagsTest, "CoopKit.Sessions.Tags.GetActorTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsActorTagsTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;

	const FGameplayTag TestStateDown = GetTestTag(TEXT("CK.Sessions.Test.State.Down"));
	const FGameplayTag TestStateBusy = GetTestTag(TEXT("CK.Sessions.Test.State.Busy"));
	const FGameplayTag TestState = GetTestTag(TEXT("CK.Sessions.Test.State"));
	const FGameplayTag TestOther = GetTestTag(TEXT("CK.Sessions.Test.Other"));
	if (!TestTrue(TEXT("The tests module registered its test tags"), TestStateDown.IsValid() && TestStateBusy.IsValid() && TestState.IsValid() && TestOther.IsValid()))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);

	AActor* Actor = World->SpawnActor<AActor>();
	if (TestNotNull(TEXT("Actor"), Actor))
	{
		FGameplayTagContainer Tags;
		CKSessions::Tags::GetActorTags(Actor, Tags);
		TestTrue(TEXT("An actor without tag sources has no tags"), Tags.IsEmpty());

		// Two components from different "plugins": both sources count, although each resets the container it is given.
		AddTagComponent(*Actor, TestStateDown);
		AddTagComponent(*Actor, TestStateBusy);
		CKSessions::Tags::GetActorTags(Actor, Tags);
		TestEqual(TEXT("Tags of every component"), Tags.Num(), 2);
		TestTrue(TEXT("First component tag"), Tags.HasTagExact(TestStateDown));
		TestTrue(TEXT("Second component tag"), Tags.HasTagExact(TestStateBusy));

		TestTrue(TEXT("Parent tag matches a child tag"), CKSessions::Tags::HasActorTag(Actor, TestState));
		TestFalse(TEXT("Unrelated tag"), CKSessions::Tags::HasActorTag(Actor, TestOther));
	}

	FGameplayTagContainer NullTags;
	CKSessions::Tags::GetActorTags(nullptr, NullTags);
	TestTrue(TEXT("Null actor gives no tags"), NullTags.IsEmpty());

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
