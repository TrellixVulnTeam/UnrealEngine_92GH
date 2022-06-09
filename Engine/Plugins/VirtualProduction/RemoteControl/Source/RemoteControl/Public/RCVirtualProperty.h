﻿// Copyright Epic Games, Inc. All Rights Reserved.
 
#pragma once

#include "PropertyBag.h"
#include "UObject/Object.h"
 
#include "RCVirtualProperty.generated.h"

class FStructOnScope;
class IStructSerializerBackend;
class IStructDeserializerBackend;
class URemoteControlPreset;
class URCVirtualPropertyContainerBase;

/**
 * Base class for dynamic virtual properties
 * Remote Control Virtual Properties using Property Bag and FInstancedPropertyBag to serialize FProperties values and UStruct 
 */
UCLASS(BlueprintType)
class REMOTECONTROL_API URCVirtualPropertyBase : public UObject
{
	GENERATED_BODY()

	friend struct FRCVirtualPropertyCastHelpers;
	friend class URCVirtualPropertySelfContainer;

protected:
	/** Return pointer to memory const container */
	virtual const uint8* GetContainerPtr() const;

	/** Return pointer to memory container */
	virtual uint8* GetContainerPtr();

	/** Return pointer to const value */
	virtual const uint8* GetValuePtr() const;

	/** Return pointer to value */
	virtual uint8* GetValuePtr();

	/** Return Pointer to Bag Property Description */
	virtual const FPropertyBagPropertyDesc* GetBagPropertyDesc() const;

	/** Pointer to Instanced property which is holds  bag of properties */
	virtual const FInstancedPropertyBag* GetPropertyBagInstance() const;

	virtual void OnModifyPropertyValue() {}

public:
	/** Returns const FProperty for this RC virtual property */
	virtual const FProperty* GetProperty() const;

	/** Returns FProperty for this RC virtual property */
	virtual FProperty* GetProperty();
	
	/** Return property bag property type */
	EPropertyBagPropertyType GetValueType() const;

	/** Return pointer to object that defines the Enum, Struct, or Class. */
	const UObject* GetValueTypeObjectWeakPtr() const;

	/**
	 * Serialize Virtual Property to given Backend
	 *
	 * @param OutBackend Struct Serialize Backend 
	 */
	void SerializeToBackend(IStructSerializerBackend& OutBackend);

	/**
	 * Deserialize Virtual Property from a given Backend
	 *
	 * @param InBackend  - Deserializer containing a payload with value data
	 */
	bool DeserializeFromBackend(IStructDeserializerBackend& InBackend);

	/** Compare this virtual property value with given property value */
	bool IsValueEqual(URCVirtualPropertyBase* InVirtualProperty);

	/** Copy this virtual property's data onto a given FProperty
	* 
	* * @param InTargetProperty - The property onto which our value is to be copied
	* * @param InTargetValuePtr - The memory location for the target property
	*/
	bool CopyCompleteValue(const FProperty* InTargetProperty, uint8* InTargetValuePtr);
	
	/** Get Bool value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueBool(bool& OutBoolValue);

	/** Get Int8 value from Virtual Property */
	UFUNCTION()
	bool GetValueInt8(int8& OutInt8);

	/** Get Byte value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueByte(uint8& OutByte);

	/** Get Int16 value from Virtual Property */
	UFUNCTION()
	bool GetValueInt16(int16& OutInt16);

	/** Get Uint16 value from Virtual Property */
	UFUNCTION()
	bool GetValueUint16(uint16& OutUInt16);

	/** Get Int32 value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueInt32(int32& OutInt32);

	/** Get Uint32 value from Virtual Property */
	UFUNCTION()
	bool GetValueUInt32(uint32& OutUInt32);

	/** Get Int64 value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueInt64(int64& OuyInt64);

	/** Get Uint64 value from Virtual Property */
	UFUNCTION()
	bool GetValueUint64(uint64& OuyUInt64);

	/** Get Float value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueFloat(float& OutFloat);

	/** Get Double value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueDouble(double& OutDouble);

	/** Get String value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueString(FString& OutStringValue);

	/** Get Name value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueName(FName& OutNameValue);

	/** Get Text value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueText(FText& OutTextValue);

	/** Get Numeric value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueNumericInteger(int64& OutInt64Value);

	/** Get Vector value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueVector(FVector& OutVector);

	/** Get Rotator value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueRotator(FRotator& OutRotator);

	/** Get Color value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool GetValueColor(FColor& OutColor);

	/** Set Bool value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueBool(const bool InBoolValue);

	/** Set Int8 value from Virtual Property */
	UFUNCTION()
	bool SetValueInt8(const int8 InInt8);

	/** Set Byte value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueByte(const uint8 InByte);

	/** Set Int16 value from Virtual Property */
	UFUNCTION()
	bool SetValueInt16(const int16 InInt16);

