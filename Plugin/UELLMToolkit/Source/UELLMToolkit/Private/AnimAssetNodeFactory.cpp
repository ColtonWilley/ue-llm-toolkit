// Copyright Natali Caggiano. All Rights Reserved.

#include "AnimAssetNodeFactory.h"
#include "AnimGraphEditor.h"
#include "AnimNodePinUtils.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_DeadBlending.h"
#include "AnimGraphNode_CopyPoseFromMesh.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "AnimGraphNode_ControlRig.h"
#include "AnimGraphNode_CustomProperty.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "ControlRig.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"

UEdGraphNode* FAnimAssetNodeFactory::CreateAnimSequenceNode(
	UEdGraph* StateGraph,
	UAnimSequence* AnimSequence,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!StateGraph)
	{
		OutError = TEXT("Invalid state graph");
		return nullptr;
	}

	if (!AnimSequence)
	{
		OutError = TEXT("Invalid animation sequence");
		return nullptr;
	}

	// Create sequence player node
	FGraphNodeCreator<UAnimGraphNode_SequencePlayer> NodeCreator(*StateGraph);
	UAnimGraphNode_SequencePlayer* SeqNode = NodeCreator.CreateNode();

	if (!SeqNode)
	{
		OutError = TEXT("Failed to create sequence player node");
		return nullptr;
	}

	SeqNode->NodePosX = static_cast<int32>(Position.X);
	SeqNode->NodePosY = static_cast<int32>(Position.Y);

	// Set the animation sequence
	SeqNode->Node.SetSequence(AnimSequence);

	NodeCreator.Finalize();

	// Generate ID
	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("Anim"), AnimSequence->GetName(), StateGraph);
	FAnimGraphEditor::SetNodeId(SeqNode, OutNodeId);

	StateGraph->Modify();

	return SeqNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateBlendSpaceNode(
	UEdGraph* StateGraph,
	UBlendSpace* BlendSpace,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!StateGraph)
	{
		OutError = TEXT("Invalid state graph");
		return nullptr;
	}

	if (!BlendSpace)
	{
		OutError = TEXT("Invalid BlendSpace");
		return nullptr;
	}

	// Create BlendSpace player node
	FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> NodeCreator(*StateGraph);
	UAnimGraphNode_BlendSpacePlayer* BSNode = NodeCreator.CreateNode();

	if (!BSNode)
	{
		OutError = TEXT("Failed to create BlendSpace player node");
		return nullptr;
	}

	BSNode->NodePosX = static_cast<int32>(Position.X);
	BSNode->NodePosY = static_cast<int32>(Position.Y);

	// Set the BlendSpace
	BSNode->Node.SetBlendSpace(BlendSpace);

	NodeCreator.Finalize();

	// Generate ID
	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("BlendSpace"), BlendSpace->GetName(), StateGraph);
	FAnimGraphEditor::SetNodeId(BSNode, OutNodeId);

	StateGraph->Modify();

	return BSNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateBlendSpace1DNode(
	UEdGraph* StateGraph,
	UBlendSpace1D* BlendSpace,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!StateGraph)
	{
		OutError = TEXT("Invalid state graph");
		return nullptr;
	}

	if (!BlendSpace)
	{
		OutError = TEXT("Invalid BlendSpace1D");
		return nullptr;
	}

	// BlendSpace1D uses the same player node
	FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> NodeCreator(*StateGraph);
	UAnimGraphNode_BlendSpacePlayer* BSNode = NodeCreator.CreateNode();

	if (!BSNode)
	{
		OutError = TEXT("Failed to create BlendSpace1D player node");
		return nullptr;
	}

	BSNode->NodePosX = static_cast<int32>(Position.X);
	BSNode->NodePosY = static_cast<int32>(Position.Y);

	// Set the BlendSpace
	BSNode->Node.SetBlendSpace(BlendSpace);

	NodeCreator.Finalize();

	// Generate ID
	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("BlendSpace1D"), BlendSpace->GetName(), StateGraph);
	FAnimGraphEditor::SetNodeId(BSNode, OutNodeId);

	StateGraph->Modify();

	return BSNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateSlotNode(
	UEdGraph* Graph,
	FName SlotName,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_Slot> NodeCreator(*Graph);
	UAnimGraphNode_Slot* SlotNode = NodeCreator.CreateNode();

	if (!SlotNode)
	{
		OutError = TEXT("Failed to create Slot node");
		return nullptr;
	}

	SlotNode->NodePosX = static_cast<int32>(Position.X);
	SlotNode->NodePosY = static_cast<int32>(Position.Y);
	SlotNode->Node.SlotName = SlotName;

	NodeCreator.Finalize();

	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("Slot"), SlotName.ToString(), Graph);
	FAnimGraphEditor::SetNodeId(SlotNode, OutNodeId);

	Graph->Modify();

	return SlotNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateInertializationNode(
	UEdGraph* Graph,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_Inertialization> NodeCreator(*Graph);
	UAnimGraphNode_Inertialization* InertNode = NodeCreator.CreateNode();

	if (!InertNode)
	{
		OutError = TEXT("Failed to create Inertialization node");
		return nullptr;
	}

	InertNode->NodePosX = static_cast<int32>(Position.X);
	InertNode->NodePosY = static_cast<int32>(Position.Y);

	NodeCreator.Finalize();

	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("Inertial"), TEXT(""), Graph);
	FAnimGraphEditor::SetNodeId(InertNode, OutNodeId);

	Graph->Modify();

	return InertNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateDeadBlendingNode(
	UEdGraph* Graph,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_DeadBlending> NodeCreator(*Graph);
	UAnimGraphNode_DeadBlending* DeadBlendNode = NodeCreator.CreateNode();

	if (!DeadBlendNode)
	{
		OutError = TEXT("Failed to create DeadBlending node");
		return nullptr;
	}

	DeadBlendNode->NodePosX = static_cast<int32>(Position.X);
	DeadBlendNode->NodePosY = static_cast<int32>(Position.Y);

	NodeCreator.Finalize();

	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("DeadBlend"), TEXT(""), Graph);
	FAnimGraphEditor::SetNodeId(DeadBlendNode, OutNodeId);

	Graph->Modify();

	return DeadBlendNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateCopyPoseFromMeshNode(
	UEdGraph* Graph,
	bool bUseAttachedParent,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_CopyPoseFromMesh> NodeCreator(*Graph);
	UAnimGraphNode_CopyPoseFromMesh* CopyPoseNode = NodeCreator.CreateNode();

	if (!CopyPoseNode)
	{
		OutError = TEXT("Failed to create CopyPoseFromMesh node");
		return nullptr;
	}

	CopyPoseNode->NodePosX = static_cast<int32>(Position.X);
	CopyPoseNode->NodePosY = static_cast<int32>(Position.Y);
	CopyPoseNode->Node.bUseAttachedParent = bUseAttachedParent;

	NodeCreator.Finalize();

	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("CopyPose"), TEXT(""), Graph);
	FAnimGraphEditor::SetNodeId(CopyPoseNode, OutNodeId);

	Graph->Modify();

	return CopyPoseNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateModifyBoneNode(
	UEdGraph* Graph,
	const FName& BoneName,
	const FRotator& Rotation,
	const FVector& Translation,
	EBoneModificationMode RotationMode,
	EBoneModificationMode TranslationMode,
	EBoneControlSpace RotationSpace,
	EBoneControlSpace TranslationSpace,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_ModifyBone> NodeCreator(*Graph);
	UAnimGraphNode_ModifyBone* ModBoneNode = NodeCreator.CreateNode();

	if (!ModBoneNode)
	{
		OutError = TEXT("Failed to create ModifyBone node");
		return nullptr;
	}

	ModBoneNode->NodePosX = static_cast<int32>(Position.X);
	ModBoneNode->NodePosY = static_cast<int32>(Position.Y);

	// Set FAnimNode_ModifyBone properties
	ModBoneNode->Node.BoneToModify.BoneName = BoneName;
	ModBoneNode->Node.Rotation = Rotation;
	ModBoneNode->Node.Translation = Translation;
	ModBoneNode->Node.RotationMode = RotationMode;
	ModBoneNode->Node.TranslationMode = TranslationMode;
	ModBoneNode->Node.RotationSpace = RotationSpace;
	ModBoneNode->Node.TranslationSpace = TranslationSpace;
	ModBoneNode->Node.ScaleMode = BMM_Ignore;

	NodeCreator.Finalize();

	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("ModBone"), BoneName.ToString(), Graph);
	FAnimGraphEditor::SetNodeId(ModBoneNode, OutNodeId);

	Graph->Modify();

	return ModBoneNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateTwoBoneIKNode(
	UEdGraph* Graph,
	const FName& IKBoneName,
	const FName& EffectorBoneName,
	EBoneControlSpace EffectorLocationSpace,
	const FName& JointTargetBoneName,
	bool bAllowStretching,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_TwoBoneIK> NodeCreator(*Graph);
	UAnimGraphNode_TwoBoneIK* IKNode = NodeCreator.CreateNode();

	if (!IKNode)
	{
		OutError = TEXT("Failed to create TwoBoneIK node");
		return nullptr;
	}

	IKNode->NodePosX = static_cast<int32>(Position.X);
	IKNode->NodePosY = static_cast<int32>(Position.Y);

	IKNode->Node.IKBone.BoneName = IKBoneName;
	IKNode->Node.EffectorTarget.bUseSocket = false;
	IKNode->Node.EffectorTarget.BoneReference.BoneName = EffectorBoneName;
	IKNode->Node.EffectorLocationSpace = EffectorLocationSpace;

	if (JointTargetBoneName != NAME_None)
	{
		IKNode->Node.JointTarget.bUseSocket = false;
		IKNode->Node.JointTarget.BoneReference.BoneName = JointTargetBoneName;
	}

	IKNode->Node.bAllowStretching = bAllowStretching;

	NodeCreator.Finalize();

	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("TwoBoneIK"), IKBoneName.ToString(), Graph);
	FAnimGraphEditor::SetNodeId(IKNode, OutNodeId);

	Graph->Modify();

	return IKNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateControlRigNode(
	UEdGraph* Graph,
	TSubclassOf<UControlRig> ControlRigClass,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_ControlRig> NodeCreator(*Graph);
	UAnimGraphNode_ControlRig* CRNode = NodeCreator.CreateNode();
	if (!CRNode)
	{
		OutError = TEXT("Failed to create ControlRig node");
		return nullptr;
	}

	CRNode->Node.SetControlRigClass(ControlRigClass);
	CRNode->NodePosX = static_cast<int32>(Position.X);
	CRNode->NodePosY = static_cast<int32>(Position.Y);
	NodeCreator.Finalize();

	CRNode->ReconstructNode();

	FProperty* CustomPinProp = UAnimGraphNode_CustomProperty::StaticClass()->FindPropertyByName(TEXT("CustomPinProperties"));
	FProperty* NodeProp = CRNode->GetClass()->FindPropertyByName(TEXT("Node"));
	if (CustomPinProp && NodeProp)
	{
		TArray<FOptionalPinFromProperty>* PinProps = CustomPinProp->ContainerPtrToValuePtr<TArray<FOptionalPinFromProperty>>(CRNode);
		void* NodePtr = NodeProp->ContainerPtrToValuePtr<void>(CRNode);
		FProperty* InputMappingProp = FAnimNode_ControlRig::StaticStruct()->FindPropertyByName(TEXT("InputMapping"));
		TMap<FName, FName>* InputMapping = InputMappingProp ? InputMappingProp->ContainerPtrToValuePtr<TMap<FName, FName>>(NodePtr) : nullptr;

		if (PinProps && InputMapping)
		{
			for (FOptionalPinFromProperty& Pin : *PinProps)
			{
				Pin.bShowPin = true;
				InputMapping->Add(Pin.PropertyName, NAME_None);
			}
			CRNode->ReconstructNode();
		}
	}

	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("CtrlRig"), TEXT(""), Graph);
	FAnimGraphEditor::SetNodeId(CRNode, OutNodeId);
	Graph->Modify();

	return CRNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateLayeredBoneBlendNode(
	UEdGraph* Graph,
	const FName& BoneName,
	int32 BlendDepth,
	bool bMeshSpaceRotationBlend,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> NodeCreator(*Graph);
	UAnimGraphNode_LayeredBoneBlend* BlendNode = NodeCreator.CreateNode();

	if (!BlendNode)
	{
		OutError = TEXT("Failed to create LayeredBoneBlend node");
		return nullptr;
	}

	BlendNode->NodePosX = static_cast<int32>(Position.X);
	BlendNode->NodePosY = static_cast<int32>(Position.Y);

	BlendNode->Node.AddPose();

	TArray<FString> BoneEntries;
	BoneName.ToString().ParseIntoArray(BoneEntries, TEXT(","));
	for (const FString& Entry : BoneEntries)
	{
		FString TrimmedEntry = Entry.TrimStartAndEnd();
		FString BoneStr;
		int32 PerBoneDepth = BlendDepth;
		if (TrimmedEntry.Split(TEXT(":"), &BoneStr, &TrimmedEntry))
		{
			BoneStr = BoneStr.TrimStartAndEnd();
			PerBoneDepth = FCString::Atoi(*TrimmedEntry.TrimStartAndEnd());
		}
		else
		{
			BoneStr = TrimmedEntry;
		}
		FBranchFilter BranchFilter;
		BranchFilter.BoneName = FName(*BoneStr);
		BranchFilter.BlendDepth = PerBoneDepth;
		BlendNode->Node.LayerSetup[0].BranchFilters.Add(BranchFilter);
	}

	BlendNode->Node.bMeshSpaceRotationBlend = bMeshSpaceRotationBlend;

	NodeCreator.Finalize();
	BlendNode->ReconstructNode();

	FString FirstBone = BoneEntries[0].TrimStartAndEnd();
	FString IdBone;
	if (!FirstBone.Split(TEXT(":"), &IdBone, nullptr))
	{
		IdBone = FirstBone;
	}
	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("LayeredBlend"), IdBone, Graph);
	FAnimGraphEditor::SetNodeId(BlendNode, OutNodeId);

	Graph->Modify();

	return BlendNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateAimOffsetNode(
	UEdGraph* Graph,
	UAimOffsetBlendSpace* AimOffset,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return nullptr;
	}

	if (!AimOffset)
	{
		OutError = TEXT("Invalid AimOffsetBlendSpace");
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_RotationOffsetBlendSpace> NodeCreator(*Graph);
	UAnimGraphNode_RotationOffsetBlendSpace* AONode = NodeCreator.CreateNode();

	if (!AONode)
	{
		OutError = TEXT("Failed to create AimOffset node");
		return nullptr;
	}

	AONode->NodePosX = static_cast<int32>(Position.X);
	AONode->NodePosY = static_cast<int32>(Position.Y);

	AONode->Node.SetBlendSpace(AimOffset);

	NodeCreator.Finalize();

	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("AimOffset"), AimOffset->GetName(), Graph);
	FAnimGraphEditor::SetNodeId(AONode, OutNodeId);

	Graph->Modify();

	return AONode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateLocalToComponentNode(
	UEdGraph* Graph,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_LocalToComponentSpace> NodeCreator(*Graph);
	UAnimGraphNode_LocalToComponentSpace* L2CNode = NodeCreator.CreateNode();
	if (!L2CNode)
	{
		OutError = TEXT("Failed to create LocalToComponent node");
		return nullptr;
	}

	L2CNode->NodePosX = static_cast<int32>(Position.X);
	L2CNode->NodePosY = static_cast<int32>(Position.Y);
	NodeCreator.Finalize();

	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("L2C"), TEXT(""), Graph);
	FAnimGraphEditor::SetNodeId(L2CNode, OutNodeId);
	Graph->Modify();

	return L2CNode;
}

