// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebugger.h"

#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "IAnimationProvider.h"
#include "IGameplayProvider.h"
#include "Insights/IUnrealInsightsModule.h"
#include "IGameplayInsightsModule.h"
#include "Modules/ModuleManager.h"
#include "ObjectTrace.h"
#include "TraceServices/Model/Frames.h"
#include "SLevelViewport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SSpacer.h"
#include "IRewindDebuggerExtension.h"
#include "IRewindDebuggerDoubleClickHandler.h"
#include "RewindDebuggerObjectTrack.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "ToolMenus.h"

static void IterateExtensions(TFunction<void(IRewindDebuggerExtension* Extension)> IteratorFunction)
{
	// update extensions
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	const int32 NumExtensions = ModularFeatures.GetModularFeatureImplementationCount(IRewindDebuggerExtension::ModularFeatureName);
	for (int32 ExtensionIndex = 0; ExtensionIndex < NumExtensions; ++ExtensionIndex)
	{
		IRewindDebuggerExtension* Extension = static_cast<IRewindDebuggerExtension*>(ModularFeatures.GetModularFeatureImplementation(IRewindDebuggerExtension::ModularFeatureName, ExtensionIndex));
		IteratorFunction(Extension);
	}
}

FRewindDebugger::FRewindDebugger()  :
	ControlState(FRewindDebugger::EControlState::Pause),
	bPIEStarted(false),
	bPIESimulating(false),
	bAutoRecord(false),
	bRecording(false),
	PlaybackRate(1),
	CurrentScrubTime(0),
	CurrentViewRange(0,0),
	CurrentTraceRange(0,0),
	RecordingIndex(0),
	bTargetActorPositionValid(false)
{
	RecordingDuration.Set(0);

	if (GEditor->bIsSimulatingInEditor || GEditor->PlayWorld)
	{
		OnPIEStarted(true);
	}

	FEditorDelegates::PreBeginPIE.AddRaw(this, &FRewindDebugger::OnPIEStarted);
	FEditorDelegates::PausePIE.AddRaw(this, &FRewindDebugger::OnPIEPaused);
	FEditorDelegates::ResumePIE.AddRaw(this, &FRewindDebugger::OnPIEResumed);
	FEditorDelegates::EndPIE.AddRaw(this, &FRewindDebugger::OnPIEStopped);
	FEditorDelegates::SingleStepPIE.AddRaw(this, &FRewindDebugger::OnPIESingleStepped);

	DebugTargetActor.OnPropertyChanged = DebugTargetActor.OnPropertyChanged.CreateLambda([this](FString Target) { RefreshDebugTracks(); });

	UnrealInsightsModule = &FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("RewindDebugger"), 0.0f, [this](float DeltaTime)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FRewindDebuggerModule_Tick);

		Tick(DeltaTime);

		return true;
	});

	IGameplayInsightsModule* GameplayInsightsModule = &FModuleManager::LoadModuleChecked<IGameplayInsightsModule>("GameplayInsights");
	GameplayInsightsModule->StartTrace();
}

FRewindDebugger::~FRewindDebugger() 
{
	FEditorDelegates::PostPIEStarted.RemoveAll(this);
	FEditorDelegates::PausePIE.RemoveAll(this);
	FEditorDelegates::ResumePIE.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
	FEditorDelegates::SingleStepPIE.RemoveAll(this);

	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
}

void FRewindDebugger::Initialize() 
{
	InternalInstance = new FRewindDebugger;
}

void FRewindDebugger::Shutdown() 
{
	delete InternalInstance;
}

void FRewindDebugger::OnComponentListChanged(const FOnComponentListChanged& InComponentListChangedDelegate)
{
	ComponentListChangedDelegate = InComponentListChangedDelegate;
}

void FRewindDebugger::OnTrackCursor(const FOnTrackCursor& InTrackCursorDelegate)
{
	TrackCursorDelegate = InTrackCursorDelegate;
}

void FRewindDebugger::OnPIEStarted(bool bSimulating)
{
	bPIEStarted = true;
	bPIESimulating = true;

	UE::Trace::ToggleChannel(TEXT("Object"), true);

	if (bAutoRecord)
	{
		StartRecording();
	}
}

void FRewindDebugger::OnPIEPaused(bool bSimulating)
{
	bPIESimulating = false;
	ControlState = EControlState::Pause;

	if (bRecording)
	{
		UWorld* World = GetWorldToVisualize();
		RecordingDuration.Set(FObjectTrace::GetWorldElapsedTime(World));
		SetCurrentScrubTime(RecordingDuration.Get());
	}
}

