// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExInlineSettings.h"

#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FPCGExInlineSettingsModule"

void FPCGExInlineSettingsModule::StartupModule()
{
}

void FPCGExInlineSettingsModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGExInlineSettingsModule, PCGExInlineSettings)
