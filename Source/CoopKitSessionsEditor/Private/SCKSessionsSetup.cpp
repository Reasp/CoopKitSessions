// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "SCKSessionsSetup.h"
#include "CKSessionsSetup.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SCKSessionsSetup"

namespace CKSessions::Setup::Window
{
	FLinearColor SeverityColor(const ECKSessionsFindingSeverity Severity)
	{
		switch (Severity)
		{
		case ECKSessionsFindingSeverity::Error:
			return FLinearColor(1.0f, 0.45f, 0.4f);
		case ECKSessionsFindingSeverity::Warning:
			return FLinearColor(1.0f, 0.8f, 0.3f);
		default:
			return FLinearColor(0.6f, 0.8f, 1.0f);
		}
	}

	FText SeverityLabel(const ECKSessionsFindingSeverity Severity)
	{
		switch (Severity)
		{
		case ECKSessionsFindingSeverity::Error:
			return LOCTEXT("Error", "Error");
		case ECKSessionsFindingSeverity::Warning:
			return LOCTEXT("Warning", "Warning");
		default:
			return LOCTEXT("Note", "Note");
		}
	}

	FText ServiceLabel(const CKSessions::Diagnostics::EService Service)
	{
		switch (Service)
		{
		case CKSessions::Diagnostics::EService::Steam:
			return LOCTEXT("Steam", "Steam");
		case CKSessions::Diagnostics::EService::Eos:
			return LOCTEXT("Eos", "EOS");
		default:
			return LOCTEXT("Null", "Null");
		}
	}
}

void SCKSessionsSetup::Construct(const FArguments& InArgs)
{
	using CKSessions::Diagnostics::EService;

	IniPath = InArgs._IniPath;
	bChangePlugins = InArgs._bChangePlugins;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
				.Text(LOCTEXT("Title", "CoopKit Sessions setup check"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("Intro", "Checks the online service, the net driver, the engine plugins, the maps and the EOS product settings of this project. Play In Editor cannot start Steam, so a Steam setup is checked from the files. A packaged build checks itself at its start: see the log, lines that start with Setup check."))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.Tag(TEXT("CoopKitSessionsSetup.Check"))
					.Text(LOCTEXT("Check", "Check again"))
					.OnClicked_Lambda([this]() { Check(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					MakeApplyButton(EService::Null, LOCTEXT("ApplyNull", "Apply recommended setup: Null"), TEXT("CoopKitSessionsSetup.Apply.Null"))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					MakeApplyButton(EService::Steam, LOCTEXT("ApplySteam", "Apply recommended setup: Steam"), TEXT("CoopKitSessionsSetup.Apply.Steam"))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeApplyButton(EService::Eos, LOCTEXT("ApplyEos", "Apply recommended setup: EOS"), TEXT("CoopKitSessionsSetup.Apply.Eos"))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FLinearColor(0.6f, 0.9f, 0.6f))
				.Text_Lambda([this]() { return Report; })
				.Visibility_Lambda([this]() { return Report.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(FindingsBox, SVerticalBox)
				]
			]
		]
	];

	Check();
}

TSharedRef<SWidget> SCKSessionsSetup::MakeApplyButton(const CKSessions::Diagnostics::EService Service, const FText& Label, const FName& ButtonTag)
{
	return SNew(SButton)
		.Tag(ButtonTag)
		.Text(Label)
		.ToolTipText(LOCTEXT("ApplyTip", "Writes the recommended settings of this service for this engine version into Config/DefaultEngine.ini and enables the engine plugins it needs. Asks first."))
		.OnClicked(this, &SCKSessionsSetup::OnApplyClicked, Service);
}

void SCKSessionsSetup::Check()
{
	Findings = CKSessions::Diagnostics::Evaluate(CKSessions::Setup::GatherProjectSnapshot());

	FindingsBox->ClearChildren();
	if (Findings.IsEmpty())
	{
		FindingsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FLinearColor(0.6f, 0.9f, 0.6f))
			.Text(LOCTEXT("NoFindings", "No problems found in the files of this project."))
		];
		return;
	}
	for (const FCKSessionsFinding& Finding : Findings)
	{
		FindingsBox->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(CKSessions::Setup::Window::SeverityColor(Finding.Severity))
				.Text(FText::Format(LOCTEXT("FindingLine", "{0}: {1}"), CKSessions::Setup::Window::SeverityLabel(Finding.Severity), FText::FromString(Finding.Message)))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(16.0f, 1.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Visibility(Finding.Fix.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				.Text(FText::Format(LOCTEXT("FixLine", "Fix: {0}"), FText::FromString(Finding.Fix)))
			]
		];
	}
}

bool SCKSessionsSetup::ApplySetup(const CKSessions::Diagnostics::EService Service)
{
	FString Error;
	const FString Path = IniPath.IsEmpty() ? CKSessions::Setup::GetProjectEngineIniPath() : IniPath;
	const TArray<FString> Plugins = bChangePlugins ? CKSessions::Setup::GetPluginsToEnable(Service) : TArray<FString>();
	if (!CKSessions::Setup::ApplyToIniFile(Path, Service, Error) || (bChangePlugins && !CKSessions::Setup::EnablePlugins(Service, Error)))
	{
		Report = FText::Format(LOCTEXT("ApplyFailed", "Not completed: {0}"), FText::FromString(Error));
		return false;
	}
	Report = FText::Format(LOCTEXT("Applied", "Saved the {0} setup to {1}.{2} Restart the editor to load it, then check again."),
		CKSessions::Setup::Window::ServiceLabel(Service), FText::FromString(Path),
		Plugins.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("PluginsEnabled", " Enabled the plugins: {0}."), FText::FromString(FString::Join(Plugins, TEXT(", ")))));
	return true;
}

FReply SCKSessionsSetup::OnApplyClicked(const CKSessions::Diagnostics::EService Service)
{
	const FText Question = FText::Format(LOCTEXT("Confirm", "Write the recommended {0} setup for this engine version into Config/DefaultEngine.ini and enable the engine plugins it needs? Other settings stay as they are. The editor must be restarted afterwards."),
		CKSessions::Setup::Window::ServiceLabel(Service));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Question) == EAppReturnType::Yes)
	{
		ApplySetup(Service);
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