void FRewindDebugger::OnPIEResumed(bool bSimulating)
{
	bPIESimulating = true;

	// restore all relative transforms of any meshes that may have been moved while scrubbing
	for (TTuple<uint64, FMeshComponentResetData>& MeshData : MeshComponentsToReset)
	{
		if (USkeletalMeshComponent* MeshComponent = MeshData.Value.Component.Get())
		{
			MeshComponent->SetRelativeTransform(MeshData.Value.RelativeTransform);
		}
	}

	MeshComponentsToReset.Empty();
}

void FRewindDebugger::OnPIESingleStepped(bool bSimulating)
{
	// restore all relative transforms of any meshes that may have been moved while scrubbing
	for (TTuple<uint64, FMeshComponentResetData>& MeshData : MeshComponentsToReset)
	{
		if (USkeletalMeshComponent* MeshComponent = MeshData.Value.Component.Get())
		{
			MeshComponent->SetRelativeTransform(MeshData.Value.RelativeTransform);
		}
	}

	MeshComponentsToReset.Empty();

	if (bRecording)
	{
		UWorld* World = GetWorldToVisualize();
		RecordingDuration.Set(FObjectTrace::GetWorldElapsedTime(World));
		SetCurrentScrubTime(RecordingDuration.Get());
	}
}


void FRewindDebugger::OnPIEStopped(bool bSimulating)
{
	bPIEStarted = false;
	bPIESimulating = false;
	MeshComponentsToReset.Empty();

	UE::Trace::ToggleChannel(TEXT("Object"), false);

	StopRecording();
	// clear the current recording (until we support playback in the Editor world on spawned actors)
	RecordingDuration.Set(0);
	SetCurrentScrubTime(0);
}

bool FRewindDebugger::GetTargetActorPosition(FVector& OutPosition) const
{
	OutPosition = TargetActorPosition;
	return bTargetActorPositionValid;
}

uint64 FRewindDebugger::GetTargetActorId() const
{
	if (DebugTargetActor.Get() == "")
	{
		return 0;
	}

	uint64 TargetActorId = 0;

	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
		{
			double Time = CurrentTraceTime();
			GameplayProvider->EnumerateObjects(Time, Time, [this,&TargetActorId](const FObjectInfo& InObjectInfo)
			{
				if (DebugTargetActor.Get() == InObjectInfo.Name)
				{
					TargetActorId = InObjectInfo.Id;
				}
			});
		}
	}

	return TargetActorId;
}

void FRewindDebugger::RefreshDebugTracks()
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
		UWorld* World = GetWorldToVisualize();

		if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
		{
			uint64 TargetActorId = GetTargetActorId();

			if (TargetActorId == 0)
			{
				return;
			}

			bool bChanged = false;

			// add actor (even if it isn't found in the gameplay provider)
			if (DebugTracks.Num() == 0)
			{
				bChanged = true;
				const bool bAddController = true;
				DebugTracks.Add(MakeShared<RewindDebugger::FRewindDebuggerObjectTrack>(TargetActorId, DebugTargetActor.Get(), bAddController));
			}
			else
			{
				if (DebugTracks[0]->GetDisplayName().ToString() != DebugTargetActor.Get() || DebugTracks[0]->GetObjectId() != TargetActorId)
				{
					bChanged = true;
					DebugTracks[0] = MakeShared<RewindDebugger::FRewindDebuggerObjectTrack>(TargetActorId, DebugTargetActor.Get());
				}
			}

			if (TargetActorId != 0 && DebugTracks.Num() > 0)
			{
				bChanged = bChanged || DebugTracks[0]->Update();
			}

			if (bChanged)
			{
				ComponentListChangedDelegate.ExecuteIfBound();
			}
		}
	}
}

void FRewindDebugger::StartRecording()
{
	if (!CanStartRecording())
	{
		return;
	}

	// Enable Object and Animation Trace filters
	UE::Trace::ToggleChannel(TEXT("ObjectProperties"), true);
	UE::Trace::ToggleChannel(TEXT("Animation"), true);
	UE::Trace::ToggleChannel(TEXT("Frame"), true);

	// update extensions
	IterateExtensions([this](IRewindDebuggerExtension* Extension)
		{
			Extension->RecordingStarted(this);
		}
	);

	RecordingDuration.Set(0);
	RecordingIndex++;
	bRecording = true;

	// setup FObjectTrace to start tracking tracing times from 0
	// and increment the RecordingIndex so we can use it to distinguish between the latest recording and older ones
	UWorld* World = GetWorldToVisualize();
	FObjectTrace::ResetWorldElapsedTime(World);
	FObjectTrace::SetWorldRecordingIndex(World, RecordingIndex);
}

