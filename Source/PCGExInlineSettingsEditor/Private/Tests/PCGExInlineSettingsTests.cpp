// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PCGGraph.h"
#include "PCGParamData.h"
#include "PCGSettings.h"
#include "PCGSubgraph.h"
#include "Helpers/PCGPropertyHelpers.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadata.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#include "PCGExInlineSettingsTypes.h"
#include "Tests/PCGExInlineSettingsTestSettings.h"

// These guard engine behaviour the struct relies on: each PCG graph site must see exactly one soft object path.

namespace PCGExInlineSettingsTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FName SettingsMemberName()
	{
		return GET_MEMBER_NAME_CHECKED(FPCGExInlineSettings, Settings);
	}

	UPCGExInlineSettingsTestSettings* NewTestSettings()
	{
		return NewObject<UPCGExInlineSettingsTestSettings>(GetTransientPackage(), NAME_None, RF_Transient);
	}

	/** Owner outside the transient package, where Resolve prefers the owned instance over the Settings path. */
	UPCGExInlineSettingsTestSettings* NewPackagedTestSettings()
	{
		return NewObject<UPCGExInlineSettingsTestSettings>(CreatePackage(TEXT("/Temp/PCGExInlineSettingsTests")), NAME_None, RF_Transient);
	}

	TArray<const FPCGSettingsOverridableParam*> FindParams(const UPCGSettings* InSettings, const FName InRootName)
	{
		TArray<const FPCGSettingsOverridableParam*> Params;
		for (const FPCGSettingsOverridableParam& Param : InSettings->OverridableParams())
		{
			if (!Param.PropertiesNames.IsEmpty() && Param.PropertiesNames[0] == InRootName)
			{
				Params.Add(&Param);
			}
		}
		return Params;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsOverridePinsTest, "PCGEx.InlineSettings.Sites.OverridePins", PCGExInlineSettingsTests::Flags)

bool FPCGExInlineSettingsOverridePinsTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsTests;

	const UPCGExInlineSettingsTestSettings* Owner = NewTestSettings();
	const FName RootName = GET_MEMBER_NAME_CHECKED(UPCGExInlineSettingsTestSettings, Inline);
	const TArray<const FPCGSettingsOverridableParam*> Params = FindParams(Owner, RootName);

	if (!TestEqual(TEXT("Override params exposed by the struct"), Params.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("The only override param is Inline/Settings"), Params[0]->PropertiesNames == TArray<FName>{RootName, SettingsMemberName()});

#if WITH_EDITORONLY_DATA
	TestTrue(TEXT("The override param is a soft object path"), Params[0]->UnderlyingType == EPCGMetadataTypes::SoftObjectPath);
#endif

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsGetGraphParameterTest, "PCGEx.InlineSettings.Sites.GetGraphParameter", PCGExInlineSettingsTests::Flags)

bool FPCGExInlineSettingsGetGraphParameterTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsTests;

	const FName ParameterName(TEXT("Param"));

	FPCGExInlineSettings Value(UPCGExInlineSettingsTestSettings::StaticClass());
	Value.SetInstance(NewTestSettings());

	FInstancedPropertyBag Bag;
	Bag.AddProperty(ParameterName, EPropertyBagPropertyType::Struct, FPCGExInlineSettings::StaticStruct());
	Bag.SetValueStruct(ParameterName, Value);

	// Same extractor setup as the Get Graph Parameter element with default settings.
	PCGPropertyHelpers::FExtractorParameters Extractor;
	Extractor.Container = Bag.GetValue().GetMemory();
	Extractor.Class = Bag.GetPropertyBagStruct();
	Extractor.PropertySelectors.Add(FPCGAttributePropertySelector::CreateAttributeSelector(ParameterName));
	Extractor.OutputAttributeName = ParameterName;

	const UPCGParamData* ParamData = PCGPropertyHelpers::ExtractPropertyAsAttributeSet(Extractor);
	if (!TestNotNull(TEXT("Extraction produced an attribute set"), ParamData))
	{
		return false;
	}

	TArray<FName> AttributeNames;
	TArray<EPCGMetadataTypes> AttributeTypes;
	ParamData->ConstMetadata()->GetAttributes(AttributeNames, AttributeTypes);

	if (!TestEqual(TEXT("Attributes extracted from the struct"), AttributeNames.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("The only attribute is Settings"), AttributeNames[0] == SettingsMemberName());
	TestTrue(TEXT("The attribute is a soft object path"), AttributeTypes[0] == EPCGMetadataTypes::SoftObjectPath);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsSubgraphPinsTest, "PCGEx.InlineSettings.Sites.SubgraphPins", PCGExInlineSettingsTests::Flags)

