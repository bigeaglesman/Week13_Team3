#pragma once

#include "Core/Types.h"

// 카메라 및 후처리 볼륨이 공유하는 포스트 프로세스 설정.
// 언리얼의 FPostProcessSettings와 같은 역할이며, bOverride_*가 true인 항목만
// 최종 뷰(FSceneView::FinalPostProcessSettings)에 덮어쓰기(블렌딩)된다.
struct FPostProcessSettings
{
	// --- Override 플래그 (체크된 항목만 적용) ---
	bool bOverride_DepthOfFieldFstop = false;
	bool bOverride_DepthOfFieldMinFstop = false;
	bool bOverride_DepthOfFieldBladeCount = false;
	bool bOverride_DepthOfFieldSensorWidth = false;
	bool bOverride_DepthOfFieldFocalDistance = false;
	bool bOverride_DepthOfFieldDepthBlurAmount = false;
	bool bOverride_DepthOfFieldDepthBlurRadius = false;

	// --- Camera (렌즈/조리개) ---
	// 조리개 F값. 작을수록 아웃포커싱이 강해진다.
	float DepthOfFieldFstop = 4.0f;
	// 최대 개방(최소 F값). Fstop의 하한 클램프이자 보케 모양이 원으로 변하는 기준.
	float DepthOfFieldMinFstop = 1.2f;
	// 조리개 날 수 (4~16). 다각형 보케의 꼭짓점 수.
	int32 DepthOfFieldBladeCount = 5;
	// 이미지 센서 가로 크기 (mm). 초점거리(FocalLength) 계산에 사용.
	float DepthOfFieldSensorWidth = 24.576f;

	// --- Depth of Field ---
	// 초점이 맞는 거리 (m 단위, 언리얼은 cm). 0이면 DoF 패스를 실행하지 않는다.
	float DepthOfFieldFocalDistance = 0.0f;
	// 원거리 흐림(Depth Blur)이 50%에 도달하는 거리 (km).
	float DepthOfFieldDepthBlurAmount = 1.0f;
	// 원거리 흐림 최대 반경 (1920px 가로 해상도 기준 픽셀).
	float DepthOfFieldDepthBlurRadius = 0.0f;
};
