// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEdNode.h"

#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowCore.h"
#include "Logging/LogMacros.h"

#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "EdGraph/EdGraphPin.h"
#endif


#define LOCTEXT_NAMESPACE "DataflowEdNode"

DEFINE_LOG_CATEGORY_STATIC(DATAFLOWNODE_LOG, Error, All);

void UDataflowEdNode::AllocateDefaultPins()
{
	UE_LOG(DATAFLOWNODE_LOG, Verbose, TEXT("UDataflowEdNode::AllocateDefaultPins()"));
	// called on node creation from UI. 
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (DataflowGraph)
	{
		if (DataflowNodeGuid.IsValid())
		{
			if (TSharedPtr<FDataflowNode> DataflowNode = DataflowGraph->FindBaseNode(DataflowNodeGuid))
			{
				for (const Dataflow::FPin& Pin : DataflowNode->GetPins())
				{
					if (Pin.Direction == Dataflow::FPin::EDirection::INPUT)
					{
						CreatePin(EEdGraphPinDirection::EGPD_Input, Pin.Type, Pin.Name);
					}					
					if (Pin.Direction == Dataflow::FPin::EDirection::OUTPUT)
					{
						CreatePin(EEdGraphPinDirection::EGPD_Output, Pin.Type, Pin.Name);
					}
				}
			}
		}
	}
#endif // WITH_EDITOR && !UE_BUILD_SHIPPING
}


FText UDataflowEdNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromString(GetName());
}

#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void UDataflowEdNode::PinConnectionListChanged(UEdGraphPin* Pin)
{
	if (ensure(IsBound()))
	{
		if (TSharedPtr<FDataflowNode> DataflowNode = DataflowGraph->FindBaseNode(DataflowNodeGuid))
		{
			if (Dataflow::FConnection* ConnectionInput = DataflowNode->FindInput(FName(Pin->GetName())) )
			{
				DataflowGraph->ClearConnections(ConnectionInput);
				for (UEdGraphPin* LinkedCon : Pin->LinkedTo)
				{
					if (UDataflowEdNode* LinkedNode = Cast<UDataflowEdNode>(LinkedCon->GetOwningNode()))
					{
						if (ensure(LinkedNode->IsBound()))
						{
							if (TSharedPtr<FDataflowNode> LinkedDataflowNode = DataflowGraph->FindBaseNode(LinkedNode->GetDataflowNodeGuid()))
							{
								if (Dataflow::FConnection* LinkedConBase = LinkedDataflowNode->FindOutput(FName(LinkedCon->GetName())))
								{
									DataflowGraph->Connect(ConnectionInput, LinkedConBase);
								}
							}
						}
					}
				}
			}
			else if (Dataflow::FConnection* Con = DataflowNode->FindOutput(FName(Pin->GetName())))
			{
				if (Dataflow::FConnection* ConnectionOutput = DataflowNode->FindOutput(FName(Pin->GetName())))
				{
					DataflowGraph->ClearConnections(ConnectionOutput);
					for (UEdGraphPin* LinkedCon : Pin->LinkedTo)
					{
						if (UDataflowEdNode* LinkedNode = Cast<UDataflowEdNode>(LinkedCon->GetOwningNode()))
						{
							if (ensure(LinkedNode->IsBound()))
							{
								if (TSharedPtr<FDataflowNode> LinkedDataflowNode = DataflowGraph->FindBaseNode(LinkedNode->GetDataflowNodeGuid()))
								{
									if (Dataflow::FConnection* LinkedConBase = LinkedDataflowNode->FindInput(FName(LinkedCon->GetName())))
									{
										DataflowGraph->Connect(LinkedConBase, ConnectionOutput);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	Super::PinConnectionListChanged(Pin);
}
#endif // WITH_EDITOR && !UE_BUILD_SHIPPING

void UDataflowEdNode::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << DataflowNodeGuid;
}


#undef LOCTEXT_NAMESPACE