bool FPCGExInlineSettingsSubgraphPinsTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsTests;

	const FName ParameterName(TEXT("Param"));

	UPCGGraph* Graph = NewObject<UPCGGraph>(GetTransientPackage(), NAME_None, RF_Transient);
	Graph->AddUserParameters({FPropertyBagPropertyDesc(ParameterName, EPropertyBagPropertyType::Struct, FPCGExInlineSettings::StaticStruct())});

	UPCGSubgraphSettings* Subgraph = NewObject<UPCGSubgraphSettings>(GetTransientPackage(), NAME_None, RF_Transient);
	Subgraph->SetSubgraph(Graph);

	// Subgraph settings only rebuild their override params when SubgraphInstance is edited.
	FProperty* SubgraphInstanceProperty = FindFProperty<FProperty>(UPCGSubgraphSettings::StaticClass(), TEXT("SubgraphInstance"));
	if (!TestNotNull(TEXT("UPCGSubgraphSettings::SubgraphInstance exists"), SubgraphInstanceProperty))
	{
		return false;
	}

	FPropertyChangedEvent ChangedEvent(SubgraphInstanceProperty);
	static_cast<UObject*>(Subgraph)->PostEditChangeProperty(ChangedEvent);

	const TArray<const FPCGSettingsOverridableParam*> Params = FindParams(Subgraph, ParameterName);
	if (!TestEqual(TEXT("Subgraph pins exposed by the struct parameter"), Params.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("The only subgraph pin is Param/Settings"), Params[0]->PropertiesNames == TArray<FName>{ParameterName, SettingsMemberName()});

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsDuplicateSyncTest, "PCGEx.InlineSettings.Sync.Duplicate", PCGExInlineSettingsTests::Flags)

bool FPCGExInlineSettingsDuplicateSyncTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsTests;

	UPCGExInlineSettingsTestSettings* Owner = NewTestSettings();
	Owner->Inline.SetInstance(PCGExInlineSettings::CreateInstance(Owner, UPCGExInlineSettingsTestSettings::StaticClass()));

	const UPCGExInlineSettingsTestSettings* Copy = DuplicateObject<UPCGExInlineSettingsTestSettings>(Owner, GetTransientPackage());
	if (!TestNotNull(TEXT("Owner duplicated"), Copy) || !TestNotNull(TEXT("Inline instance duplicated"), Copy->Inline.Instance.Get()))
	{
		return false;
	}

	TestTrue(TEXT("The copy owns its own instance"), Copy->Inline.Instance->GetOuter() == Copy);
	TestTrue(TEXT("Settings follows the duplicated instance"), Copy->Inline.Settings == FSoftObjectPath(Copy->Inline.Instance));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsImportTextSyncTest, "PCGEx.InlineSettings.Sync.ImportText", PCGExInlineSettingsTests::Flags)

bool FPCGExInlineSettingsImportTextSyncTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsTests;

	UPCGExInlineSettingsTestSettings* Owner = NewTestSettings();

	FPCGExInlineSettings Source;
	Source.SetInstance(PCGExInlineSettings::CreateInstance(Owner, UPCGExInlineSettingsTestSettings::StaticClass()));
	Source.Settings = FSoftObjectPath(TEXT("/Engine/Transient.PCGExInlineSettingsStalePath"));

	const UScriptStruct* Struct = FPCGExInlineSettings::StaticStruct();

	FString Text;
	Struct->ExportText(Text, &Source, nullptr, Owner, PPF_None, nullptr);

	FPCGExInlineSettings Imported;
	const TCHAR* ImportResult = Struct->ImportText(*Text, &Imported, Owner, PPF_None, nullptr, Struct->GetName());
	if (!TestNotNull(TEXT("Text import succeeded"), ImportResult))
	{
		return false;
	}

	TestTrue(TEXT("Instance imported"), Imported.Instance == Source.Instance);
	TestTrue(TEXT("Stale Settings re-derived from the instance"), Imported.Settings == FSoftObjectPath(Source.Instance));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsCopyMatchingValuesTest, "PCGEx.InlineSettings.CopyMatchingValues", PCGExInlineSettingsTests::Flags)

