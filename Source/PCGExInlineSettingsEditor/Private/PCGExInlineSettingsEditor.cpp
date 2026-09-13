// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExInlineSettingsEditor.h"

#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FPCGExInlineSettingsEditorModule"

void FPCGExInlineSettingsEditorModule::StartupModule()
{
}

void FPCGExInlineSettingsEditorModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGExInlineSettingsEditorModule, PCGExInlineSettingsEditor)
