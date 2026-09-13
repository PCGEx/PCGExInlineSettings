// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExInlineSettingsCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Algo/AllOf.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/Blueprint.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "StructUtils/PropertyBag.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Textures/SlateIcon.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#include "PCGData.h"
#include "PCGGraph.h"
#include "PCGInputOutputSettings.h"
#include "PCGSettings.h"
#include "PCGSubgraph.h"
#include "Elements/PCGExecuteBlueprint.h"
#include "Elements/PCGReroute.h"
#include "Elements/PCGUserParameterGet.h"
#include "Elements/Blueprint/PCGBlueprintBaseElement.h"

#include "PCGExInlineSettingsEditorSettings.h"
#include "PCGExInlineSettingsTypes.h"

#define LOCTEXT_NAMESPACE "PCGExInlineSettingsCustomization"

namespace PCGExInlineSettingsCustomization
{
	// Structural nodes are only offered when the allowed class explicitly targets them.
	bool IsStructural(const UClass* InClass, const UClass* InAllowedClass)
	{
		const UClass* const StructuralBases[] =
		{
			UPCGBaseSubgraphSettings::StaticClass(),
			UPCGRerouteSettings::StaticClass(),
			UPCGGraphInputOutputSettings::StaticClass(),
			UPCGUserParameterGetSettings::StaticClass(),
			UPCGGenericUserParameterGetSettings::StaticClass(),
		};

		for (const UClass* StructuralBase : StructuralBases)
		{
			if (InClass->IsChildOf(StructuralBase) && !(InAllowedClass && InAllowedClass->IsChildOf(StructuralBase)))
			{
				return true;
			}
		}

		return false;
	}

	FText GetTypeCategory(const UPCGSettings* InSettings)
	{
		return StaticEnum<EPCGSettingsType>()->GetDisplayNameTextByValue(static_cast<int64>(InSettings->GetType()));
	}

	FText MakeVariantCategory(const FText& InCategory, const FText& InLabel)
	{
		return FText::Format(LOCTEXT("VariantCategory", "{0}|{1}"), InCategory, InLabel);
	}

	FText GetInstanceTitle(const UPCGSettings* InSettings)
	{
		FText Title = InSettings->GetDefaultNodeTitle();
		if (Title.IsEmpty())
		{
			Title = InSettings->GetClass()->GetDisplayNameText();
		}

		const FString Extra = InSettings->GetAdditionalTitleInformation();
		return Extra.IsEmpty() ? Title : FText::Format(LOCTEXT("TitleWithExtra", "{0} ({1})"), Title, FText::FromString(Extra));
	}

	// Properties declared by the PCG bases are node plumbing (debug, asset info, GPU, determinism), not settings.
	bool IsBaseClassProperty(const FProperty* InProperty)
	{
		const UClass* OwnerClass = InProperty->GetOwnerClass();
		return OwnerClass == UPCGSettings::StaticClass() || OwnerClass == UPCGSettingsInterface::StaticClass() || OwnerClass == UPCGData::StaticClass();
	}

	// First segment of the property's Category meta, the group it lands in.
	FName GetTopCategory(const FProperty* InProperty)
	{
		FString Category = InProperty->GetMetaData(TEXT("Category"));
		int32 SeparatorIndex = INDEX_NONE;
		if (Category.FindChar(TEXT('|'), SeparatorIndex))
		{
			Category.LeftInline(SeparatorIndex);
		}
		Category.TrimStartAndEndInline();
		return Category.IsEmpty() ? NAME_None : FName(*Category);
	}