bool FPCGExInlineSettingsCopyMatchingValuesTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsTests;

	UPCGExInlineSettingsTestSettings* Source = NewTestSettings();
	Source->Shared = 7;
	Source->Mismatched = 3;
	Source->Seed = 42;
	Source->Nested = NewObject<UPCGExInlineSettingsTestSettings>(Source, NAME_None, RF_Transient);
	Source->Nested->Seed = 99;
	Source->bEnabled = false;
	Source->Config.Strength = 9;
	Source->Config.OnlyA = 4;

	UPCGExInlineSettingsTestSiblingSettings* Target = NewObject<UPCGExInlineSettingsTestSiblingSettings>(GetTransientPackage(), NAME_None, RF_Transient);
	Target->Config.Scale = 0.5f;
	const int32 NumOverridableParams = Target->OverridableParams().Num();

	PCGExInlineSettings::CopyMatchingValues(Source, Target);

	TestEqual(TEXT("Config of another type: shared base member carried over"), Target->Config.Strength, 9);
	TestEqual(TEXT("Config of another type: untouched base member keeps the target's value"), Target->Config.Scale, 0.5f);
	TestTrue(TEXT("Config of another type: target-only member untouched"), Target->Config.OnlyB == TEXT("Default"));

	TestEqual(TEXT("Same name and type: carried over"), Target->Shared, 7);
	TestEqual(TEXT("Same name, different type: untouched"), Target->Mismatched, 0.0f);
	TestEqual(TEXT("Seed: carried over"), Target->Seed, 42);
	TestTrue(TEXT("Target-only property keeps its default"), Target->OnlyHere == TEXT("Default"));
	TestTrue(TEXT("Base plumbing (bEnabled) untouched"), Target->bEnabled);
	TestEqual(TEXT("Cached override params untouched"), Target->OverridableParams().Num(), NumOverridableParams);

	if (TestNotNull(TEXT("Instanced sub-object carried over"), Target->Nested.Get()))
	{
		TestTrue(TEXT("Sub-object is a copy owned by the target"), Target->Nested.Get() != Source->Nested.Get() && Target->Nested->GetOuter() == Target);
		TestEqual(TEXT("Sub-object values copied"), Target->Nested->Seed, 99);
	}

	// An untouched source value must not override the target's own default, sub-objects included.
	UPCGExInlineSettingsTestSettings* Untouched = NewTestSettings();
	UPCGExInlineSettingsTestSiblingSettings* Target2 = NewObject<UPCGExInlineSettingsTestSiblingSettings>(GetTransientPackage(), NAME_None, RF_Transient);
	Target2->Shared = 5;
	UPCGSettings* OwnNested = NewObject<UPCGExInlineSettingsTestSettings>(Target2, NAME_None, RF_Transient);
	Target2->Nested = OwnNested;
	PCGExInlineSettings::CopyMatchingValues(Untouched, Target2);
	TestEqual(TEXT("Default source value leaves the target alone"), Target2->Shared, 5);
	TestTrue(TEXT("Default (null) source sub-object leaves the target's own alone"), Target2->Nested.Get() == OwnNested);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsResolveStalePathTest, "PCGEx.InlineSettings.Resolve.StalePath", PCGExInlineSettingsTests::Flags)

bool FPCGExInlineSettingsResolveStalePathTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsTests;

	UPCGExInlineSettingsTestSettings* Archetype = NewPackagedTestSettings();
	Archetype->Inline.SetInstance(PCGExInlineSettings::CreateInstance(Archetype, UPCGExInlineSettingsTestSettings::StaticClass()));

	TestTrue(TEXT("An in-sync value resolves to its own instance"), Archetype->Inline.Resolve() == Archetype->Inline.Instance);

	// Subobject instancing hands a spawned instance its own copy but never re-derives the path.
	UPCGExInlineSettingsTestSettings* Spawned = NewPackagedTestSettings();
	Spawned->Inline.Instance = PCGExInlineSettings::CreateInstance(Spawned, UPCGExInlineSettingsTestSettings::StaticClass());
	Spawned->Inline.Settings = FSoftObjectPath(Archetype->Inline.Instance);

	// RF_Transient propagates to subobjects, so the execution-copy carve-out must test the package, not the flag.
	if (!TestTrue(TEXT("Precondition: the spawned instance carries RF_Transient"), Spawned->Inline.Instance->HasAnyFlags(RF_Transient)))
	{
		return false;
	}

	TestTrue(TEXT("Resolve returns the holder's own instance, not the archetype's"), Spawned->Inline.Resolve() == Spawned->Inline.Instance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsResolveExecutionCopyTest, "PCGEx.InlineSettings.Resolve.ExecutionCopy", PCGExInlineSettingsTests::Flags)

bool FPCGExInlineSettingsResolveExecutionCopyTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsTests;

	UPCGExInlineSettingsTestSettings* Persistent = NewPackagedTestSettings();
	const UPCGSettings* Overridden = PCGExInlineSettings::CreateInstance(Persistent, UPCGExInlineSettingsTestSettings::StaticClass());

	// Shape of a PCG execution copy: holder and nested instance duplicated into the transient package, Settings left
	// carrying the path an override pin wrote.
	UPCGExInlineSettingsTestSettings* Copy = NewTestSettings();
	Copy->Inline.Instance = PCGExInlineSettings::CreateInstance(Copy, UPCGExInlineSettingsTestSettings::StaticClass());
	Copy->Inline.Settings = FSoftObjectPath(Overridden);

	TestTrue(TEXT("Resolve honours the overridden path inside an execution copy"), Copy->Inline.Resolve() == Overridden);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsResolveExternalTest, "PCGEx.InlineSettings.Resolve.External", PCGExInlineSettingsTests::Flags)

bool FPCGExInlineSettingsResolveExternalTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsTests;

	UPCGExInlineSettingsTestSettings* Owner = NewPackagedTestSettings();
	UPCGSettings* Referenced = PCGExInlineSettings::CreateInstance(Owner, UPCGExInlineSettingsTestSettings::StaticClass());

	FPCGExInlineSettings Value;
	Value.SetInstance(PCGExInlineSettings::CreateInstance(Owner, UPCGExInlineSettingsTestSettings::StaticClass()));
	Value.SetExternal(TSoftObjectPtr<UPCGSettings>(Referenced));

	TestTrue(TEXT("Settings follows the external reference"), Value.Settings == FSoftObjectPath(Referenced));
	TestTrue(TEXT("Resolve returns the external reference over a leftover instance"), Value.Resolve() == Referenced);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
