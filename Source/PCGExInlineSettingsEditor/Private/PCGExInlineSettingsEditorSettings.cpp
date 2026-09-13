// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExInlineSettingsEditorSettings.h"

#include "PCGSettings.h"

UPCGExInlineSettingsEditorSettings::UPCGExInlineSettingsEditorSettings()
{
	ShownBaseProperties.Add(GET_MEMBER_NAME_CHECKED(UPCGSettings, Seed));
}