	// Leaf property handles under an object node, category nodes flattened when the host created any.
	void GatherLeafProperties(const TSharedRef<IPropertyHandle>& InContainer, TArray<TSharedRef<IPropertyHandle>>& OutProperties)
	{
		uint32 NumChildren = 0;
		if (InContainer->GetNumChildren(NumChildren) != FPropertyAccess::Success)
		{
			return;
		}

		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			const TSharedPtr<IPropertyHandle> Child = InContainer->GetChildHandle(ChildIndex);
			if (!Child.IsValid())
			{
				continue;
			}

			if (!Child->GetProperty())
			{
				GatherLeafProperties(Child.ToSharedRef(), OutProperties);
			}
			else
			{
				OutProperties.Add(Child.ToSharedRef());
			}
		}
	}

	// Menu row for the visibility popover: a small dot (bright = shown, dim = hidden, half = partly shown; accent = local
	// override) before the label. The label stays an STextBlock so the menu search still finds it.
	TSharedRef<SWidget> MakeVisibilityEntry(const FText& InLabel, const FText& InTooltip, const int32 InNumShown, const int32 InNumTotal, const bool bInLocalOverride)
	{
		const float Alpha = InNumShown == 0 ? 0.25f : (InNumShown < InNumTotal ? 0.55f : 0.9f);
		FLinearColor DotColor = bInLocalOverride ? FStyleColors::AccentBlue.GetSpecifiedColor() : FLinearColor::White;
		DotColor.A = Alpha;

		return SNew(SHorizontalBox)
			.ToolTipText(InTooltip)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.BulletPoint"))
				.ColorAndOpacity(DotColor)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(InLabel)
			];
	}

	FText GetVisibilityTooltip(const int32 InNumShown, const int32 InNumTotal, const bool bInLocalOverride)
	{
		const FText State = InNumShown == 0
			? LOCTEXT("StateHidden", "Hidden")
			: (InNumShown < InNumTotal ? FText::Format(LOCTEXT("StatePartial", "{0} of {1} shown"), InNumShown, InNumTotal) : LOCTEXT("StateShown", "Shown"));
		return bInLocalOverride ? FText::Format(LOCTEXT("StateWithLocal", "{0} (local override)"), State) : State;
	}

	FText GetForceStateLabel(const FPCGExInlineSettingsCustomization::EForceState InState)
	{
		switch (InState)
		{
		case FPCGExInlineSettingsCustomization::EForceState::Default: return LOCTEXT("ForceDefault", "Default");
		case FPCGExInlineSettingsCustomization::EForceState::Shown: return LOCTEXT("ForceShown", "Always Show");
		case FPCGExInlineSettingsCustomization::EForceState::Hidden: return LOCTEXT("ForceHidden", "Always Hide");
		default: checkNoEntry(); return FText::GetEmpty();
		}
	}

	// Class of an asset by path, from the loaded object or the asset registry; null when neither knows it.
	const UClass* GetAssetClass(const FSoftObjectPath& InPath)
	{
		if (const UObject* Loaded = InPath.ResolveObject())
		{
			return Loaded->GetClass();
		}

		const IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
		const FAssetData AssetData = AssetRegistry ? AssetRegistry->GetAssetByObjectPath(InPath) : FAssetData();
		return AssetData.IsValid() ? AssetData.GetClass() : nullptr;
	}

	// 5.7 has no FPCGAssetHelpers: the same registry tags the PCG palette reads, in the 5.8 helper's shape.
	struct FBlueprintAssetOutput
	{
		FText Category;
		FText Description;
		FSoftClassPath GeneratedClass;
		bool bOnlyExposePreconfiguredSettings = false;
		bool bEnabledPreconfiguredSettings = false;
	};

	bool GetBlueprintAssetRegistryData(const FAssetData& InAssetData, FBlueprintAssetOutput& OutOutput)
	{
		OutOutput.GeneratedClass = FSoftClassPath(InAssetData.GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath));
		OutOutput.Category = InAssetData.GetTagValueRef<FText>(GET_MEMBER_NAME_CHECKED(UPCGBlueprintBaseElement, Category));
		OutOutput.Description = InAssetData.GetTagValueRef<FText>(GET_MEMBER_NAME_CHECKED(UPCGBlueprintBaseElement, Description));
		OutOutput.bOnlyExposePreconfiguredSettings = InAssetData.GetTagValueRef<bool>(GET_MEMBER_NAME_CHECKED(UPCGBlueprintBaseElement, bOnlyExposePreconfiguredSettings));
		OutOutput.bEnabledPreconfiguredSettings = InAssetData.GetTagValueRef<bool>(GET_MEMBER_NAME_CHECKED(UPCGBlueprintBaseElement, bEnablePreconfiguredSettings));
		return true;
	}

	// A replaced instance owned by this outer leaves its package (undoable); a shared one belongs to another object.
	void ReleaseOwnedInstance(UObject* InOuter, FPCGExInlineSettings& InValue)
	{
		UPCGSettings* Previous = InValue.Instance;
		if (Previous && Previous->GetOuter() == InOuter)
		{
			Previous->SetFlags(RF_Transactional);
			Previous->Rename(nullptr, GetTransientOuterForRename(Previous->GetClass()), REN_DontCreateRedirectors);
		}

		InValue.Instance = nullptr;
	}
}

TSharedRef<IPropertyTypeCustomization> FPCGExInlineSettingsCustomization::MakeInstance()
{
	return MakeShared<FPCGExInlineSettingsCustomization>();
}

void FPCGExInlineSettingsCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructHandle = PropertyHandle;
	SettingsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExInlineSettings, Settings), /*bRecurse=*/false);
	InstanceHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExInlineSettingsBase, Instance), /*bRecurse=*/false);
	ExternalHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExInlineSettingsBase, External), /*bRecurse=*/false);
	AllowedClassHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExInlineSettingsBase, AllowedClass), /*bRecurse=*/false);
	PropertyUtilities = CustomizationUtils.GetPropertyUtilities();
	Site = DetectSite();

	// Only the name and value widgets survive when the row is hosted by a property bag, so everything lives in them.
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(600.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.IsEnabled(this, &FPCGExInlineSettingsCustomization::CanPickClass)
				.ToolTipText(this, &FPCGExInlineSettingsCustomization::GetClassTooltip)
				.OnGetMenuContent(this, &FPCGExInlineSettingsCustomization::BuildClassMenu)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(this, &FPCGExInlineSettingsCustomization::GetClassLabel)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Warning"))
				.ToolTipText(LOCTEXT("NotAllowedTooltip", "The inline settings class is not permitted by the allowed class."))
				.Visibility(this, &FPCGExInlineSettingsCustomization::GetNotAllowedVisibility)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SComboButton)
				.HasDownArrow(false)
				.ToolTipText(LOCTEXT("VisibilityMenuTooltip", "Choose which inline properties are shown or hidden here, over the plugin's editor settings."))
				.Visibility(this, &FPCGExInlineSettingsCustomization::GetVisibilityMenuVisibility)
				.OnGetMenuContent(this, &FPCGExInlineSettingsCustomization::BuildVisibilityMenu)
				.ButtonContent()
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Visibility"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("MakeLocalCopy", "Make Local Copy"))
				.ToolTipText(LOCTEXT("MakeLocalCopyTooltip", "These inline settings are shared with the parent graph and are read-only here. Copy them into this object to edit them."))
				.IsEnabled(this, &FPCGExInlineSettingsCustomization::IsValueEditable)
				.Visibility(this, &FPCGExInlineSettingsCustomization::GetMakeLocalCopyVisibility)
				.OnClicked(this, &FPCGExInlineSettingsCustomization::OnMakeLocalCopy)
			]
		];
}

void FPCGExInlineSettingsCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	if (Site == ESite::Definition && AllowedClassHandle.IsValid())
	{
		ChildBuilder.AddProperty(AllowedClassHandle.ToSharedRef());
	}

	const TSharedRef<SWidget> ExternalNameWidget = ExternalHandle.IsValid() ? ExternalHandle->CreatePropertyNameWidget() : SNullWidget::NullWidget;

	ChildBuilder.AddCustomRow(LOCTEXT("ExternalRowSearch", "External"))
		.IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FPCGExInlineSettingsCustomization::IsValueEditable)))
		.NameContent()
		[
			ExternalNameWidget
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(600.0f)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UPCGSettings::StaticClass())
			.ObjectPath(this, &FPCGExInlineSettingsCustomization::GetExternalPath)
			.OnShouldFilterAsset(this, &FPCGExInlineSettingsCustomization::ShouldFilterExternalAsset)
			.OnObjectChanged(this, &FPCGExInlineSettingsCustomization::OnExternalChanged)
			.AllowClear(true)
			.DisplayThumbnail(false)
		];

	AddInstanceRows(ChildBuilder);
}

FPCGExInlineSettingsCustomization::ESite FPCGExInlineSettingsCustomization::DetectSite() const
{
	TArray<UObject*> Outers;
	StructHandle->GetOuterObjects(Outers);

	if (Outers.IsEmpty())
	{
		return ESite::Owner;
	}

	if (Algo::AllOf(Outers, [](const UObject* Outer) { return Outer && Outer->IsA<UPCGGraph>(); }))
	{
		return ESite::Definition;
	}

	if (Algo::AllOf(Outers, [](const UObject* Outer) { return Outer && Outer->IsA<UPCGGraphInstance>(); }))
	{
		return ESite::Override;
	}

	return ESite::Owner;
}

UClass* FPCGExInlineSettingsCustomization::GetEffectiveAllowedClass() const
{
	// Override sites read the definition: an overridden local copy keeps whatever allowed class it was copied with.
	if (Site == ESite::Override)
	{
		const FProperty* ParameterProperty = StructHandle->GetProperty();
		const bool bIsTopLevelParameter = ParameterProperty && Cast<UPropertyBag>(ParameterProperty->GetOwnerStruct());

		if (bIsTopLevelParameter)
		{
			TArray<UObject*> Outers;
			StructHandle->GetOuterObjects(Outers);

			for (const UObject* Outer : Outers)
			{
				const UPCGGraphInstance* GraphInstance = Cast<UPCGGraphInstance>(Outer);
				const UPCGGraph* Graph = GraphInstance ? GraphInstance->GetGraph() : nullptr;
				const FInstancedPropertyBag* Parameters = Graph ? Graph->GetUserParametersStruct() : nullptr;
				if (!Parameters)
				{
					continue;
				}

				const TValueOrError<FPCGExInlineSettings*, EPropertyBagResult> Definition = Parameters->GetValueStruct<FPCGExInlineSettings>(ParameterProperty->GetFName());
				if (Definition.HasValue() && Definition.GetValue())
				{
					return Definition.GetValue()->AllowedClass.Get();
				}
			}
		}
	}

	UClass* LocalAllowedClass = nullptr;
	ForEachValue([&LocalAllowedClass](UObject*, FPCGExInlineSettings& Value)
	{
		if (!LocalAllowedClass)
		{
			LocalAllowedClass = Value.AllowedClass.Get();
		}
	});

	return LocalAllowedClass;
}