void FRewindDebugger::StopRecording()
{
	if (bRecording)
	{
		// Enable Object and Animation Trace filters
		UE::Trace::ToggleChannel(TEXT("ObjectProperties"), false);
		UE::Trace::ToggleChannel(TEXT("Animation"), false);
		UE::Trace::ToggleChannel(TEXT("Frame"), false);

		// update extensions
		IterateExtensions([this](IRewindDebuggerExtension* Extension)
			{
				Extension->RecordingStopped(this);
			}
		);

		bRecording = false;
	}
}

bool FRewindDebugger::CanPause() const
{
	return ControlState != EControlState::Pause;
}

void FRewindDebugger::Pause()
{
	if (CanPause())
	{
		if (bPIESimulating)
		{
			// pause PIE
		}

		ControlState = EControlState::Pause;
	}
}

bool FRewindDebugger::IsPlaying() const
{
	return ControlState==EControlState::Play && !bPIESimulating;
}

bool FRewindDebugger::CanPlay() const
{
	return ControlState!=EControlState::Play && !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::Play()
{
	if (CanPlay())
	{
		if (CurrentScrubTime >= RecordingDuration.Get())
		{
			SetCurrentScrubTime(0);
		}

		ControlState = EControlState::Play;
	}
}

bool FRewindDebugger::CanPlayReverse() const
{
	return ControlState!=EControlState::PlayReverse && !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::PlayReverse()
{
	if (CanPlayReverse())
	{
		if (CurrentScrubTime <= 0)
		{
			SetCurrentScrubTime(RecordingDuration.Get());
		}

		ControlState = EControlState::PlayReverse;
	}
}

bool FRewindDebugger::CanScrub() const
{
	return !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::ScrubToStart()
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(0);
		TrackCursorDelegate.ExecuteIfBound(false);
	}
}

void FRewindDebugger::ScrubToEnd()
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(RecordingDuration.Get());
		TrackCursorDelegate.ExecuteIfBound(false);
	}
}

void FRewindDebugger::Step(int frames)
{
	if (CanScrub())
	{
		Pause();

		if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
			UWorld* World = GetWorldToVisualize();

			if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
			{
				if (const IGameplayProvider::RecordingInfoTimeline* Recording = GameplayProvider->GetRecordingInfo(RecordingIndex))
				{
					uint64 EventCount = Recording->GetEventCount();

					if (EventCount > 0)
					{
						ScrubTimeInformation.FrameIndex = FMath::Clamp<int64>(ScrubTimeInformation.FrameIndex + frames, 0, (int64)EventCount - 1);
						const FRecordingInfoMessage& Event = Recording->GetEvent(ScrubTimeInformation.FrameIndex);

						SetCurrentScrubTime(Event.ElapsedTime);
						
						TrackCursorDelegate.ExecuteIfBound(false);
					}
				}
			}
		}
	}
}

void FRewindDebugger::StepForward()
{
	Step(1);
}

void FRewindDebugger::StepBackward()
{
	Step(-1);
}


void FRewindDebugger::ScrubToTime(double ScrubTime, bool bIsScrubbing)
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(ScrubTime);
	}
}

UWorld* FRewindDebugger::GetWorldToVisualize() const
{
	// we probably want to replace this with a world selector widget, if we are going to support tracing from anything other thn the PIE world

	UWorld* World = nullptr;

#if WITH_EDITOR
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (GIsEditor && EditorEngine != nullptr && World == nullptr)
	{
		// lets use PlayWorld during PIE/Simulate and regular world from editor otherwise, to draw debug information
		World = EditorEngine->PlayWorld != nullptr ? ToRawPtr(EditorEngine->PlayWorld) : EditorEngine->GetEditorWorldContext().World();
	}

#endif
	if (!GIsEditor && World == nullptr)
	{
		World = GEngine->GetWorld();
	}

	return World;
}

void FRewindDebugger::SetCurrentViewRange(const TRange<double>& Range)
{
	CurrentViewRange = Range;
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		GetScrubTimeInformation(CurrentViewRange.GetLowerBoundValue(), LowerBoundViewTimeInformation, RecordingIndex, Session);
		GetScrubTimeInformation(CurrentViewRange.GetUpperBoundValue(), UpperBoundViewTimeInformation, RecordingIndex, Session);
		
		CurrentTraceRange.SetLowerBoundValue(LowerBoundViewTimeInformation.ProfileTime);
		CurrentTraceRange.SetUpperBoundValue(UpperBoundViewTimeInformation.ProfileTime);
	}
}

void FRewindDebugger::SetCurrentScrubTime(double Time)
{
	CurrentScrubTime = Time;

	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		GetScrubTimeInformation(CurrentScrubTime, ScrubTimeInformation, RecordingIndex, Session);
		
		TraceTime.Set(ScrubTimeInformation.ProfileTime);
	}
}

