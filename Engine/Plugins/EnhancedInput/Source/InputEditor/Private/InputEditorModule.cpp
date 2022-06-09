// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputEditorModule.h"

#include "AssetTypeActions_Base.h"
#include "EnhancedInputModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "PlayerMappableInputConfig.h"
#include "InputCustomizations.h"
#include "InputModifiers.h"
#include "ISettingsModule.h"
#include "K2Node_EnhancedInputAction.h"
#include "K2Node_GetInputActionValue.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "SSettingsEditorCheckoutNotice.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetTypeActions/AssetTypeActions_DataAsset.h"
#include "EnhancedInputDeveloperSettings.h"
#include "Styling/SlateStyle.h"
#include "Styling/StyleColors.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

#define LOCTEXT_NAMESPACE "InputEditor"

EAssetTypeCategories::Type FInputEditorModule::InputAssetsCategory;

IMPLEMENT_MODULE(FInputEditorModule, InputEditor)

// Asset factories

// InputContext
UInputMappingContext_Factory::UInputMappingContext_Factory(const class FObjectInitializer& OBJ) : Super(OBJ) {
	SupportedClass = UInputMappingContext::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UInputMappingContext_Factory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UInputMappingContext::StaticClass()));
	return NewObject<UInputMappingContext>(InParent, Class, Name, Flags | RF_Transactional, Context);
}

// InputAction
UInputAction_Factory::UInputAction_Factory(const class FObjectInitializer& OBJ)
	: Super(OBJ)
{
	SupportedClass = UInputAction::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UInputAction_Factory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UInputAction::StaticClass()));
	return NewObject<UInputAction>(InParent, Class, Name, Flags | RF_Transactional, Context);
}

// UPlayerMappableInputConfig_Factory
UPlayerMappableInputConfig_Factory::UPlayerMappableInputConfig_Factory(const class FObjectInitializer& OBJ)
	: Super(OBJ)
{
	SupportedClass = UPlayerMappableInputConfig::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UPlayerMappableInputConfig_Factory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UPlayerMappableInputConfig::StaticClass()));
	return NewObject<UPlayerMappableInputConfig>(InParent, Class, Name, Flags | RF_Transactional, Context);
}

//
//// InputTrigger
//UInputTrigger_Factory::UInputTrigger_Factory(const class FObjectInitializer& OBJ) : Super(OBJ) {
//	ParentClass = UInputTrigger::StaticClass();
//	SupportedClass = UInputTrigger::StaticClass();
//	bEditAfterNew = true;
//	bCreateNew = true;
//}
//
//// InputModifier
//UInputModifier_Factory::UInputModifier_Factory(const class FObjectInitializer& OBJ) : Super(OBJ) {
//	ParentClass = UInputModifier::StaticClass();
//	SupportedClass = UInputModifier::StaticClass();
//	bEditAfterNew = true;
//	bCreateNew = true;
//}



// Asset type actions
// TODO: Move asset type action definitions out?