void FPCGExInlineSettingsCustomization::ForEachValue(TFunctionRef<void(UObject* Outer, FPCGExInlineSettings& Value)> Func) const
{
	if (!StructHandle.IsValid())
	{
		return;
	}

	TArray<UObject*> Outers;
	StructHandle->GetOuterObjects(Outers);

	StructHandle->EnumerateRawData([&Outers, &Func](void* RawData, const int32 DataIndex, const int32 NumDatas)
	{
		// Raw data and outer objects are index-aligned for a struct held by objects; anything else isn't editable here.
		if (RawData && Outers.Num() == NumDatas && Outers.IsValidIndex(DataIndex) && Outers[DataIndex])
		{
			Func(Outers[DataIndex], *static_cast<FPCGExInlineSettings*>(RawData));
		}
		return true;
	});
}

void FPCGExInlineSettingsCustomization::Commit(const FText& TransactionText, TFunctionRef<void(UObject* Outer, FPCGExInlineSettings& Value)> Mutator)
{
	TArray<UObject*> Outers;
	StructHandle->GetOuterObjects(Outers);
	if (Outers.IsEmpty())
	{
		return;
	}

	FScopedTransaction Transaction(TransactionText);
	StructHandle->NotifyPreChange();

	ForEachValue([&Mutator](UObject* Outer, FPCGExInlineSettings& Value)
	{
		Outer->Modify();
		Mutator(Outer, Value);
		Value.SyncSettings();
	});

	StructHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	StructHandle->NotifyFinishedChangingProperties();

	// Rebuilds the panel and destroys this customization; nothing may touch members afterwards.
	if (const TSharedPtr<IPropertyUtilities> Utilities = PropertyUtilities)
	{
		Utilities->ForceRefresh();
	}
}

bool FPCGExInlineSettingsCustomization::IsValueEditable() const
{
	return SettingsHandle.IsValid() && SettingsHandle->IsEditable();
}

bool FPCGExInlineSettingsCustomization::HasExternal() const
{
	bool bHasExternal = false;
	ForEachValue([&bHasExternal](UObject*, FPCGExInlineSettings& Value) { bHasExternal |= Value.IsExternal(); });
	return bHasExternal;
}

bool FPCGExInlineSettingsCustomization::HasSharedInstance() const
{
	bool bHasShared = false;
	ForEachValue([&bHasShared](UObject* Outer, FPCGExInlineSettings& Value)
	{
		bHasShared |= Value.Instance && Value.Instance->GetOuter() != Outer;
	});
	return bHasShared;
}

bool FPCGExInlineSettingsCustomization::CanPickClass() const
{
	return IsValueEditable() && !HasExternal();
}

bool FPCGExInlineSettingsCustomization::IsInlineEditable() const
{
	return CanPickClass() && !HasSharedInstance();
}

TSharedRef<SWidget> FPCGExInlineSettingsCustomization::BuildClassMenu()
{
	GatherMenuEntries(GetEffectiveAllowedClass());

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	MenuBuilder.AddSearchWidget();

	MenuBuilder.BeginSection(NAME_None);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("ClearEntry", "None"),
		LOCTEXT("ClearEntryTooltip", "Remove the inline settings."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &FPCGExInlineSettingsCustomization::OnClearPicked)));
	MenuBuilder.EndSection();

	// Entries are sorted by category, so each run of equal categories becomes one section.
	int32 EntryIndex = 0;
	while (EntryIndex < MenuEntries.Num())
	{
		const FText Category = MenuEntries[EntryIndex].Category;
		MenuBuilder.BeginSection(NAME_None, Category);

		for (; EntryIndex < MenuEntries.Num() && MenuEntries[EntryIndex].Category.EqualTo(Category); ++EntryIndex)
		{
			const FMenuEntry& Entry = MenuEntries[EntryIndex];
			MenuBuilder.AddMenuEntry(
				Entry.Label,
				Entry.Tooltip,
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FPCGExInlineSettingsCustomization::OnMenuEntryPicked, EntryIndex)));
		}

		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget(nullptr, 500);
}

void FPCGExInlineSettingsCustomization::GatherMenuEntries(const UClass* InAllowedClass)
{
	MenuEntries.Reset();

	const UClass* BaseClass = InAllowedClass ? InAllowedClass : UPCGSettings::StaticClass();

	// Same filter as the PCG node palette, plus editor-only classes (stripped on cook) and structural nodes.
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(BaseClass)
			|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden | CLASS_HideDropDown | CLASS_CompiledFromBlueprint)
			|| PCGExInlineSettingsCustomization::IsStructural(Class, InAllowedClass))
		{
			continue;
		}

		const UPCGSettings* DefaultSettings = Class->GetDefaultObject<UPCGSettings>();
		if (!DefaultSettings || !DefaultSettings->bExposeToLibrary || DefaultSettings->IsEditorOnly())
		{
			continue;
		}

		const FText Label = PCGExInlineSettingsCustomization::GetInstanceTitle(DefaultSettings);
		const FText Category = PCGExInlineSettingsCustomization::GetTypeCategory(DefaultSettings);
		const FText Tooltip = DefaultSettings->GetNodeTooltipText();
		const TArray<FPCGPreConfiguredSettingsInfo> Variants = DefaultSettings->GetPreconfiguredInfo();

		if (Variants.IsEmpty() || !DefaultSettings->OnlyExposePreconfiguredSettings())
		{
			FMenuEntry& Entry = MenuEntries.AddDefaulted_GetRef();
			Entry.SettingsClass = Class;
			Entry.Label = Label;
			Entry.Category = Category;
			Entry.Tooltip = Tooltip;
		}

		const FText VariantCategory = DefaultSettings->GroupPreconfiguredSettings() ? PCGExInlineSettingsCustomization::MakeVariantCategory(Category, Label) : Category;
		for (const FPCGPreConfiguredSettingsInfo& Variant : Variants)
		{
			FMenuEntry& Entry = MenuEntries.AddDefaulted_GetRef();
			Entry.SettingsClass = Class;
			Entry.Preconfigured = Variant;
			Entry.Label = Variant.Label;
			Entry.Category = VariantCategory;
			Entry.Tooltip = Variant.Tooltip.IsEmpty() ? Tooltip : Variant.Tooltip;
		}
	}

	if (UPCGBlueprintSettings::StaticClass()->IsChildOf(BaseClass))
	{
		GatherBlueprintElementEntries(PCGExInlineSettingsCustomization::GetTypeCategory(GetDefault<UPCGBlueprintSettings>()));
	}

	MenuEntries.Sort([](const FMenuEntry& A, const FMenuEntry& B)
	{
		const int32 CategoryOrder = A.Category.CompareTo(B.Category);
		return CategoryOrder != 0 ? CategoryOrder < 0 : A.Label.CompareTo(B.Label) < 0;
	});
}