void FRewindDebugger::GetScrubTimeInformation(double InDebugTime, FScrubTimeInformation & InOutTimeInformation, uint16 InRecordingIndex, const TraceServices::IAnalysisSession* AnalysisSession)
{
	const IGameplayProvider* GameplayProvider = AnalysisSession->ReadProvider<IGameplayProvider>("GameplayProvider");
	const IAnimationProvider* AnimationProvider = AnalysisSession->ReadProvider<IAnimationProvider>("AnimationProvider");
	
	if (GameplayProvider && AnimationProvider)
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);
		
		if (const IGameplayProvider::RecordingInfoTimeline* Recording = GameplayProvider->GetRecordingInfo(InRecordingIndex))
		{
			const uint64 EventCount = Recording->GetEventCount();

			if (EventCount > 0)
			{
				int ScrubFrameIndex = InOutTimeInformation.FrameIndex;
				const FRecordingInfoMessage& FirstEvent = Recording->GetEvent(0);
				const FRecordingInfoMessage& LastEvent = Recording->GetEvent(EventCount - 1);

				// Check if we are outside of the recorded range, and apply the first or last frame
				if (InDebugTime <= FirstEvent.ElapsedTime)
				{
					ScrubFrameIndex = FMath::Min<uint64>(1, EventCount - 1);
				}
				else if (InDebugTime >= LastEvent.ElapsedTime)
				{
					ScrubFrameIndex = EventCount - 1;
				}
				// Find the two keys surrounding the InDebugTime, and pick the nearest to update InOutTimeInformation
				else
				{
					const FRecordingInfoMessage& ScrubEvent = Recording->GetEvent(ScrubFrameIndex);
					constexpr float MaxTimeDifferenceInSeconds = 15.0f / 60.0f;
					
					// Use linear search on smaller time differences
					if (FMath::Abs(InDebugTime - ScrubEvent.ElapsedTime) <= MaxTimeDifferenceInSeconds)
					{
						if (Recording->GetEvent(ScrubFrameIndex).ElapsedTime > InDebugTime)
						{
							for (uint64 EventIndex = ScrubFrameIndex; EventIndex > 0; EventIndex--)
							{
								const FRecordingInfoMessage& Event = Recording->GetEvent(EventIndex);
								const FRecordingInfoMessage& NextEvent = Recording->GetEvent(EventIndex - 1);
								if (Event.ElapsedTime >= InDebugTime && NextEvent.ElapsedTime <= InDebugTime)
								{
									if (Event.ElapsedTime - InDebugTime < InDebugTime - NextEvent.ElapsedTime)
									{
										ScrubFrameIndex = EventIndex;
									}
									else
									{
										ScrubFrameIndex = EventIndex - 1;
									}
									break;
								}
							}
						}
						else
						{
							for (uint64 EventIndex = ScrubFrameIndex; EventIndex < EventCount - 1; EventIndex++)
							{
								const FRecordingInfoMessage& Event = Recording->GetEvent(EventIndex);
								const FRecordingInfoMessage& NextEvent = Recording->GetEvent(EventIndex + 1);
								if (Event.ElapsedTime <= InDebugTime && NextEvent.ElapsedTime >= InDebugTime)
								{
									if (InDebugTime - Event.ElapsedTime < NextEvent.ElapsedTime - InDebugTime)
									{
										ScrubFrameIndex = EventIndex;
									}
									else
									{
										ScrubFrameIndex = EventIndex + 1;
									}
									break;
								}
							}
						}
					}
					// Binary search for surrounding keys on big time differences
					else
					{
						uint64 StartEventIndex = 0;
						uint64 EndEventIndex = EventCount -1;
						
						while (EndEventIndex - StartEventIndex > 1)
						{
							const uint64 MiddleEventIndex = ((StartEventIndex + EndEventIndex) / 2);
							const FRecordingInfoMessage& MiddleEvent = Recording->GetEvent(MiddleEventIndex);
							if (InDebugTime < MiddleEvent.ElapsedTime)
							{
								EndEventIndex = MiddleEventIndex;
							}
							else
							{
								StartEventIndex = MiddleEventIndex;
							}
						}
						
						// Ensure there is not frames between start and end index
						check(EndEventIndex == StartEventIndex + 1)
				
						const FRecordingInfoMessage& Event = Recording->GetEvent(StartEventIndex);
						const FRecordingInfoMessage& NextEvent = Recording->GetEvent(EndEventIndex);

						// Ensure debug time is between both frames time range
						check (Event.ElapsedTime <= InDebugTime && NextEvent.ElapsedTime >= InDebugTime)

						// Choose frame that is nearest to the debug time
						if (InDebugTime - Event.ElapsedTime < NextEvent.ElapsedTime - InDebugTime)
						{
							ScrubFrameIndex = StartEventIndex;
						}
						else
						{
							ScrubFrameIndex = EndEventIndex;
						}
					}
				}
				
				const FRecordingInfoMessage& Event = Recording->GetEvent(ScrubFrameIndex);
				InOutTimeInformation.FrameIndex = ScrubFrameIndex;
				InOutTimeInformation.ProfileTime = Event.ProfileTime;
			}
		}
	}
}

