#pragma once

#include "SkeletalMeshEditorPanel.h"

class UBodySetup;

// 메시 상세 정보 UI를 확장하기 위한 패널 골격입니다.
class FSkeletalMeshDetailsPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshDetailsPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;

private:
	void DrawPhysicsAssetDetails();
	void DrawBodyShapes(UBodySetup& Setup, int32 BoneIndex);
	void DrawAddJointMenu(int32 ChildBone);
};