void FPCGExInlineSettingsCustomization::GatherBlueprintElementEntries(const FText& InFallbackCategory)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Mirrors the PCG palette's Blueprint element discovery (PCGEditorUtils::ForEachPCGBlueprintAssetData).
	TSet<FTopLevelAssetPath> ElementClassPaths;
	AssetRegistry.GetDerivedClassNames({UPCGBlueprintBaseElement::StaticClass()->GetClassPathName()}, TSet<FTopLevelAssetPath>(), ElementClassPaths);
	ElementClassPaths.Add(UPCGBlueprintBaseElement::StaticClass()->GetClassPathName());

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	AssetRegistry.EnumerateAssets(Filter, [this, &ElementClassPaths, &InFallbackCategory](const FAssetData& AssetData)
	{
		if (!UAssetRegistryHelpers::IsAssetDataBlueprintOfClassSet(AssetData, ElementClassPaths)
			|| !AssetData.GetTagValueRef<bool>(GET_MEMBER_NAME_CHECKED(UPCGBlueprintBaseElement, bExposeToLibrary)))
		{
			return true;
		}

		PCGExInlineSettingsCustomization::FBlueprintAssetOutput AssetOutput;
		if (!PCGExInlineSettingsCustomization::GetBlueprintAssetRegistryData(AssetData, AssetOutput))
		{
			return true;
		}

		TArray<FPCGPreConfiguredSettingsInfo> Variants;
		if (AssetOutput.bEnabledPreconfiguredSettings)
		{
			const TSubclassOf<UPCGBlueprintBaseElement> ElementClass = AssetOutput.GeneratedClass.TryLoadClass<UPCGBlueprintBaseElement>();
			if (const UPCGBlueprintBaseElement* DefaultElement = ElementClass ? ElementClass->GetDefaultObject<UPCGBlueprintBaseElement>() : nullptr)
			{
				Variants = DefaultElement->PreconfiguredInfo;
			}
		}

		const FText Label = FText::FromString(FName::NameToDisplayString(AssetData.AssetName.ToString(), false));
		const FText Category = AssetOutput.Category.IsEmpty() ? InFallbackCategory : AssetOutput.Category;

		if (Variants.IsEmpty() || !AssetOutput.bOnlyExposePreconfiguredSettings)
		{
			FMenuEntry& Entry = MenuEntries.AddDefaulted_GetRef();
			Entry.BlueprintElementClass = AssetOutput.GeneratedClass;
			Entry.Label = Label;
			Entry.Category = Category;
			Entry.Tooltip = AssetOutput.Description;
		}

		const FText VariantCategory = PCGExInlineSettingsCustomization::MakeVariantCategory(Category, Label);
		for (const FPCGPreConfiguredSettingsInfo& Variant : Variants)
		{
			FMenuEntry& Entry = MenuEntries.AddDefaulted_GetRef();
			Entry.BlueprintElementClass = AssetOutput.GeneratedClass;
			Entry.Preconfigured = Variant;
			Entry.Label = Variant.Label;
			Entry.Category = VariantCategory;
			Entry.Tooltip = Variant.Tooltip.IsEmpty() ? AssetOutput.Description : Variant.Tooltip;
		}

		return true;
	});
}