const TraceServices::IAnalysisSession* FRewindDebugger::GetAnalysisSession() const
{
	if (UnrealInsightsModule == nullptr)
	{
		UnrealInsightsModule = &FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	}

	return UnrealInsightsModule->GetAnalysisSession().Get();
}

void FRewindDebugger::Tick(float DeltaTime)
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		if (bRecording)
		{
			// if you select a debug target before you start recording, update component list when it becomes valid
			RefreshDebugTracks();
		}
		
		const IAnimationProvider* AnimationProvider = Session->ReadProvider<IAnimationProvider>("AnimationProvider");
		const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");

		if (AnimationProvider && GameplayProvider)
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
			UWorld* World = GetWorldToVisualize();

			if (bPIESimulating)
			{
				if (bRecording)
				{
					RecordingDuration.Set(FObjectTrace::GetWorldElapsedTime(World));
					SetCurrentScrubTime(RecordingDuration.Get());
					TrackCursorDelegate.ExecuteIfBound(false);
				}
			}
			else
			{
				if (RecordingDuration.Get() > 0)
				{
					if (ControlState == EControlState::Play || ControlState == EControlState::PlayReverse)
					{
						float Rate = PlaybackRate * (ControlState == EControlState::Play ? 1 : -1);
						SetCurrentScrubTime(FMath::Clamp(CurrentScrubTime + Rate * DeltaTime, 0.0f, RecordingDuration.Get()));
						TrackCursorDelegate.ExecuteIfBound(Rate<0);

						if (CurrentScrubTime == 0 || CurrentScrubTime == RecordingDuration.Get())
						{
							// pause at end.
							ControlState = EControlState::Pause;
						}
					}

					double CurrentTraceTime = TraceTime.Get();
					const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);
					TraceServices::FFrame Frame;
					if(FrameProvider.GetFrameFromTime(ETraceFrameType::TraceFrameType_Game, CurrentTraceTime, Frame))
					{
						
						// until we have actor transforms traced out, the first skeletal mesh component transform on the target actor be used as as the actor position 
						uint64 TargetActorId = GetTargetActorId();
						if (TargetActorId != 0)
						{
							if (UObject* ObjectInstance = FObjectTrace::GetObjectFromId(TargetActorId))
							{
								if (AActor* TargetActor = Cast<AActor>(ObjectInstance))
								{
									TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshComponents;
									TargetActor->GetComponents(SkeletalMeshComponents);

									if (SkeletalMeshComponents.Num() > 0)
									{
										int64 ObjectId = FObjectTrace::GetObjectId(SkeletalMeshComponents[0]);

										AnimationProvider->ReadSkeletalMeshPoseTimeline(ObjectId, [this, Frame, ObjectId, AnimationProvider](const IAnimationProvider::SkeletalMeshPoseTimeline& TimelineData, bool bHasCurves)
											{
												TimelineData.EnumerateEvents(Frame.StartTime, Frame.EndTime,
													[this](double InStartTime, double InEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& PoseMessage)
													{
														bTargetActorPositionValid = true;
														TargetActorPosition = PoseMessage.ComponentToWorld.GetTranslation();

														return TraceServices::EEventEnumerate::Stop;
													});
											});
									}
								}
							}
						}
						
						// update pose on all SkeletalMeshComponents:
						// - enumerate all skeletal mesh pose timelines
						// - check if the corresponding mesh component still exists
						// - apply the recorded pose for the current Frame
						AnimationProvider->EnumerateSkeletalMeshPoseTimelines([this, &Frame, AnimationProvider, GameplayProvider](uint64 ObjectId, const IAnimationProvider::SkeletalMeshPoseTimeline& TimelineData)
						{
							if(UObject* ObjectInstance = FObjectTrace::GetObjectFromId(ObjectId))
							{
								if(USkeletalMeshComponent* MeshComponent = Cast<USkeletalMeshComponent>(ObjectInstance))
								{
									AnimationProvider->ReadSkeletalMeshPoseTimeline(ObjectId, [this, &Frame, ObjectId, MeshComponent, AnimationProvider](const IAnimationProvider::SkeletalMeshPoseTimeline& TimelineData, bool bHasCurves)
									{
										TimelineData.EnumerateEvents(Frame.StartTime, Frame.EndTime,
											[this, ObjectId, MeshComponent, AnimationProvider](double InStartTime, double InEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& PoseMessage)
											{
												FTransform ComponentWorldTransform;
												const FSkeletalMeshInfo* SkeletalMeshInfo = AnimationProvider->FindSkeletalMeshInfo(PoseMessage.MeshId);
												AnimationProvider->GetSkeletalMeshComponentSpacePose(PoseMessage, *SkeletalMeshInfo, ComponentWorldTransform, MeshComponent->GetEditableComponentSpaceTransforms());
												MeshComponent->ApplyEditedComponentSpaceTransforms();

												if (MeshComponentsToReset.Find(ObjectId) == nullptr)
												{
													FMeshComponentResetData ResetData;
													ResetData.Component = MeshComponent;
													ResetData.RelativeTransform = MeshComponent->GetRelativeTransform();
													MeshComponentsToReset.Add(ObjectId, ResetData);
												}

												// todo: we need to take into account tick order requirements for attached objects here
												MeshComponent->SetWorldTransform(ComponentWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
												MeshComponent->SetForcedLOD(PoseMessage.LodIndex + 1);
												return TraceServices::EEventEnumerate::Stop;
											});
									});
								}
							}
						});

						// Apply Animation Blueprint Debugging Data:
						// - enumerate over all anim graph timelines
						// - check if their instance class still exists and is the debugging target for the Animation Blueprint Editor
						// - if it is copy that debug data into the class debug data for the blueprint debugger
						AnimationProvider->EnumerateAnimGraphTimelines([&Frame, AnimationProvider, GameplayProvider](uint64 ObjectId, const IAnimationProvider::AnimGraphTimeline& AnimGraphTimeline)
						{
							if(UObject* ObjectInstance = FObjectTrace::GetObjectFromId(ObjectId))
							{
								if(UAnimInstance* AnimInstance = Cast<UAnimInstance>(ObjectInstance))
								{
									if(UAnimBlueprintGeneratedClass* InstanceClass = Cast<UAnimBlueprintGeneratedClass>(AnimInstance->GetClass()))
									{
										if(UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(InstanceClass->ClassGeneratedBy))
										{
											if(AnimBlueprint->IsObjectBeingDebugged(AnimInstance))
											{
												// update debug info for attached Animation Blueprint editors
												uint64 Id = FObjectTrace::GetObjectId(AnimInstance);
												const int32 NodeCount = InstanceClass->GetAnimNodeProperties().Num();
						
												FAnimBlueprintDebugData& DebugData = InstanceClass->GetAnimBlueprintDebugData();
												DebugData.ResetNodeVisitSites();
					
												AnimGraphTimeline.EnumerateEvents(Frame.StartTime, Frame.EndTime, [Id, AnimationProvider, GameplayProvider, &DebugData, NodeCount](double InGraphStartTime, double InGraphEndTime, uint32 InDepth, const FAnimGraphMessage& InMessage)
												{
													// Basic verification - check node count is the same
													// @TODO: could add some form of node hash/CRC to the class to improve this
													if(InMessage.NodeCount == NodeCount)
													{
														// Check for an update phase (which contains weights)
														if(InMessage.Phase == EAnimGraphPhase::Update)
														{
															AnimationProvider->ReadAnimNodesTimeline(Id, [InGraphStartTime, InGraphEndTime, &DebugData](const IAnimationProvider::AnimNodesTimeline& InNodesTimeline)
															{
																InNodesTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [&DebugData](double InStartTime, double InEndTime, uint32 InDepth, const FAnimNodeMessage& InMessage)
																{
																	DebugData.RecordNodeVisit(InMessage.NodeId, InMessage.PreviousNodeId, InMessage.Weight);
																	return TraceServices::EEventEnumerate::Continue;
																});
															});
					
															AnimationProvider->ReadStateMachinesTimeline(Id, [InGraphStartTime, InGraphEndTime, &DebugData](const IAnimationProvider::StateMachinesTimeline& InStateMachinesTimeline)
															{
																InStateMachinesTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [&DebugData](double InStartTime, double InEndTime, uint32 InDepth, const FAnimStateMachineMessage& InMessage)
																{
																	DebugData.RecordStateData(InMessage.StateMachineIndex, InMessage.StateIndex, InMessage.StateWeight, InMessage.ElapsedTime);
																	return TraceServices::EEventEnumerate::Continue;
																});
															});
					
															AnimationProvider->ReadAnimSequencePlayersTimeline(Id, [InGraphStartTime, InGraphEndTime, GameplayProvider, &DebugData](const IAnimationProvider::AnimSequencePlayersTimeline& InSequencePlayersTimeline)
															{
																InSequencePlayersTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [&DebugData](double InStartTime, double InEndTime, uint32 InDepth, const FAnimSequencePlayerMessage& InMessage)
																{
																	DebugData.RecordSequencePlayer(InMessage.NodeId, InMessage.Position, InMessage.Length, InMessage.FrameCounter);
																	return TraceServices::EEventEnumerate::Continue;
																});
															});
					
															AnimationProvider->ReadAnimBlendSpacePlayersTimeline(Id, [InGraphStartTime, InGraphEndTime, GameplayProvider, &DebugData](const IAnimationProvider::BlendSpacePlayersTimeline& InBlendSpacePlayersTimeline)
															{
																InBlendSpacePlayersTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [GameplayProvider, &DebugData](double InStartTime, double InEndTime, uint32 InDepth, const FBlendSpacePlayerMessage& InMessage)
																{
																	UBlendSpace* BlendSpace = nullptr;
																	const FObjectInfo* BlendSpaceInfo = GameplayProvider->FindObjectInfo(InMessage.BlendSpaceId);
																	if(BlendSpaceInfo)
																	{
																		BlendSpace = TSoftObjectPtr<UBlendSpace>(FSoftObjectPath(BlendSpaceInfo->PathName)).LoadSynchronous();
																	}
					
																	DebugData.RecordBlendSpacePlayer(InMessage.NodeId, BlendSpace, FVector(InMessage.PositionX, InMessage.PositionY, InMessage.PositionZ), FVector(InMessage.FilteredPositionX, InMessage.FilteredPositionY, InMessage.FilteredPositionZ));
																	return TraceServices::EEventEnumerate::Continue;
																});
															});
					
															AnimationProvider->ReadAnimSyncTimeline(Id, [InGraphStartTime, InGraphEndTime, AnimationProvider, &DebugData](const IAnimationProvider::AnimSyncTimeline& InAnimSyncTimeline)
															{
																InAnimSyncTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [AnimationProvider, &DebugData](double InStartTime, double InEndTime, uint32 InDepth, const FAnimSyncMessage& InMessage)
																{
																	const TCHAR* GroupName = AnimationProvider->GetName(InMessage.GroupNameId);
																	if(GroupName)
																	{
																		DebugData.RecordNodeSync(InMessage.SourceNodeId, FName(GroupName));
																	}
														
																	return TraceServices::EEventEnumerate::Continue;
																});
															});
														}
					
														// Some traces come from both update and evaluate phases
														if(InMessage.Phase == EAnimGraphPhase::Update || InMessage.Phase == EAnimGraphPhase::Evaluate)
														{
															AnimationProvider->ReadAnimAttributesTimeline(Id, [InGraphStartTime, InGraphEndTime, AnimationProvider, &DebugData](const IAnimationProvider::AnimAttributeTimeline& InAnimAttributeTimeline)
															{
																InAnimAttributeTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [AnimationProvider, &DebugData](double InStartTime, double InEndTime, uint32 InDepth, const FAnimAttributeMessage& InMessage)
																{
																	const TCHAR* AttributeName = AnimationProvider->GetName(InMessage.AttributeNameId);
																	if(AttributeName)
																	{
																		DebugData.RecordNodeAttribute(InMessage.TargetNodeId, InMessage.SourceNodeId, FName(AttributeName));
																	}
														
																	return TraceServices::EEventEnumerate::Continue;
																});
															});
														}
					
														// Anim node values can come from all phases
														AnimationProvider->ReadAnimNodeValuesTimeline(Id, [InGraphStartTime, InGraphEndTime, AnimationProvider, &DebugData](const IAnimationProvider::AnimNodeValuesTimeline& InNodeValuesTimeline)
														{
															InNodeValuesTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [AnimationProvider, &DebugData](double InStartTime, double InEndTime, uint32 InDepth, const FAnimNodeValueMessage& InMessage)
															{
																FText Text = AnimationProvider->FormatNodeKeyValue(InMessage);
																DebugData.RecordNodeValue(InMessage.NodeId, Text.ToString());
																return TraceServices::EEventEnumerate::Continue;
															});
														});
													}
													return TraceServices::EEventEnumerate::Continue;
												});
											}
										}
									}
								}
							}
							return TraceServices::EEventEnumerate::Continue;
						});
					}
				}
			}
		}

		// update extensions
		IterateExtensions([DeltaTime, this](IRewindDebuggerExtension* Extension)
			{
				Extension->Update(DeltaTime, this);
			}
		);
	}
}