UEdGraphNode* FAnimAssetNodeFactory::CreateComponentToLocalNode(
	UEdGraph* Graph,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_ComponentToLocalSpace> NodeCreator(*Graph);
	UAnimGraphNode_ComponentToLocalSpace* C2LNode = NodeCreator.CreateNode();
	if (!C2LNode)
	{
		OutError = TEXT("Failed to create ComponentToLocal node");
		return nullptr;
	}

	C2LNode->NodePosX = static_cast<int32>(Position.X);
	C2LNode->NodePosY = static_cast<int32>(Position.Y);
	NodeCreator.Finalize();

	OutNodeId = FAnimGraphEditor::GenerateAnimNodeId(TEXT("C2L"), TEXT(""), Graph);
	FAnimGraphEditor::SetNodeId(C2LNode, OutNodeId);
	Graph->Modify();

	return C2LNode;
}

bool FAnimAssetNodeFactory::ConnectToOutputPose(
	UEdGraph* StateGraph,
	const FString& AnimNodeId,
	FString& OutError)
{
	if (!StateGraph)
	{
		OutError = TEXT("Invalid state graph");
		return false;
	}

	// Find anim node
	UEdGraphNode* AnimNode = FAnimGraphEditor::FindNodeById(StateGraph, AnimNodeId);
	if (!AnimNode)
	{
		OutError = FString::Printf(TEXT("Animation node not found: %s"), *AnimNodeId);
		return false;
	}

	// Find pose output pin on anim node
	auto PoseOutputConfig = FPinSearchConfig::Output({
		FName("Pose"),
		FName("Output"),
		FName("Output Pose")
	}).WithCategory(UEdGraphSchema_K2::PC_Struct).WithNameContains(TEXT("Pose"));

	UEdGraphPin* PosePin = FAnimNodePinUtils::FindPinWithFallbacks(AnimNode, PoseOutputConfig, &OutError);
	if (!PosePin)
	{
		return false;
	}

	// Find result node
	UEdGraphNode* ResultNode = FAnimNodePinUtils::FindResultNode(StateGraph);
	if (!ResultNode)
	{
		OutError = TEXT("State result node not found");
		return false;
	}

	// Find result input pin
	auto ResultConfig = FPinSearchConfig::Input({
		FName("Result"),
		FName("Pose"),
		FName("Output Pose"),
		FName("InPose")
	}).AcceptAny();

	UEdGraphPin* ResultPin = FAnimNodePinUtils::FindPinWithFallbacks(ResultNode, ResultConfig, &OutError);
	if (!ResultPin)
	{
		return false;
	}

	// Make connection
	PosePin->MakeLinkTo(ResultPin);
	StateGraph->Modify();

	return true;
}

bool FAnimAssetNodeFactory::ClearStateGraph(UEdGraph* StateGraph, FString& OutError)
{
	if (!StateGraph)
	{
		OutError = TEXT("Invalid state graph");
		return false;
	}

	// Collect nodes to remove (exclude result node)
	TArray<UEdGraphNode*> NodesToRemove;
	for (UEdGraphNode* Node : StateGraph->Nodes)
	{
		// Keep result nodes
		if (Node->IsA<UAnimGraphNode_StateResult>() ||
			Node->IsA<UAnimGraphNode_TransitionResult>())
		{
			continue;
		}

		NodesToRemove.Add(Node);
	}

	// Remove nodes
	for (UEdGraphNode* Node : NodesToRemove)
	{
		Node->BreakAllNodeLinks();
		StateGraph->RemoveNode(Node);
	}

	StateGraph->Modify();

	return true;
}