void FPCGExInlineSettingsCustomization::OnMenuEntryPicked(int32 EntryIndex)
{
	if (!MenuEntries.IsValidIndex(EntryIndex))
	{
		return;
	}

	const FMenuEntry Entry = MenuEntries[EntryIndex];

	UClass* SettingsClass = Entry.SettingsClass.Get();
	TSubclassOf<UPCGBlueprintBaseElement> ElementClass;

	if (!Entry.BlueprintElementClass.IsNull())
	{
		ElementClass = Entry.BlueprintElementClass.TryLoadClass<UPCGBlueprintBaseElement>();
		if (!ElementClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("PCGExInlineSettings: could not load Blueprint element '%s'."), *Entry.BlueprintElementClass.ToString());
			return;
		}

		SettingsClass = UPCGBlueprintSettings::StaticClass();
	}

	if (!SettingsClass)
	{
		return;
	}

	Commit(
		FText::Format(LOCTEXT("SetClassTransaction", "Set Inline Settings to {0}"), Entry.Label),
		[&Entry, SettingsClass, &ElementClass](UObject* Outer, FPCGExInlineSettings& Value)
		{
			// The previous instance outlives this call (it only moves to the transient package), so its values can be read.
			const UPCGSettings* Previous = Value.Instance;
			PCGExInlineSettingsCustomization::ReleaseOwnedInstance(Outer, Value);

			UPCGSettings* NewInstance = PCGExInlineSettings::CreateInstance(Outer, SettingsClass);
			if (!NewInstance)
			{
				return;
			}

			if (UPCGBlueprintSettings* BlueprintSettings = Cast<UPCGBlueprintSettings>(NewInstance); BlueprintSettings && ElementClass)
			{
				UPCGBlueprintBaseElement* ElementInstance = nullptr;
				BlueprintSettings->SetBlueprintElementType(ElementClass, ElementInstance);
			}

			// Before the variant is applied, so a preconfigured value always wins over a carried-over one.
			if (GetDefault<UPCGExInlineSettingsEditorSettings>()->bKeepEditedValuesOnClassChange)
			{
				PCGExInlineSettings::CopyMatchingValues(Previous, NewInstance);
			}

			if (Entry.Preconfigured.IsSet())
			{
				NewInstance->ApplyPreconfiguredSettings(Entry.Preconfigured.GetValue());
			}

			Value.Instance = NewInstance;
		});
}

void FPCGExInlineSettingsCustomization::OnClearPicked()
{
	Commit(
		LOCTEXT("ClearTransaction", "Clear Inline Settings"),
		[](UObject* Outer, FPCGExInlineSettings& Value)
		{
			PCGExInlineSettingsCustomization::ReleaseOwnedInstance(Outer, Value);
		});
}

FText FPCGExInlineSettingsCustomization::GetClassLabel() const
{
	if (HasExternal())
	{
		return LOCTEXT("ExternalLabel", "External asset");
	}

	bool bFirst = true;
	bool bMultipleValues = false;
	const UPCGSettings* FirstInstance = nullptr;

	ForEachValue([&](UObject*, FPCGExInlineSettings& Value)
	{
		const UPCGSettings* Instance = Value.Instance;
		if (bFirst)
		{
			FirstInstance = Instance;
			bFirst = false;
		}
		else if (Instance != FirstInstance && (!Instance || !FirstInstance || Instance->GetClass() != FirstInstance->GetClass()))
		{
			bMultipleValues = true;
		}
	});

	if (bMultipleValues)
	{
		return LOCTEXT("MultipleValuesLabel", "Multiple Values");
	}

	return FirstInstance ? PCGExInlineSettingsCustomization::GetInstanceTitle(FirstInstance) : LOCTEXT("NoneLabel", "None");
}

FText FPCGExInlineSettingsCustomization::GetClassTooltip() const
{
	FString GraphValue;
	bool bFirst = true;

	ForEachValue([&](UObject*, FPCGExInlineSettings& Value)
	{
		if (bFirst)
		{
			GraphValue = Value.Settings.ToString();
			bFirst = false;
		}
	});

	return GraphValue.IsEmpty()
		? LOCTEXT("PickClassTooltip", "Pick the inline settings class.")
		: FText::Format(LOCTEXT("PickClassWithValueTooltip", "Pick the inline settings class.\nGraph value: {0}"), FText::FromString(GraphValue));
}