	/** Set Uint16 value from Virtual Property */
	UFUNCTION()
	bool SetValueUint16(const uint16 InUInt16);

	/** Set Int32 value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueInt32(const int32 InInt32);

	/** Set Uint32 value from Virtual Property */
	UFUNCTION()
	bool SetValueUInt32(const uint32 InUInt32);

	/** Set Int64 value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueInt64(const int64 InInt64);

	/** Set Uint64 value from Virtual Property */
	UFUNCTION()
	bool SetValueUint64(const uint64 InUInt64);

	/** Set Float value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueFloat(const float InFloat);

	/** Set Double value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueDouble(const double InDouble);

	/** Set String value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueString(const FString& InStringValue);

	/** Set Name value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueName(const FName& InNameValue);

	/** Set Text value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueText(const FText& InTextValue);

	/** Set Numeric value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueNumericInteger(const int64 InInt64Value);

	/** Set Vector value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueVector(const FVector& InVector);

	/** Set Rotator value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueRotator(const FRotator& InRotator);

	/** Set Color value from Virtual Property */
	UFUNCTION(BlueprintCallable, Category = "Remote Control Behaviour")
	bool SetValueColor(const FColor& InColor);
	
	/** Set FProperty Name */
	UFUNCTION(BlueprintPure, Category = "Remote Control Behaviour")
	FName GetPropertyName() const;

public:
	/** Unique property name */
	UPROPERTY()
	FName PropertyName;

	/** Property Id */
	UPROPERTY()
	FGuid Id;

	/** Pointer to Remote Control Preset */
	UPROPERTY()
	TWeakObjectPtr<URemoteControlPreset> PresetWeakPtr;
};

/**
 * Remote Control Virtual Property which is stores in container with many properties
 * Where Property Bag has more then one Property 
 */
UCLASS()
class REMOTECONTROL_API URCVirtualPropertyInContainer : public URCVirtualPropertyBase
{
	GENERATED_BODY()

protected:
	//~ Begin URCVirtualPropertyBase interface
	virtual const FInstancedPropertyBag* GetPropertyBagInstance() const override;
	//~ End URCVirtualPropertyBase interface

public:
	/** Pointer to container where stores Virtual Properties */
	UPROPERTY()
	TWeakObjectPtr<URCVirtualPropertyContainerBase> ContainerWeakPtr;
};

/**
 * Remote Control Virtual Property which is stores in self defined Property Bag
 * In this case SelfContainer holds only single property in the Property Bag
 */
UCLASS()
class REMOTECONTROL_API URCVirtualPropertySelfContainer : public URCVirtualPropertyBase
{
	GENERATED_BODY()

public:
	/**
	 * Adds a new property to the bag. If property with this name already in bag the function just return
	 *
	 * @param InPropertyName				Name of the property
	 * @param InValueType					Property Type
	 * @param InValueTypeObject				Property Type object if exists
	 * 
	 */
	void AddProperty(const FName InPropertyName, const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject = nullptr);

	/**
	 * Duplicates property from give Property. If property with this name already in bag the function just return
	 *
	 * @param InPropertyName				Name of the property
	 * @param InSourceProperty				Source FProperty for duplication
	 *
	 * @return true if property duplicated successfully 
	 */
	bool DuplicateProperty(const FName& InPropertyName, const FProperty* InSourceProperty);

	/**
	 * Duplicates property from give Property and copy the value. If property with this name already in bag the function just return
	 *
	 * @param InPropertyName				Name of the property
	 * @param InSourceProperty				Source FProperty for duplication
	 * @param InSourceContainerPtr			Pointer to source container
	 *
	 * @return true if property duplicated and value copied successfully 
	 */
	bool DuplicatePropertyWithCopy(const FName& InPropertyName, const FProperty* InSourceProperty, const uint8* InSourceContainerPtr);

	/**
	 * Duplicates property from given Virtual Property. If property with this name already in bag the function just return
	 *
	 * @param InVirtualProperty				Virtual Property to duplicate from
	 *
	 * @return true if property duplicated and value copied successfully 
	 */
	bool DuplicatePropertyWithCopy(URCVirtualPropertyBase* InVirtualProperty);

	/** Resets the property bag instance to empty and remove Virtual Property data */
	void Reset();

	/** Creates new Struct on Scope for this Property Bag UStruct and Memory */
	TSharedPtr<FStructOnScope> CreateStructOnScope() const;

protected:
	//~ Begin URCVirtualPropertyBase interface
	virtual const FInstancedPropertyBag* GetPropertyBagInstance() const override;
	//~ End URCVirtualPropertyBase interface

private:
	/** Instanced property bag for store a bag of properties. */
	UPROPERTY()
	FInstancedPropertyBag Bag;
};