// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPath.h"
#include "Utils/PCGPreconfiguration.h"

class IDetailChildrenBuilder;
class IPropertyHandle;
class IPropertyUtilities;
class SWidget;
class UPCGSettings;
struct FAssetData;
struct FPCGExInlineSettings;

/**
 * Details customization for FPCGExInlineSettings: palette-style class menu, External asset picker, inline rows of the
 * owned instance, and the AllowedClass row on graph parameter definitions. Every write re-derives Settings.
 */
class FPCGExInlineSettingsCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ Begin IPropertyTypeCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ End IPropertyTypeCustomization interface

private:
	/** Where the struct is displayed; decides whether AllowedClass is editable and where it's read from. */
	enum class ESite : uint8
	{
		Owner,      // C++ owner (node settings, data assets, ...): AllowedClass comes from code
		Definition, // graph parameter definition, outer is a UPCGGraph
		Override,   // graph parameter override, outer is a UPCGGraphInstance (asset, component, subgraph node)
	};

	struct FMenuEntry
	{
		TSubclassOf<UPCGSettings> SettingsClass;
		FSoftClassPath BlueprintElementClass;
		TOptional<FPCGPreConfiguredSettingsInfo> Preconfigured;
		FText Label;
		FText Category;
		FText Tooltip;
	};

	ESite DetectSite() const;

	/** Allowed class in effect here: the definition's value at override sites, the local value elsewhere. */
	UClass* GetEffectiveAllowedClass() const;

	/** Calls Func for each edited value with the object that owns it. */
	void ForEachValue(TFunctionRef<void(UObject* Outer, FPCGExInlineSettings& Value)> Func) const;

	/** Mutates every edited value inside one transaction, re-derives Settings, notifies, then rebuilds the panel. */
	void Commit(const FText& TransactionText, TFunctionRef<void(UObject* Outer, FPCGExInlineSettings& Value)> Mutator);

	bool IsValueEditable() const;
	bool HasExternal() const;
	bool HasSharedInstance() const;
	bool CanPickClass() const;
	bool IsInlineEditable() const;

	TSharedRef<SWidget> BuildClassMenu();
	void GatherMenuEntries(const UClass* InAllowedClass);
	void GatherBlueprintElementEntries(const FText& InFallbackCategory);
	void OnMenuEntryPicked(int32 EntryIndex);
	void OnClearPicked();

	FText GetClassLabel() const;
	FText GetClassTooltip() const;
	EVisibility GetNotAllowedVisibility() const;
	EVisibility GetMakeLocalCopyVisibility() const;
	FReply OnMakeLocalCopy();

	FString GetExternalPath() const;
	bool ShouldFilterExternalAsset(const FAssetData& InAssetData) const;
	void OnExternalChanged(const FAssetData& InAssetData);

	void AddInstanceRows(IDetailChildrenBuilder& ChildBuilder);

	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyHandle> SettingsHandle;
	TSharedPtr<IPropertyHandle> InstanceHandle;
	TSharedPtr<IPropertyHandle> ExternalHandle;
	TSharedPtr<IPropertyHandle> AllowedClassHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
	ESite Site = ESite::Owner;

	/** Rebuilt each time the class menu opens; menu actions index into it. */
	TArray<FMenuEntry> MenuEntries;
};