EVisibility FPCGExInlineSettingsCustomization::GetNotAllowedVisibility() const
{
	const UClass* AllowedClass = GetEffectiveAllowedClass();
	const UClass* BaseClass = AllowedClass ? AllowedClass : UPCGSettings::StaticClass();

	bool bNotAllowed = false;
	ForEachValue([BaseClass, &bNotAllowed](UObject*, FPCGExInlineSettings& Value)
	{
		// External assets are checked through the registry so an unloaded asset still gets flagged.
		const UClass* EffectiveClass = nullptr;
		if (Value.IsExternal())
		{
			EffectiveClass = PCGExInlineSettingsCustomization::GetAssetClass(Value.External.ToSoftObjectPath());
		}
		else if (Value.Instance)
		{
			EffectiveClass = Value.Instance->GetClass();
		}

		bNotAllowed |= EffectiveClass && !EffectiveClass->IsChildOf(BaseClass);
	});

	return bNotAllowed ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FPCGExInlineSettingsCustomization::GetMakeLocalCopyVisibility() const
{
	return HasSharedInstance() && !HasExternal() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply FPCGExInlineSettingsCustomization::OnMakeLocalCopy()
{
	Commit(
		LOCTEXT("MakeLocalCopyTransaction", "Make Local Inline Settings Copy"),
		[](UObject* Outer, FPCGExInlineSettings& Value)
		{
			if (Value.Instance && Value.Instance->GetOuter() != Outer)
			{
				Value.Instance = PCGExInlineSettings::DuplicateInstance(Value.Instance, Outer);
			}
		});

	return FReply::Handled();
}

FString FPCGExInlineSettingsCustomization::GetExternalPath() const
{
	FString Path;
	bool bFirst = true;
	bool bMultipleValues = false;

	ForEachValue([&](UObject*, FPCGExInlineSettings& Value)
	{
		const FString ValuePath = Value.External.ToString();
		if (bFirst)
		{
			Path = ValuePath;
			bFirst = false;
		}
		else if (ValuePath != Path)
		{
			bMultipleValues = true;
		}
	});

	return bMultipleValues ? FString() : Path;
}

bool FPCGExInlineSettingsCustomization::ShouldFilterExternalAsset(const FAssetData& InAssetData) const
{
	const UClass* AssetClass = InAssetData.GetClass();
	const UClass* AllowedClass = GetEffectiveAllowedClass();
	return !AssetClass || !AssetClass->IsChildOf(AllowedClass ? AllowedClass : UPCGSettings::StaticClass());
}

void FPCGExInlineSettingsCustomization::OnExternalChanged(const FAssetData& InAssetData)
{
	const FSoftObjectPath NewPath = InAssetData.IsValid() ? InAssetData.GetSoftObjectPath() : FSoftObjectPath();

	Commit(
		LOCTEXT("SetExternalTransaction", "Set External Settings"),
		[&NewPath](UObject*, FPCGExInlineSettings& Value)
		{
			Value.External = TSoftObjectPtr<UPCGSettings>(NewPath);
		});
}

void FPCGExInlineSettingsCustomization::AddInstanceRows(IDetailChildrenBuilder& ChildBuilder)
{
	if (!InstanceHandle.IsValid())
	{
		return;
	}

	const TAttribute<bool> RowsEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FPCGExInlineSettingsCustomization::IsInlineEditable));

	// Grouping comes from each property's Category meta: object-rooted panels give category nodes, struct-rooted ones don't.
	uint32 NumObjectNodes = 0;
	InstanceHandle->GetNumChildren(NumObjectNodes);

	for (uint32 ObjectIndex = 0; ObjectIndex < NumObjectNodes; ++ObjectIndex)
	{
		const TSharedPtr<IPropertyHandle> ObjectHandle = InstanceHandle->GetChildHandle(ObjectIndex);
		if (!ObjectHandle.IsValid())
		{
			continue;
		}

		TArray<TSharedRef<IPropertyHandle>> Properties;
		PCGExInlineSettingsCustomization::GatherLeafProperties(ObjectHandle.ToSharedRef(), Properties);

		TMap<FName, IDetailGroup*> Groups;
		for (const TSharedRef<IPropertyHandle>& Property : Properties)
		{
			if (IsInstancePropertyHidden(Property->GetProperty()))
			{
				continue;
			}

			const FName Category = PCGExInlineSettingsCustomization::GetTopCategory(Property->GetProperty());
			if (Category.IsNone())
			{
				ChildBuilder.AddProperty(Property).IsEnabled(RowsEnabled);
				continue;
			}

			IDetailGroup*& Group = Groups.FindOrAdd(Category);
			if (!Group)
			{
				Group = &ChildBuilder.AddGroup(Category, FText::FromString(FName::NameToDisplayString(Category.ToString(), false)), /*bStartExpanded=*/true);
			}
			Group->AddPropertyRow(Property).IsEnabled(RowsEnabled);
		}
	}
}

bool FPCGExInlineSettingsCustomization::IsInstancePropertyHidden(const FProperty* InProperty) const
{
	if (!InProperty)
	{
		return true;
	}

	const FName PropertyName = InProperty->GetFName();
	const FName Category = PCGExInlineSettingsCustomization::GetTopCategory(InProperty);

	// Per-value force lists win, property before category; then the plugin's editor settings; then the base-class rule.
	switch (GetForceState(PropertyName))
	{
	case EForceState::Hidden: return true;
	case EForceState::Shown: return false;
	default: break;
	}

	switch (GetForceState(Category))
	{
	case EForceState::Hidden: return true;
	case EForceState::Shown: return false;
	default: break;
	}

	const UPCGExInlineSettingsEditorSettings* EditorSettings = GetDefault<UPCGExInlineSettingsEditorSettings>();
	if (EditorSettings->HiddenProperties.Contains(PropertyName) || EditorSettings->HiddenCategories.Contains(Category))
	{
		return true;
	}

	return EditorSettings->bHideBaseProperties && PCGExInlineSettingsCustomization::IsBaseClassProperty(InProperty) && !EditorSettings->ShownBaseProperties.Contains(PropertyName);
}

