// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExInlineSettingsEditor.h"

#include "PropertyEditorModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"

#include "PCGExInlineSettingsCustomization.h"
#include "PCGExInlineSettingsResync.h"
#include "PCGExInlineSettingsTypes.h"

#define LOCTEXT_NAMESPACE "FPCGExInlineSettingsEditorModule"

void FPCGExInlineSettingsEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	// Exact-name lookup: structs deriving from FPCGExInlineSettings need their own registration.
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGExInlineSettings::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExInlineSettingsCustomization::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddStatic(&PCGExInlineSettingsResync::OnAssetRenamed);
}

void FPCGExInlineSettingsEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().OnAssetRenamed().Remove(AssetRenamedHandle);
	}
	AssetRenamedHandle.Reset();

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout(FPCGExInlineSettings::StaticStruct()->GetFName());
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGExInlineSettingsEditorModule, PCGExInlineSettingsEditor)