void FRewindDebugger::ComponentSelectionChanged(TSharedPtr<RewindDebugger::FRewindDebuggerTrack> SelectedObject)
{
	SelectedTrack = SelectedObject;

	// todo: if not hidden (add a toggle button to toolbar to hide/show details)
	TSharedPtr<SDockTab> DetailsTab = FGlobalTabmanager::Get()->TryInvokeTab(FName("RewindDebuggerDetails"));

	TSharedPtr<SWidget> DetailsView;

	if (SelectedObject)
	{
		if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
		{
			DetailsView = SelectedObject->GetDetailsView();
		}
	}
	
	if (DetailsView)
	{
		DetailsTab->SetContent(DetailsView.ToSharedRef());
	}
	else
	{
		static TSharedPtr<SWidget> EmptyDetails;
		if (EmptyDetails == nullptr)
		{
			EmptyDetails = 	SNew(SSpacer);
		}
		DetailsTab->SetContent(EmptyDetails.ToSharedRef());
	}
}

void FRewindDebugger::ComponentDoubleClicked(TSharedPtr<RewindDebugger::FRewindDebuggerTrack> SelectedObject)
{
	if (!SelectedObject.IsValid())
	{
		return;
	}
	
	SelectedTrack = SelectedObject;
	
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	static const FName HandlerFeatureName = IRewindDebuggerDoubleClickHandler::ModularFeatureName;
		
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
	
		const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");
		const FObjectInfo& ObjectInfo = GameplayProvider->GetObjectInfo(SelectedObject->GetObjectId());
		uint64 ClassId = ObjectInfo.ClassId;
		bool bHandled = false;
		
		const int32 NumExtensions = ModularFeatures.GetModularFeatureImplementationCount(HandlerFeatureName);

		// iterate up the class hierarchy, looking for a registered double click handler, until we find the one that succeeeds that is most specific to the type of this object
		while (ClassId != 0 && !bHandled)
		{
			const FClassInfo& ClassInfo = GameplayProvider->GetClassInfo(ClassId);
		
			for (int32 ExtensionIndex = 0; ExtensionIndex < NumExtensions; ++ExtensionIndex)
			{
				IRewindDebuggerDoubleClickHandler* Handler = static_cast<IRewindDebuggerDoubleClickHandler*>(ModularFeatures.GetModularFeatureImplementation(HandlerFeatureName, ExtensionIndex));
				if (Handler->GetTargetTypeName() == ClassInfo.Name)
				{
					if (Handler->HandleDoubleClick(this))
					{
						bHandled = true;
						break;
					}
				}
			}
		
			ClassId = ClassInfo.SuperId;
		}
	}
}

