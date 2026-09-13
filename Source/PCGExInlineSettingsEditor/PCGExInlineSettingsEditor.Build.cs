// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using UnrealBuildTool;

public class PCGExInlineSettingsEditor : ModuleRules
{
	public PCGExInlineSettingsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PCGExInlineSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"PropertyEditor", // RegisterCustomPropertyTypeLayout, SObjectPropertyEntryBox
			"UnrealEd",       // FScopedTransaction
			"AssetRegistry",  // Blueprint element discovery, asset rename listener
			"PCG",
		});
	}
}
