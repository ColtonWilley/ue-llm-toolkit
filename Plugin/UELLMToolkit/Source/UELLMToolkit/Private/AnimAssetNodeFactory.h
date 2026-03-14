// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;
class UAnimSequence;
class UBlendSpace;
class UBlendSpace1D;
class UAnimGraphNode_ModifyBone;
class UAnimGraphNode_TwoBoneIK;
class UAnimGraphNode_LayeredBoneBlend;
class UControlRig;
class UAimOffsetBlendSpace;
class UAnimGraphNode_LocalToComponentSpace;
class UAnimGraphNode_ComponentToLocalSpace;
enum EBoneModificationMode : int;
enum EBoneControlSpace : int;

/**
 * AnimAssetNodeFactory - Factory for creating animation asset player nodes
 *
 * Responsibilities:
 * - Creating SequencePlayer nodes
 * - Creating BlendSpacePlayer nodes (2D and 1D)
 * - Connecting animation nodes to output poses
 * - Clearing state graphs
 */
class FAnimAssetNodeFactory
{
public:
	/**
	 * Create an animation sequence player node
	 * @param StateGraph State graph to add node to
	 * @param AnimSequence Animation sequence asset
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateAnimSequenceNode(
		UEdGraph* StateGraph,
		UAnimSequence* AnimSequence,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Create a BlendSpace player node (2D)
	 * @param StateGraph State graph to add node to
	 * @param BlendSpace BlendSpace asset
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateBlendSpaceNode(
		UEdGraph* StateGraph,
		UBlendSpace* BlendSpace,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Create a BlendSpace1D player node
	 * @param StateGraph State graph to add node to
	 * @param BlendSpace BlendSpace1D asset
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateBlendSpace1DNode(
		UEdGraph* StateGraph,
		UBlendSpace1D* BlendSpace,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Create a Slot node (for montage playback)
	 * @param Graph Graph to add node to
	 * @param SlotName Slot name (e.g., "DefaultSlot")
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateSlotNode(
		UEdGraph* Graph,
		FName SlotName,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Create an Inertialization node (pose-difference decay blending)
	 * @param Graph Graph to add node to
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateInertializationNode(
		UEdGraph* Graph,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Create a DeadBlending node (velocity extrapolation + cross-fade)
	 * @param Graph Graph to add node to
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateDeadBlendingNode(
		UEdGraph* Graph,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Create a CopyPoseFromMesh node (copies pose from another skeletal mesh component)
	 * @param Graph Graph to add node to
	 * @param bUseAttachedParent If true, automatically uses the parent component's pose
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateCopyPoseFromMeshNode(
		UEdGraph* Graph,
		bool bUseAttachedParent,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Create a Transform (Modify) Bone node
	 * @param Graph Graph to add node to
	 * @param BoneName Bone to modify
	 * @param Rotation Rotation to apply
	 * @param Translation Translation to apply
	 * @param RotationMode How to apply rotation (ignore/replace/additive)
	 * @param TranslationMode How to apply translation (ignore/replace/additive)
	 * @param RotationSpace Coordinate space for rotation
	 * @param TranslationSpace Coordinate space for translation
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateModifyBoneNode(
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
		FString& OutError
	);

	/**
	 * Create a TwoBoneIK node (skeletal control for two-bone IK chain)
	 * @param Graph Graph to add node to
	 * @param IKBoneName End effector bone (tip of the IK chain)
	 * @param EffectorBoneName Bone to use as effector target
	 * @param EffectorLocationSpace Coordinate space for effector
	 * @param JointTargetBoneName Optional bone for joint (pole) target
	 * @param bAllowStretching Whether to allow bones to stretch
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateTwoBoneIKNode(
		UEdGraph* Graph,
		const FName& IKBoneName,
		const FName& EffectorBoneName,
		EBoneControlSpace EffectorLocationSpace,
		const FName& JointTargetBoneName,
		bool bAllowStretching,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Create a Control Rig evaluation node
	 * @param Graph Graph to add node to
	 * @param ControlRigClass Control Rig Blueprint class
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateControlRigNode(
		UEdGraph* Graph,
		TSubclassOf<UControlRig> ControlRigClass,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Create a Layered Bone Blend node (per-bone layer blending)
	 * @param Graph Graph to add node to
	 * @param BoneName Root bone for the blend layer filter
	 * @param BlendDepth Blend depth (0 = all descendants)
	 * @param bMeshSpaceRotationBlend Use mesh-space rotation blending
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateLayeredBoneBlendNode(
		UEdGraph* Graph,
		const FName& BoneName,
		int32 BlendDepth,
		bool bMeshSpaceRotationBlend,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Create an Aim Offset node (mesh-space additive rotation)
	 * @param Graph Graph to add node to
	 * @param AimOffset AimOffsetBlendSpace asset
	 * @param Position Node position
	 * @param OutNodeId Generated node ID
	 * @param OutError Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateAimOffsetNode(
		UEdGraph* Graph,
		UAimOffsetBlendSpace* AimOffset,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	static UEdGraphNode* CreateLocalToComponentNode(
		UEdGraph* Graph,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	static UEdGraphNode* CreateComponentToLocalNode(
		UEdGraph* Graph,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Connect animation node to state output pose
	 * @param StateGraph State graph
	 * @param AnimNodeId Animation node ID
	 * @param OutError Error message if failed
	 * @return True if successful
	 */
	static bool ConnectToOutputPose(
		UEdGraph* StateGraph,
		const FString& AnimNodeId,
		FString& OutError
	);

	/**
	 * Clear all nodes from state graph (except result node)
	 * @param StateGraph State graph to clear
	 * @param OutError Error message if failed
	 * @return True if successful
	 */
	static bool ClearStateGraph(UEdGraph* StateGraph, FString& OutError);
};