TSharedPtr<SWidget> FRewindDebugger::BuildComponentContextMenu()
{
	UComponentContextMenuContext* MenuContext = NewObject<UComponentContextMenuContext>();
	MenuContext->SelectedObject = GetSelectedComponent();

	if (SelectedTrack.IsValid())
	{
		// build a list of class hierarchy names to make it easier for extensions to enable menu entries by type
		if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
	
			const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");
			const FObjectInfo& ObjectInfo = GameplayProvider->GetObjectInfo(SelectedTrack->GetObjectId());
			uint64 ClassId = ObjectInfo.ClassId;
			while (ClassId != 0)
			{
				const FClassInfo& ClassInfo = GameplayProvider->GetClassInfo(ClassId);
				MenuContext->TypeHierarchy.Add(ClassInfo.Name);
				ClassId = ClassInfo.SuperId;
			}
		}
	}

	return UToolMenus::Get()->GenerateWidget("RewindDebugger.ComponentContextMenu", FToolMenuContext(MenuContext));
 }


TSharedPtr<FDebugObjectInfo> FRewindDebugger::GetSelectedComponent() const
{
	if (!SelectedComponent.IsValid())
	{
		SelectedComponent = MakeShared<FDebugObjectInfo>(0,"");
	}
	SelectedComponent->ObjectId = SelectedTrack->GetObjectId();
	SelectedComponent->ObjectName = SelectedTrack->GetDisplayName().ToString();
	return SelectedComponent;
}

// build a component tree that's compatible with the public api from 5.0 for GetDebugComponents.
void FRewindDebugger::RefreshDebugComponents(TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>>& InTracks, TArray<TSharedPtr<FDebugObjectInfo>>& OutComponents)
{
	OutComponents.SetNum(0);
	for(auto& Track : InTracks)
	{
		int Index = OutComponents.Num();
		OutComponents.Add(MakeShared<FDebugObjectInfo>(Track->GetObjectId(), Track->GetDisplayName().ToString()));
		TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>> TrackChildren;
		Track->IterateSubTracks([&TrackChildren](TSharedPtr<RewindDebugger::FRewindDebuggerTrack> Child) { TrackChildren.Add(Child); });
		RefreshDebugComponents(TrackChildren, OutComponents[Index]->Children);
	}
}

TArray<TSharedPtr<FDebugObjectInfo>>& FRewindDebugger::GetDebugComponents()
{
	RefreshDebugComponents(DebugTracks, DebugComponents);
	return DebugComponents;
}
