// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SBasicFilterBar.h"
#include "Filters/CustomClassFilterData.h"

#include "IAssetTypeActions.h"
#include "AssetTypeCategories.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetToolsModule.h"
#include "UObject/Object.h"
#include "SlateGlobals.h"
#include "Logging/LogMacros.h"
#include "Styling/SlateIconFinder.h"

#include "SAssetFilterBar.generated.h"

#define LOCTEXT_NAMESPACE "FilterBar"

/** Delegate that subclasses can use to specify classes to not include in this filter
 * Returning false for a class will prevent it from showing up in the add filter dropdown
 */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnFilterAssetType, UClass* /* Test Class */);

/* Delegate used by SAssetFilterBar to populate the add filter menu */
DECLARE_DELEGATE_ThreeParams(FOnPopulateAddAssetFilterMenu, UToolMenu*, TSharedPtr<FFilterCategory>, FOnFilterAssetType)

/** ToolMenuContext that is used to create the Add Filter Menu */
UCLASS()
class EDITORWIDGETS_API UAssetFilterBarContext : public UObject
{
	GENERATED_BODY()

public:
	TSharedPtr<FFilterCategory> MenuExpansion;
	FOnPopulateAddAssetFilterMenu PopulateFilterMenu;
};


/**
* An Asset Filter Bar widget, which can be used to filter items of type [FilterType] given a list of custom filters along with built in support for AssetType filters
* @see SFilterBar in EditorWidgets, which is a simplified version of this widget for most use cases which is probably what you want to use
* @see SBasicFilterBar in ToolWidgets if you want a simple FilterBar without AssetType Filters (usable in non editor builds)
* @see SFilterList in ContentBrowser for an extremely complex example of how to use this widget, though for most cases you probably want to use SFilterBar
* NOTE: The filter functions create copies, so you want to use SAssetFilterBar<TSharedPtr<ItemType>> etc instead of SAssetFilterBar<ItemType> when possible
* NOTE: You will need to also add "ToolWidgets" as a dependency to your module to use this widget
* Sample Usage:
*		SAssignNew(MyFilterBar, SAssetFilterBar<FText>)
*		.OnFilterChanged() // A delegate for when the list of filters changes
*		.CustomFilters() // An array of filters available to this FilterBar (@see FGenericFilter to create simple delegate based filters)
*
* Use the GetAllActiveFilters() and GetCombinedBackendFilter() functions to get all the custom and asset filters respectively
* NOTE: GetCombinedBackendFilter returns an FARFilter, and it is on the user of this widget to compile it/use it to filter their items.
*		If you want more straightforward filtering, look at SFilterBar which streamlines this
* Use MakeAddFilterButton() to make the button that summons the dropdown showing all the filters
* Sample Usage:
*  void OnFilterChangedDelegate()
*  {
*		TSharedPtr< TFilterCollection<FilterType> > ActiveFilters = MyFilterBar->GetAllActiveFilters();
*		FARFilter CombinedBackEndFilter = MyFilterBar->GetCombinedBackendFilter()
*		TArray<FilterType> MyUnfilteredItems;
*		TArray<FilterType> FilteredItems;
*		
*		for(FilterType& MyItem : MyUnfilteredItems)
*		{
*			if(CompileAndRunFARFilter(CombinedBackEndFilter, MyItem) && ActiveFilters.PassesAllFilters(MyItem))
*			{
*				FilteredItems.Add(MyItem);
*			}
*		}
*  }
*/
template<typename FilterType>
class SAssetFilterBar : public SBasicFilterBar<FilterType>
{
public:
	
	using FOnFilterChanged = typename SBasicFilterBar<FilterType>::FOnFilterChanged;
	using FOnExtendAddFilterMenu = typename SBasicFilterBar<FilterType>::FOnExtendAddFilterMenu;

	SLATE_BEGIN_ARGS( SAssetFilterBar<FilterType> )
		: _UseDefaultAssetFilters(true)
		{
		
		}

		/** Delegate for when filters have changed */
		SLATE_EVENT( FOnFilterChanged, OnFilterChanged )
	
		/** Delegate to extend the Add Filter dropdown */
		SLATE_EVENT( FOnExtendAddFilterMenu, OnExtendAddFilterMenu )
	
		/** Initial List of Custom Filters that will be added to the AddFilter Menu */
		SLATE_ARGUMENT( TArray<TSharedRef<FFilterBase<FilterType>>>, CustomFilters)

		/** Initial List of Custom Class filters that will be added to the AddFilter Menu */
		SLATE_ARGUMENT( TArray<TSharedRef<FCustomClassFilterData>>, CustomClassFilters)