class FAssetTypeActions_InputContext : public FAssetTypeActions_DataAsset
{
public:
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputMappingContext", "Input Mapping Context"); }
	virtual uint32 GetCategories() override { return FInputEditorModule::GetInputAssetsCategory(); }
	virtual FColor GetTypeColor() const override { return FColor(255, 255, 127); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputContextDesc", "A collection of device input to action mappings."); }
	virtual UClass* GetSupportedClass() const override { return UInputMappingContext::StaticClass(); }
};

class FAssetTypeActions_InputAction : public FAssetTypeActions_DataAsset
{
public:
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputAction", "Input Action"); }
	virtual uint32 GetCategories() override { return FInputEditorModule::GetInputAssetsCategory(); }
	virtual FColor GetTypeColor() const override { return FColor(127, 255, 255); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputActionDesc", "Represents an an abstract game action that can be mapped to arbitrary hardware input devices."); }
	virtual UClass* GetSupportedClass() const override { return UInputAction::StaticClass(); }
};

class FAssetTypeActions_PlayerMappableInputConfig : public FAssetTypeActions_DataAsset
{
public:
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_PlayerMappableInputConfig", "Player Mappable Input Config"); }
	virtual uint32 GetCategories() override { return FInputEditorModule::GetInputAssetsCategory(); }
	virtual FColor GetTypeColor() const override { return FColor(127, 255, 255); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_PlayerBindableInputConfigDesc", "Represents one set of Player Mappable controller/keymappings"); }
	virtual UClass* GetSupportedClass() const override { return UPlayerMappableInputConfig::StaticClass(); }
};

/** Custom style set for Enhanced Input */
class FEnhancedInputSlateStyle final : public FSlateStyleSet
{
public:
	FEnhancedInputSlateStyle()
		: FSlateStyleSet("EnhancedInputEditor")
	{
		SetParentStyleName(FAppStyle::GetAppStyleSetName());

		// The icons are located in /Engine/Plugins/EnhancedInput/Content/Editor/Slate/Icons
		SetContentRoot(FPaths::EnginePluginsDir() / TEXT("EnhancedInput/Content/Editor/Slate"));
		SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

		// Enhanced Input Editor icons
		static const FVector2D Icon16 = FVector2D(16.0f, 16.0f);
		static const FVector2D Icon64 = FVector2D(64.0f, 64.0f);

		Set("ClassIcon.InputAction", new IMAGE_BRUSH_SVG("Icons/InputAction_16", Icon16));
		Set("ClassThumbnail.InputAction", new IMAGE_BRUSH_SVG("Icons/InputAction_64", Icon64));
		
		Set("ClassIcon.InputMappingContext", new IMAGE_BRUSH_SVG("Icons/InputMappingContext_16", Icon16));
		Set("ClassThumbnail.InputMappingContext", new IMAGE_BRUSH_SVG("Icons/InputMappingContext_64", Icon64));
		
		Set("ClassIcon.PlayerMappableInputConfig", new IMAGE_BRUSH_SVG("Icons/PlayerMappableInputConfig_16", Icon16));
		Set("ClassThumbnail.PlayerMappableInputConfig", new IMAGE_BRUSH_SVG("Icons/PlayerMappableInputConfig_64", Icon64));			
	}
};

void FInputEditorModule::StartupModule()
{
	// Register customizations
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("InputMappingContext", FOnGetDetailCustomizationInstance::CreateStatic(&FInputContextDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("EnhancedActionKeyMapping", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FEnhancedActionMappingCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout(UEnhancedInputDeveloperSettings::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FEnhancedInputDeveloperSettingsCustomization::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();

	// Register input assets
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	InputAssetsCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("Input")), LOCTEXT("InputAssetsCategory", "Input"));
	{
		RegisterAssetTypeActions(AssetTools, MakeShareable(new FAssetTypeActions_InputAction));
		RegisterAssetTypeActions(AssetTools, MakeShareable(new FAssetTypeActions_InputContext));
		RegisterAssetTypeActions(AssetTools, MakeShareable(new FAssetTypeActions_PlayerMappableInputConfig));
		// TODO: Build these off a button on the InputContext Trigger/Mapping pickers? Would be good to have both.
		//RegisterAssetTypeActions(AssetTools, MakeShareable(new FAssetTypeActions_InputTrigger));
		//RegisterAssetTypeActions(AssetTools, MakeShareable(new FAssetTypeActions_InputModifier));
	}

	// Make a new style set for Enhanced Input, which will register any custom icons for the types in this plugin
	StyleSet = MakeShared<FEnhancedInputSlateStyle>();
	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FInputEditorModule::ShutdownModule()
{
	// Unregister input assets
	if (FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		for (TSharedPtr<IAssetTypeActions>& AssetAction : CreatedAssetTypeActions)
		{
			AssetToolsModule->Get().UnregisterAssetTypeActions(AssetAction.ToSharedRef());
		}
	}
	CreatedAssetTypeActions.Empty();

	// Unregister input settings
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Engine", "Enhanced Input");
	}

	// Unregister customizations
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomClassLayout("InputContext");
	PropertyModule.UnregisterCustomPropertyTypeLayout("EnhancedActionKeyMapping");
	PropertyModule.UnregisterCustomClassLayout("EnhancedInputDeveloperSettings");
	PropertyModule.NotifyCustomizationModuleChanged();

	// Unregister slate stylings
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
	}
}

void FInputEditorModule::Tick(float DeltaTime)
{
	// Update any blueprints that are referencing an input action with a modified value type
	if (UInputAction::ActionsWithModifiedValueTypes.Num())
	{
		TSet<UBlueprint*> BPsModified;
		for (TObjectIterator<UK2Node_EnhancedInputAction> NodeIt; NodeIt; ++NodeIt)
		{
			if (UInputAction::ActionsWithModifiedValueTypes.Contains(NodeIt->InputAction))
			{
				NodeIt->ReconstructNode();
				BPsModified.Emplace(NodeIt->GetBlueprint());
			}
		}
		for (TObjectIterator<UK2Node_GetInputActionValue> NodeIt; NodeIt; ++NodeIt)
		{
			if (UInputAction::ActionsWithModifiedValueTypes.Contains(NodeIt->InputAction))
			{
				NodeIt->ReconstructNode();
				BPsModified.Emplace(NodeIt->GetBlueprint());
			}
		}

		if (BPsModified.Num())
		{
			FNotificationInfo Info(FText::Format(LOCTEXT("ActionValueTypeChange", "Changing action value type affected {0} blueprint(s)!"), BPsModified.Num()));
			Info.ExpireDuration = 5.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}

		UInputAction::ActionsWithModifiedValueTypes.Reset();
	}
}

#undef LOCTEXT_NAMESPACE