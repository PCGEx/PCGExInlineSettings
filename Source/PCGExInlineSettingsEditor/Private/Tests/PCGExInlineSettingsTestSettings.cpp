// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tests/PCGExInlineSettingsTestSettings.h"

UPCGExInlineSettingsTestSettings::UPCGExInlineSettingsTestSettings()
{
#if WITH_EDITORONLY_DATA
	bExposeToLibrary = false;
#endif
}

FPCGElementPtr UPCGExInlineSettingsTestSettings::CreateElement() const
{
	// Never executed: the tests only inspect overridable params and serialization.
	return nullptr;
}