		/** Whether the filter bar should provide the default asset filters */
		SLATE_ARGUMENT(bool, UseDefaultAssetFilters)
	
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs )
	{
		bUseDefaultAssetFilters = InArgs._UseDefaultAssetFilters;
		CustomClassFilters = InArgs._CustomClassFilters;
		
		typename SBasicFilterBar<FilterType>::FArguments Args;
		Args._OnFilterChanged = InArgs._OnFilterChanged;
		Args._CustomFilters = InArgs._CustomFilters;
		Args._OnExtendAddFilterMenu = InArgs._OnExtendAddFilterMenu;
		
		SBasicFilterBar<FilterType>::Construct(Args);
		
		CreateAssetTypeActionFilters();
	}

protected:

	typedef typename SBasicFilterBar<FilterType>::SFilter SFilter;

	/** A subclass of SFilter in SBasicFilterBar to add functionality for Asset Filters */
	class SAssetFilter : public SFilter
	{
		using FOnRequestRemove = typename SBasicFilterBar<FilterType>::SFilter::FOnRequestRemove;
		using FOnRequestEnableOnly = typename SBasicFilterBar<FilterType>::SFilter::FOnRequestEnableOnly;
		using FOnRequestEnableAll = typename SBasicFilterBar<FilterType>::SFilter::FOnRequestEnableAll;
		using FOnRequestDisableAll = typename SBasicFilterBar<FilterType>::SFilter::FOnRequestDisableAll;
		using FOnRequestRemoveAll = typename SBasicFilterBar<FilterType>::SFilter::FOnRequestRemoveAll;
		using FOnRequestRemoveAllButThis = typename SBasicFilterBar<FilterType>::SFilter::FOnRequestRemoveAllButThis;
		
		SLATE_BEGIN_ARGS( SAssetFilter ){}

			/** The Custom Class Data that is associated with this filter */
			SLATE_ARGUMENT( TSharedPtr<FCustomClassFilterData>, CustomClassFilter )

			// SFilter Arguments
		
			/** If this is an front end filter, this is the filter object */
			SLATE_ARGUMENT( TSharedPtr<FFilterBase<FilterType>>, FrontendFilter )

			/** Invoked when the filter toggled */
			SLATE_EVENT( FOnFilterChanged, OnFilterChanged )

			/** Invoked when a request to remove this filter originated from within this filter */
			SLATE_EVENT( FOnRequestRemove, OnRequestRemove )

			/** Invoked when a request to enable only this filter originated from within this filter */
			SLATE_EVENT( FOnRequestEnableOnly, OnRequestEnableOnly )

			/** Invoked when a request to enable all filters originated from within this filter */
			SLATE_EVENT( FOnRequestEnableAll, OnRequestEnableAll)

			/** Invoked when a request to disable all filters originated from within this filter */
			SLATE_EVENT( FOnRequestDisableAll, OnRequestDisableAll )

			/** Invoked when a request to remove all filters originated from within this filter */
			SLATE_EVENT( FOnRequestRemoveAll, OnRequestRemoveAll )

			/** Invoked when a request to remove all filters originated from within this filter */
			SLATE_EVENT( FOnRequestRemoveAllButThis, OnRequestRemoveAllButThis )

		SLATE_END_ARGS()

		/** Constructs this widget with InArgs */
		void Construct( const FArguments& InArgs )
		{
			this->bEnabled = false;
			this->OnFilterChanged = InArgs._OnFilterChanged;
			this->OnRequestRemove = InArgs._OnRequestRemove;
			this->OnRequestEnableOnly = InArgs._OnRequestEnableOnly;
			this->OnRequestEnableAll = InArgs._OnRequestEnableAll;
			this->OnRequestDisableAll = InArgs._OnRequestDisableAll;
			this->OnRequestRemoveAll = InArgs._OnRequestRemoveAll;
			this->OnRequestRemoveAllButThis = InArgs._OnRequestRemoveAllButThis;
			this->FrontendFilter = InArgs._FrontendFilter;

			CustomClassFilter = InArgs._CustomClassFilter;
			
			// Get the tooltip and color of the type represented by this filter
			this->FilterColor = FLinearColor::White;
			if ( CustomClassFilter.IsValid() )
			{
				this->FilterColor = CustomClassFilter->GetColor();
				// No tooltip for asset type filters
			}
			else if ( this->FrontendFilter.IsValid() )
			{
				this->FilterColor = this->FrontendFilter->GetColor();
				this->FilterToolTip = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this->FrontendFilter.ToSharedRef(), &FFilterBase<FilterType>::GetToolTipText));
			}

			SFilter::Construct_Internal();
		}

	public:
		
		/** Returns this widgets contribution to the combined filter */
		FARFilter GetBackendFilter() const
		{
			FARFilter Filter;

			if(CustomClassFilter)
			{
				CustomClassFilter->BuildBackendFilter(Filter);
			}

			return Filter;
		}
		
		/** Gets the asset type actions associated with this filter */
		const TSharedPtr<FCustomClassFilterData>& GetCustomClassFilterData() const
		{
			return CustomClassFilter;
		}

		/** Returns the display name for this filter */
		virtual FText GetFilterDisplayName() const override
		{
			if (CustomClassFilter.IsValid())
			{
				return CustomClassFilter->GetName();
			}
			else
			{
				return SFilter::GetFilterDisplayName();
			}

		}

		virtual FString GetFilterName() const override
		{
			if (CustomClassFilter.IsValid())
			{
				return CustomClassFilter->GetFilterName();
			}
			else
			{
				return SFilter::GetFilterName();
			}
		}
		
	protected:
		/** The asset type actions that are associated with this filter */
		TSharedPtr<FCustomClassFilterData> CustomClassFilter;
	};

