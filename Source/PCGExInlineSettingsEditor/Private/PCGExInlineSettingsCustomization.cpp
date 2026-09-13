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
#include "Engine/Blueprint.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "StructUtils/PropertyBag.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
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
#include "Helpers/PCGAssetHelpers.h"

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

	// Base-class properties are node plumbing (debug, asset info, GPU, determinism); Seed is the exception.
	bool ShouldHideInstanceProperty(const FProperty* InProperty)
	{
		if (!InProperty || InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, Seed))
		{
			return false;
		}

		const UClass* OwnerClass = InProperty->GetOwnerClass();
		return OwnerClass == UPCGSettings::StaticClass() || OwnerClass == UPCGSettingsInterface::StaticClass() || OwnerClass == UPCGData::StaticClass();
	}

	// Leaf property handles under a category or object node, nested categories flattened.
	void GatherVisibleProperties(const TSharedRef<IPropertyHandle>& InContainer, TArray<TSharedRef<IPropertyHandle>>& OutProperties)
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
				GatherVisibleProperties(Child.ToSharedRef(), OutProperties);
			}
			else if (!ShouldHideInstanceProperty(Child->GetProperty()))
			{
				OutProperties.Add(Child.ToSharedRef());
			}
		}
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

		FPCGAssetHelpers::FBlueprintAssetOutput AssetOutput;
		if (!FPCGAssetHelpers::GetBlueprintAssetRegistryData(AssetData, AssetOutput))
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
			UE_LOGF(LogTemp, Warning, "PCGExInlineSettings: could not load Blueprint element '%ls'.", *Entry.BlueprintElementClass.ToString());
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
		bNotAllowed |= !Value.IsExternal() && Value.Instance && !Value.Instance->GetClass()->IsChildOf(BaseClass);
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

	// An inline object property exposes an object node; its children are category handles (or properties without categories).
	uint32 NumObjectNodes = 0;
	InstanceHandle->GetNumChildren(NumObjectNodes);

	for (uint32 ObjectIndex = 0; ObjectIndex < NumObjectNodes; ++ObjectIndex)
	{
		const TSharedPtr<IPropertyHandle> ObjectHandle = InstanceHandle->GetChildHandle(ObjectIndex);
		uint32 NumChildren = 0;
		if (!ObjectHandle.IsValid() || ObjectHandle->GetNumChildren(NumChildren) != FPropertyAccess::Success)
		{
			continue;
		}

		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			const TSharedPtr<IPropertyHandle> Child = ObjectHandle->GetChildHandle(ChildIndex);
			if (!Child.IsValid())
			{
				continue;
			}

			if (!Child->GetProperty())
			{
				TArray<TSharedRef<IPropertyHandle>> Properties;
				PCGExInlineSettingsCustomization::GatherVisibleProperties(Child.ToSharedRef(), Properties);
				if (Properties.IsEmpty())
				{
					continue;
				}

				const FText GroupName = Child->GetPropertyDisplayName();
				IDetailGroup& Group = ChildBuilder.AddGroup(FName(*GroupName.ToString()), GroupName, /*bStartExpanded=*/true);
				for (const TSharedRef<IPropertyHandle>& Property : Properties)
				{
					Group.AddPropertyRow(Property).IsEnabled(RowsEnabled);
				}
			}
			else if (!PCGExInlineSettingsCustomization::ShouldHideInstanceProperty(Child->GetProperty()))
			{
				ChildBuilder.AddProperty(Child.ToSharedRef()).IsEnabled(RowsEnabled);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
