#pragma once

#include "SkeletalMeshEditorPanel.h"

class FConstraintGraphPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FConstraintGraphPanel(FSkeletalMeshEditor& InEditor);
	bool IsAvailableInCurrentMode() const override;

protected:
	void DrawContent() override;
};