public:

	/** Use this function to get an FARFilter that represents all the AssetType filters that are currently active */
	FARFilter GetCombinedBackendFilter() const
	{
		FARFilter CombinedFilter;

		// Add all selected filters
		for (int32 FilterIdx = 0; FilterIdx < AssetFilters.Num(); ++FilterIdx)
		{
			const TSharedPtr<SAssetFilter> AssetFilter = AssetFilters[FilterIdx];
			
			if ( AssetFilter.IsValid() && AssetFilter->IsEnabled())
			{
				CombinedFilter.Append(AssetFilter->GetBackendFilter());
			}
		}

		if ( CombinedFilter.bRecursiveClasses )
		{
			// Add exclusions for AssetTypeActions NOT in the filter.
			// This will prevent assets from showing up that are both derived from an asset in the filter set and derived from an asset not in the filter set
			// Get the list of all asset type actions
			for(const TSharedRef<FCustomClassFilterData> &CustomClassFilter : CustomClassFilters)
			{
				const UClass* TypeClass = CustomClassFilter->GetClass();
				if (TypeClass && !CombinedFilter.ClassPaths.Contains(TypeClass->GetClassPathName()))
				{
					CombinedFilter.RecursiveClassPathsExclusionSet.Add(TypeClass->GetClassPathName());
				}
			}
		}

		// HACK: A blueprint can be shown as Blueprint or as BlueprintGeneratedClass, but we don't want to distinguish them while filtering.
		// This should be removed, once all blueprints are shown as BlueprintGeneratedClass.
		if(CombinedFilter.ClassPaths.Contains(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint"))))
		{
			CombinedFilter.ClassPaths.AddUnique(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("BlueprintGeneratedClass")));
		}

		return CombinedFilter;
	}

	/** Check if there is a filter associated with the given class represented by FTopLevelAssetPath in the filter bar
	 * @param	InClassPath		The Class the filter is associated with
	 */
	bool DoesAssetTypeFilterExist(const FTopLevelAssetPath& InClassPath)
	{
		for(const TSharedRef<FCustomClassFilterData>& CustomClassFilterData : CustomClassFilters)
		{
			if(CustomClassFilterData->GetClassPathName() == InClassPath)
			{
				return true;
			}
		}

		return false;
	}

	/** Set the check box state of the specified filter (in the filter drop down) and pin/unpin a filter widget on/from the filter bar. When a filter is pinned (was not already pinned), it is activated if requested and deactivated when unpinned.
	 * @param	InClassPath		The Class the filter is associated with (must exist in the widget)
	 * @param	InCheckState	The CheckState to apply to the flter
	 * @param	bEnableFilter	Whether the filter should be enabled when it is pinned
	 */
	void SetAssetTypeFilterCheckState(const FTopLevelAssetPath& InClassPath, ECheckBoxState InCheckState, bool bEnableFilter = true)
	{
		for(const TSharedRef<FCustomClassFilterData>& CustomClassFilterData : CustomClassFilters)
		{
			if(CustomClassFilterData->GetClassPathName() == InClassPath)
			{
				bool FilterChecked = IsClassTypeInUse(CustomClassFilterData);

				if (InCheckState == ECheckBoxState::Checked && !FilterChecked)
				{
					TSharedRef<SFilter> NewFilter = AddAssetFilterToBar(CustomClassFilterData);

					if(bEnableFilter)
					{
						NewFilter->SetEnabled(true);
					}
				}
				else if (InCheckState == ECheckBoxState::Unchecked && FilterChecked)
				{
					RemoveAssetFilter(CustomClassFilterData); // Unpin the filter widget and deactivate the filter.
				}
				// else -> Already in the desired 'check' state.
				
			}
		}
	}

	/** Returns the check box state of the specified filter (in the filter drop down). This tells whether the filter is pinned or not on the filter bar, but not if filter is active or not.
	 * @see		IsFilterActive().
	 * @param	InClassPath		The Class the filter is associated with
	 * @return The CheckState if a filter associated with the class name is in the filter bar, ECheckBoxState::Undetermined otherwise
	 */
	
	ECheckBoxState GetAssetTypeFilterCheckState(const FTopLevelAssetPath& InClassPath) const
	{
		for(const TSharedRef<FCustomClassFilterData>& CustomClassFilterData : CustomClassFilters)
		{
			if(CustomClassFilterData->GetClassPathName() == InClassPath)
			{
				return IsClassTypeInUse(CustomClassFilterData) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; 
			}
		}

		return ECheckBoxState::Undetermined;
	}

	/** Returns true if the specified filter is both checked (pinned on the filter bar) and active (contributing to filter the result).
	 *  @param	InClassPath		The Class the filter is associated with
	 */
	bool IsAssetTypeFilterActive(const FTopLevelAssetPath& InClassPath) const
	{
		for(const TSharedPtr<SAssetFilter> AssetFilter : AssetFilters)
		{
			if(AssetFilter->GetCustomClassFilterData()->GetClassPathName() == InClassPath)
			{
				return AssetFilter->IsEnabled();
			}
		}
		
		return false;
	}

	/** If a filter with the input class name is Checked (i.e visible in the bar), enable/disable it
	 * @see SetFilterCheckState to set the check state and GetFilterCheckState to check if it is checked
	 * @param	InClassPath		The Class the filter is associated with
	 * @param	bEnable			Whether to enable or disable the filter
	 */
	void ToggleAssetTypeFilterEnabled(const FTopLevelAssetPath& InClassPath, bool bEnable)
	{
		for(const TSharedPtr<SAssetFilter> AssetFilter : AssetFilters)
		{
			if(AssetFilter->GetCustomClassFilterData()->GetClassPathName() == InClassPath)
			{
				AssetFilter->SetEnabled(bEnable);
			}
		}
	}

	/** Remove all filters from the filter bar, while disabling any active ones */
	virtual void RemoveAllFilters() override
	{
		AssetFilters.Empty();
		SBasicFilterBar<FilterType>::RemoveAllFilters();
	}

protected:

	/** AssetFilter specific override to SBasicFilterBar::RemoveAllButThis */
	virtual void RemoveAllButThis(const TSharedRef<SFilter>& FilterToKeep) override
	{
		TSharedPtr<SAssetFilter> AssetFilterToKeep;

		// Make sure to keep it in our local list of AssetFilters
		for(const TSharedPtr<SAssetFilter> AssetFilter : AssetFilters)
		{
			if(AssetFilter == FilterToKeep)
			{
				AssetFilterToKeep = AssetFilter;
			}
		}
		
		SBasicFilterBar<FilterType>::RemoveAllButThis(FilterToKeep);

		AssetFilters.Empty();

		if(AssetFilterToKeep)
		{
			AssetFilters.Add(AssetFilterToKeep.ToSharedRef());
		}
	}

	/** Add an AssetFilter to the toolbar, making it "Active" but not enabled */
	TSharedRef<SFilter> AddAssetFilterToBar(const TSharedPtr<FCustomClassFilterData>& CustomClassFilter)
	{
		TSharedRef<SAssetFilter> NewFilter =
			SNew(SAssetFilter)
			.CustomClassFilter(CustomClassFilter)
			.OnFilterChanged(this->OnFilterChanged)
			.OnRequestRemove(this, &SAssetFilterBar<FilterType>::RemoveFilterAndUpdate)
			.OnRequestEnableOnly(this, &SAssetFilterBar<FilterType>::EnableOnlyThisFilter)
			.OnRequestEnableAll(this, &SAssetFilterBar<FilterType>::EnableAllFilters)
			.OnRequestDisableAll(this, &SAssetFilterBar<FilterType>::DisableAllFilters)
			.OnRequestRemoveAll(this, &SAssetFilterBar<FilterType>::RemoveAllFilters)
			.OnRequestRemoveAllButThis(this, &SAssetFilterBar<FilterType>::RemoveAllButThis);

		this->AddFilterToBar( NewFilter );

		// Add it to our list of just AssetFilters
		AssetFilters.Add(NewFilter);
		
		return NewFilter;
	}
	
	/** Remove a filter from the filter bar */
	virtual void RemoveFilter(const TSharedRef<SFilter>& FilterToRemove) override
	{
		SBasicFilterBar<FilterType>::RemoveFilter(FilterToRemove);

		AssetFilters.RemoveAll([&FilterToRemove](TSharedRef<SAssetFilter>& AssetFilter) { return AssetFilter == FilterToRemove; });
	}

	/** Handler for when the remove filter button was clicked on a filter */
	void RemoveAssetFilter(const TSharedPtr<FCustomClassFilterData>& CustomClassData, bool ExecuteOnFilterChanged = true)
	{
		TSharedPtr<SAssetFilter> FilterToRemove;
		for ( const TSharedPtr<SAssetFilter> AssetFilter : AssetFilters )
		{
			const TSharedPtr<FCustomClassFilterData>& ClassData = AssetFilter->GetCustomClassFilterData();
			if (ClassData == CustomClassData)
			{
				FilterToRemove = AssetFilter;
				break;
			}
		}

		if ( FilterToRemove.IsValid() )
		{
			if (ExecuteOnFilterChanged)
			{
				this->RemoveFilterAndUpdate(FilterToRemove.ToSharedRef());
			}
			else
			{
				this->RemoveFilter(FilterToRemove.ToSharedRef());
			}

			// Remove it from our local list of AssetFilters
			AssetFilters.Remove(FilterToRemove.ToSharedRef());
		}
	}

	/** Create the default set of IAssetTypeActions Filters provided with the widget if requested */
	void CreateAssetTypeActionFilters()
	{
		if(!bUseDefaultAssetFilters)
		{
			return;
		}

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

		AssetFilterCategories.Empty();

		// Add the Basic category
		AssetFilterCategories.Add(EAssetTypeCategories::Basic, MakeShareable(new FFilterCategory(LOCTEXT("BasicFilter", "Basic"), LOCTEXT("BasicFilterTooltip", "Filter by basic assets."))));

		// Add the advanced categories
		TArray<FAdvancedAssetCategory> AdvancedAssetCategories;
		AssetToolsModule.Get().GetAllAdvancedAssetCategories(/*out*/ AdvancedAssetCategories);

		for (const FAdvancedAssetCategory& AdvancedAssetCategory : AdvancedAssetCategories)
		{
			const FText Tooltip = FText::Format(LOCTEXT("WildcardFilterTooltip", "Filter by {0} Assets."), AdvancedAssetCategory.CategoryName);
			AssetFilterCategories.Add(AdvancedAssetCategory.CategoryType, MakeShareable(new FFilterCategory(AdvancedAssetCategory.CategoryName, Tooltip)));
		}

		// Get the browser type maps
		TArray<TWeakPtr<IAssetTypeActions>> AssetTypeActionsList;
		AssetToolsModule.Get().GetAssetTypeActionsList(AssetTypeActionsList);
		
		// Sort the list
		struct FCompareIAssetTypeActions
		{
			FORCEINLINE bool operator()( const TWeakPtr<IAssetTypeActions>& A, const TWeakPtr<IAssetTypeActions>& B ) const
			{
				return A.Pin()->GetName().CompareTo( B.Pin()->GetName() ) == -1;
			}
		};
		AssetTypeActionsList.Sort( FCompareIAssetTypeActions() );

		const TSharedRef<FPathPermissionList>& AssetClassPermissionList = AssetToolsModule.Get().GetAssetClassPathPermissionList(EAssetClassAction::CreateAsset);

		// For every asset type, convert it to an FCustomClassFilterData and add it to the list
		for (int32 ClassIdx = 0; ClassIdx < AssetTypeActionsList.Num(); ++ClassIdx)
		{
			const TWeakPtr<IAssetTypeActions>& WeakTypeActions = AssetTypeActionsList[ClassIdx];
			if ( WeakTypeActions.IsValid() )
			{
				TSharedPtr<IAssetTypeActions> TypeActions = WeakTypeActions.Pin();
				if ( ensure(TypeActions.IsValid()) && TypeActions->CanFilter() )
				{
					UClass* SupportedClass = TypeActions->GetSupportedClass();
					if ((!SupportedClass || AssetClassPermissionList->PassesFilter(SupportedClass->GetPathName())))
					{
						// Convert the AssetTypeAction to an FCustomClassFilterData and add it to our list
						TSharedRef<FCustomClassFilterData> CustomClassFilterData = MakeShared<FCustomClassFilterData>(TypeActions);
						CustomClassFilters.Add(CustomClassFilterData);
					}
				}
			}
		}
		
		// Do a second pass through all the CustomClassFilters with AssetTypeActions to update their categories
		UpdateAssetTypeActionCategories();

	}

	void UpdateAssetTypeActionCategories()
	{
		for(const TSharedRef<FCustomClassFilterData> &CustomClassFilter : CustomClassFilters)
		{
			for (auto MenuIt = AssetFilterCategories.CreateIterator(); MenuIt; ++MenuIt)
			{
				if(TSharedPtr<IAssetTypeActions> AssetTypeActions = CustomClassFilter->GetAssetTypeActions())
				{
					if(MenuIt.Key() & AssetTypeActions->GetCategories())
					{
						CustomClassFilter->AddCategory(MenuIt.Value());
					}
				}
				
			}
		}	
	}

	
	/* Filter Dropdown Related Functionality */

	/** Handler for when the add filter menu is populated by a category */
	void CreateFiltersMenuCategory(FToolMenuSection& Section, const TArray<TSharedPtr<FCustomClassFilterData>> CustomClassFilterDatas) const
	{
		for (int32 ClassIdx = 0; ClassIdx < CustomClassFilterDatas.Num(); ++ClassIdx)
		{
			const TSharedPtr<FCustomClassFilterData>& CustomClassFilterData = CustomClassFilterDatas[ClassIdx];
			
			const FText& LabelText = CustomClassFilterData->GetName();
			Section.AddMenuEntry(
				NAME_None,
				LabelText,
				FText::Format( LOCTEXT("FilterByTooltipPrefix", "Filter by {0}"), LabelText ),
				FSlateIconFinder::FindIconForClass(CustomClassFilterData->GetClass()),
				FUIAction(
					FExecuteAction::CreateSP( const_cast< SAssetFilterBar<FilterType>* >(this), &SAssetFilterBar<FilterType>::FilterByTypeClicked, CustomClassFilterData ),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &SAssetFilterBar<FilterType>::IsClassTypeInUse, CustomClassFilterData ) ),
				EUserInterfaceActionType::ToggleButton
				);
		}
	}
	
	void CreateFiltersMenuCategory(UToolMenu* InMenu, const TArray<TSharedPtr<FCustomClassFilterData>> CustomClassFilterDatas) const
	{
		CreateFiltersMenuCategory(InMenu->AddSection("Section"), CustomClassFilterDatas);
	}

	/** Handler for when the add filter button was clicked */
	virtual TSharedRef<SWidget> MakeAddFilterMenu() override
	{
		const FName FilterMenuName = "FilterBar.FilterMenu";
		if (!UToolMenus::Get()->IsMenuRegistered(FilterMenuName))
		{
			UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(FilterMenuName);
			Menu->bShouldCloseWindowAfterMenuSelection = true;
			Menu->bCloseSelfOnly = true;

			Menu->AddDynamicSection(NAME_None, FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
			{
				if (UAssetFilterBarContext* Context = InMenu->FindContext<UAssetFilterBarContext>())
				{
					Context->PopulateFilterMenu.ExecuteIfBound(InMenu, Context->MenuExpansion, FOnFilterAssetType());
				}
			}));
		}

		UAssetFilterBarContext* FilterBarContext = NewObject<UAssetFilterBarContext>();
		FilterBarContext->PopulateFilterMenu = FOnPopulateAddAssetFilterMenu::CreateSP(this, &SAssetFilterBar<FilterType>::PopulateAddFilterMenu);

		/** Auto expand the Basic Category if it is present */
		if(TSharedPtr<FFilterCategory>* BasicCategory = AssetFilterCategories.Find(EAssetTypeCategories::Basic))
		{
			FilterBarContext->MenuExpansion = *BasicCategory;
		}
	
		FToolMenuContext ToolMenuContext(FilterBarContext);

		return UToolMenus::Get()->GenerateWidget(FilterMenuName, ToolMenuContext);
	} 
	
	/** Handler to Populate the Add Filter Menu. Use OnFilterAssetType in Subclasses to add classes to the exclusion list */
	virtual void PopulateAddFilterMenu(UToolMenu* Menu, TSharedPtr<FFilterCategory> MenuExpansion, FOnFilterAssetType OnFilterAssetType)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

		// A local struct to describe a category in the filter menu
		struct FCategoryMenu
		{
			/** The Classes that belong to this category */
			TArray<TSharedPtr<FCustomClassFilterData>> Classes;

			//Menu section
			FName SectionExtensionHook;
			FText SectionHeading;

			FCategoryMenu(const FName& InSectionExtensionHook, const FText& InSectionHeading)
				: SectionExtensionHook(InSectionExtensionHook)
				, SectionHeading(InSectionHeading)
			{}
		};
		
		// Create a map of Categories to Menus
		TMap<TSharedPtr<FFilterCategory>, FCategoryMenu> CategoryToMenuMap;
		
		// For every asset type, move it into all the categories it should appear in
		for(const TSharedRef<FCustomClassFilterData> &CustomClassFilter : CustomClassFilters)
		{
			bool bPassesExternalFilters = true;

			// Run any external class filters we have
			if(OnFilterAssetType.IsBound())
			{
				bPassesExternalFilters = OnFilterAssetType.Execute(CustomClassFilter->GetClass());
			}

			if(bPassesExternalFilters)
			{
				/** Get all the categories this filter belongs to */
				TArray<TSharedPtr<FFilterCategory>> Categories = CustomClassFilter->GetCategories();

				for(const TSharedPtr<FFilterCategory>& Category : Categories)
				{
					// If the category for this custom class already exists
					if(FCategoryMenu* CategoryMenu = CategoryToMenuMap.Find(Category))
					{
						CategoryMenu->Classes.Add( CustomClassFilter );
					}
					// Otherwise create a new FCategoryMenu for the category and add it to the map
					else
					{
						const FName ExtensionPoint = NAME_None;
						const FText SectionHeading = FText::Format(LOCTEXT("WildcardFilterHeadingHeadingTooltip", "{0} Filters"), Category->Title);

						FCategoryMenu NewCategoryMenu(ExtensionPoint, SectionHeading);
						NewCategoryMenu.Classes.Add(CustomClassFilter);
					
						CategoryToMenuMap.Add(Category, NewCategoryMenu);
					}
				}
			}
		}

		// Remove any empty categories
		for (auto MenuIt = CategoryToMenuMap.CreateIterator(); MenuIt; ++MenuIt)
		{
			if (MenuIt.Value().Classes.Num() == 0)
			{
				CategoryToMenuMap.Remove(MenuIt.Key());
			}
		}

		// Set the extension hook for the basic category, if it exists and we have any assets for it
		if(TSharedPtr<FFilterCategory>* BasicCategory = AssetFilterCategories.Find(EAssetTypeCategories::Basic))
		{
			if(FCategoryMenu* BasicMenu = CategoryToMenuMap.Find(*BasicCategory))
			{
				BasicMenu->SectionExtensionHook = "FilterBarFilterBasicAsset";
			}
		}

		// Populate the common filter sections (Reset Filters etc)
		{
			this->PopulateCommonFilterSections(Menu);
		}
		
		// If we want to expand a category
		if(MenuExpansion)
		{
			// First add the expanded category, this appears as standard entries in the list (Note: intentionally not using FindChecked here as removing it from the map later would cause the ref to be garbage)
			FCategoryMenu* ExpandedCategory = CategoryToMenuMap.Find( MenuExpansion );
			if(ExpandedCategory)
			{
				FToolMenuSection& Section = Menu->AddSection(ExpandedCategory->SectionExtensionHook, ExpandedCategory->SectionHeading);
				if(MenuExpansion == AssetFilterCategories.FindChecked(EAssetTypeCategories::Basic))
				{
					// If we are doing a full menu (i.e expanding basic) we add a menu entry which toggles all other categories
					Section.AddMenuEntry(
						NAME_None,
						MenuExpansion->Title,
						MenuExpansion->Tooltip,
						FSlateIcon(FAppStyle::Get().GetStyleSetName(), "PlacementBrowser.Icons.Basic"),
						FUIAction(
						FExecuteAction::CreateSP( this, &SAssetFilterBar<FilterType>::FilterByTypeCategoryClicked, MenuExpansion, ExpandedCategory->Classes ),
						FCanExecuteAction(),
						FGetActionCheckState::CreateSP(this, &SAssetFilterBar<FilterType>::IsTypeCategoryChecked, MenuExpansion, ExpandedCategory->Classes ) ),
						EUserInterfaceActionType::ToggleButton
						);
				}

				// Now populate with all the assets from the expanded category
				SAssetFilterBar<FilterType>::CreateFiltersMenuCategory( Section, ExpandedCategory->Classes);
				
				// Remove the Expanded from the map now, as this is treated differently and is no longer needed.
				CategoryToMenuMap.Remove(MenuExpansion);
			}
		}

		TSharedPtr<FFilterCategory>* BasicCategory = AssetFilterCategories.Find(EAssetTypeCategories::Basic);

		// We are in Full Menu Mode if there is no menu expansion, or the menu expansion is EAssetTypeCategories::Basic
		bool bInFullMenuMode = !MenuExpansion || (BasicCategory && MenuExpansion == *BasicCategory);

		// If we are in full menu mode, add all the other categories as submenus
		if(bInFullMenuMode)
		{
			FToolMenuSection& Section = Menu->AddSection("AssetFilterBarFilterAdvancedAsset", LOCTEXT("AdvancedAssetsMenuHeading", "Other Assets"));
			
			// Sort by category name so that we add the submenus in alphabetical order
			CategoryToMenuMap.KeySort([](const TSharedPtr<FFilterCategory>& A, const TSharedPtr<FFilterCategory>& B) {
				return A->Title.CompareTo(B->Title) < 0;
			});

			// For all the remaining categories, add them as submenus
			for (const TPair<TSharedPtr<FFilterCategory>, FCategoryMenu>& CategoryMenuPair : CategoryToMenuMap)
			{
				Section.AddSubMenu(
					NAME_None,
					CategoryMenuPair.Key->Title,
					CategoryMenuPair.Key->Tooltip,
					FNewToolMenuDelegate::CreateSP(this, &SAssetFilterBar<FilterType>::CreateFiltersMenuCategory, CategoryMenuPair.Value.Classes),
					FUIAction(
					FExecuteAction::CreateSP(this, &SAssetFilterBar<FilterType>::FilterByTypeCategoryClicked, CategoryMenuPair.Key, CategoryMenuPair.Value.Classes),
					FCanExecuteAction(),
					FGetActionCheckState::CreateSP(this, &SAssetFilterBar<FilterType>::IsTypeCategoryChecked, CategoryMenuPair.Key, CategoryMenuPair.Value.Classes)),
					EUserInterfaceActionType::ToggleButton
					);
			}
		}

		// Now add all non-asset filters
		this->PopulateCustomFilters(Menu);
	}

	/* Asset Type filter related functionality */
	
	/** Handler for when filter by type is selected */
 	void FilterByTypeClicked(TSharedPtr<FCustomClassFilterData> CustomClassFilterData)
	{
		if (CustomClassFilterData.IsValid())
		{
			if (IsClassTypeInUse(CustomClassFilterData))
			{
				RemoveAssetFilter(CustomClassFilterData);
			}
			else
			{
				TSharedRef<SFilter> NewFilter = AddAssetFilterToBar(CustomClassFilterData);
				NewFilter->SetEnabled(true);
			}
		}
	}

	/** Handler to determine the "checked" state of class filter in the filter dropdown */
	bool IsClassTypeInUse(TSharedPtr<FCustomClassFilterData> Class) const
	{
		for (const TSharedPtr<SAssetFilter> AssetFilter : this->AssetFilters)
		{
			if (AssetFilter.IsValid() && AssetFilter->GetCustomClassFilterData() == Class)
			{
				return true;
			}
		}

		return false;
	}

 	/** Handler for when filter by type category is selected */
 	void FilterByTypeCategoryClicked(TSharedPtr<FFilterCategory> TypeCategory, TArray<TSharedPtr<FCustomClassFilterData>> Classes)
	{
		bool bFullCategoryInUse = IsTypeCategoryInUse(TypeCategory, Classes);
		bool ExecuteOnFilterChanged = false;

		for(const TSharedPtr<FCustomClassFilterData>& CustomClass : Classes)
		{
			if(bFullCategoryInUse)
			{
				RemoveAssetFilter(CustomClass);
				ExecuteOnFilterChanged = true;
			}
			else if(!IsClassTypeInUse(CustomClass))
			{
				TSharedRef<SFilter> NewFilter = AddAssetFilterToBar(CustomClass);
				NewFilter->SetEnabled(true, false);
				ExecuteOnFilterChanged = true;
			}
		}

		if (ExecuteOnFilterChanged)
		{
			this->OnFilterChanged.ExecuteIfBound();
		}
	}

 	/** Handler to determine the "checked" state of an type category in the filter dropdown */
 	ECheckBoxState IsTypeCategoryChecked(TSharedPtr<FFilterCategory> TypeCategory, TArray<TSharedPtr<FCustomClassFilterData>> Classes) const
	{
		bool bIsAnyActionInUse = false;
		bool bIsAnyActionNotInUse = false;

		for (const TSharedPtr<FCustomClassFilterData>& CustomClassFilter : Classes)
		{
			if (IsClassTypeInUse(CustomClassFilter))
			{
				bIsAnyActionInUse = true;
			}
			else
			{
				bIsAnyActionNotInUse = true;
			}

			if (bIsAnyActionInUse && bIsAnyActionNotInUse)
			{
				return ECheckBoxState::Undetermined;
			}
			
		}

		if (bIsAnyActionInUse)
		{
			return ECheckBoxState::Checked;
		}
		else
		{
			return ECheckBoxState::Unchecked;
		}
	}

 	/** Function to check if a given type category is in use */
 	bool IsTypeCategoryInUse(TSharedPtr<FFilterCategory> TypeCategory, TArray<TSharedPtr<FCustomClassFilterData>> Classes) const
 	{
 		ECheckBoxState AssetTypeCategoryCheckState = IsTypeCategoryChecked(TypeCategory, Classes);

 		if (AssetTypeCategoryCheckState == ECheckBoxState::Unchecked)
 		{
 			return false;
 		}

 		// An asset type category is in use if any of its type actions are in use (ECheckBoxState::Checked or ECheckBoxState::Undetermined)
 		return true;
 	}
	
protected:
	/** A copy of all AssetFilters in this->Filters for convenient access */
	TArray< TSharedRef<SAssetFilter> > AssetFilters;

	/** List of custom Class Filters that will be shown in the filter bar */
	TArray<TSharedRef<FCustomClassFilterData>> CustomClassFilters;

	TMap<EAssetTypeCategories::Type, TSharedPtr<FFilterCategory>> AssetFilterCategories;
	
	/** Whether the filter bar provides the default Asset Filters */
	bool bUseDefaultAssetFilters;

};

#undef LOCTEXT_NAMESPACE