EVisibility FPCGExInlineSettingsCustomization::GetVisibilityMenuVisibility() const
{
	return Site == ESite::Definition ? EVisibility::Visible : EVisibility::Collapsed;
}

TSharedRef<SWidget> FPCGExInlineSettingsCustomization::BuildVisibilityMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	const UPCGSettings* FirstInstance = nullptr;
	ForEachValue([&FirstInstance](UObject*, FPCGExInlineSettings& Value)
	{
		if (!FirstInstance)
		{
			FirstInstance = Value.Instance;
		}
	});

	if (!FirstInstance)
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("NoInstanceForVisibility", "Pick inline settings first."), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
		return MenuBuilder.MakeWidget();
	}

	MenuBuilder.AddSearchWidget();

	struct FCategoryCount
	{
		int32 NumShown = 0;
		int32 NumTotal = 0;
	};

	TArray<FName> Categories;
	TMap<FName, FCategoryCount> CategoryCounts;
	TArray<const FProperty*> Properties;
	for (TFieldIterator<FProperty> It(FirstInstance->GetClass()); It; ++It)
	{
		if (!It->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		Properties.Add(*It);

		const FName Category = PCGExInlineSettingsCustomization::GetTopCategory(*It);
		if (!Category.IsNone())
		{
			Categories.AddUnique(Category);
			FCategoryCount& Count = CategoryCounts.FindOrAdd(Category);
			++Count.NumTotal;
			Count.NumShown += IsInstancePropertyHidden(*It) ? 0 : 1;
		}
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("CategoriesSection", "Categories"));
	for (const FName Category : Categories)
	{
		const FCategoryCount& Count = CategoryCounts.FindChecked(Category);
		const bool bLocal = GetForceState(Category) != EForceState::Default;
		MenuBuilder.AddSubMenu(
			PCGExInlineSettingsCustomization::MakeVisibilityEntry(
				FText::FromString(FName::NameToDisplayString(Category.ToString(), false)),
				PCGExInlineSettingsCustomization::GetVisibilityTooltip(Count.NumShown, Count.NumTotal, bLocal),
				Count.NumShown, Count.NumTotal, bLocal),
			FNewMenuDelegate::CreateSP(this, &FPCGExInlineSettingsCustomization::BuildForceStateSubMenu, Category));
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("PropertiesSection", "Properties"));
	for (const FProperty* Property : Properties)
	{
		const int32 NumShown = IsInstancePropertyHidden(Property) ? 0 : 1;
		const bool bLocal = GetForceState(Property->GetFName()) != EForceState::Default;
		MenuBuilder.AddSubMenu(
			PCGExInlineSettingsCustomization::MakeVisibilityEntry(
				Property->GetDisplayNameText(),
				PCGExInlineSettingsCustomization::GetVisibilityTooltip(NumShown, 1, bLocal),
				NumShown, 1, bLocal),
			FNewMenuDelegate::CreateSP(this, &FPCGExInlineSettingsCustomization::BuildForceStateSubMenu, Property->GetFName()));
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget(nullptr, 500);
}

void FPCGExInlineSettingsCustomization::BuildForceStateSubMenu(FMenuBuilder& MenuBuilder, FName InName)
{
	for (const EForceState State : {EForceState::Default, EForceState::Shown, EForceState::Hidden})
	{
		MenuBuilder.AddMenuEntry(
			PCGExInlineSettingsCustomization::GetForceStateLabel(State),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FPCGExInlineSettingsCustomization::SetForceState, InName, State),
				FCanExecuteAction(),
				FGetActionCheckState::CreateSP(this, &FPCGExInlineSettingsCustomization::GetForceCheckState, InName, State)),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
}

FPCGExInlineSettingsCustomization::EForceState FPCGExInlineSettingsCustomization::GetForceState(FName InName) const
{
	// Multi-edit reads the first value: definitions are edited one at a time.
	EForceState State = EForceState::Default;
	bool bFirst = true;
	ForEachValue([InName, &State, &bFirst](UObject*, FPCGExInlineSettings& Value)
	{
		if (!bFirst)
		{
			return;
		}
		bFirst = false;

		if (Value.ForceHidden.Contains(InName))
		{
			State = EForceState::Hidden;
		}
		else if (Value.ForceShown.Contains(InName))
		{
			State = EForceState::Shown;
		}
	});

	return State;
}

ECheckBoxState FPCGExInlineSettingsCustomization::GetForceCheckState(FName InName, EForceState InState) const
{
	return GetForceState(InName) == InState ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FPCGExInlineSettingsCustomization::SetForceState(FName InName, EForceState InState)
{
	Commit(
		LOCTEXT("SetForceStateTransaction", "Set Inline Settings Property Visibility"),
		[InName, InState](UObject*, FPCGExInlineSettings& Value)
		{
			Value.ForceShown.Remove(InName);
			Value.ForceHidden.Remove(InName);
			if (InState == EForceState::Shown)
			{
				Value.ForceShown.Add(InName);
			}
			else if (InState == EForceState::Hidden)
			{
				Value.ForceHidden.Add(InName);
			}
		});
}

#undef LOCTEXT_NAMESPACE
