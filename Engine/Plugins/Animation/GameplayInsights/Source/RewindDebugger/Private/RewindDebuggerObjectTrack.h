// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RewindDebuggerTrack.h"

namespace RewindDebugger
{

class FRewindDebuggerObjectTrack : public FRewindDebuggerTrack
{
public:

	FRewindDebuggerObjectTrack(uint64 InObjectId, const FString& InObjectName, bool bInAddController = false)
		: ObjectName(InObjectName)
		, ObjectId(InObjectId)
		, bAddController(bInAddController)
	{
	}

	TRange<double> GetExistenceRange() const { return ExistenceRange; }

private:
	virtual TSharedPtr<SWidget> GetTimelineViewInternal() override;
	virtual bool UpdateInternal() override;
	virtual void IterateSubTracksInternal(TFunction<void(TSharedPtr<FRewindDebuggerTrack> SubTrack)> IteratorFunction) override;
	
	virtual FName GetNameInternal() const override { return ""; }
	virtual FSlateIcon GetIconInternal() override { return Icon; }
	virtual FText GetDisplayNameInternal() const override { return FText::FromString(ObjectName); }
	virtual uint64 GetObjectIdInternal() const override { return ObjectId; }
	virtual bool HasDebugDataInternal() const override { return false; }
	
	FString ObjectName;
	FSlateIcon Icon;
	TRange<double> ExistenceRange;
	uint64 ObjectId;
	TArray<TSharedPtr<FRewindDebuggerTrack>> Children;

	bool bAddController;
};

}