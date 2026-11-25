#pragma once

#include "Math/Vector.h"
#include "ClothConfigNv.generated.h"

// Phase Constraint 한 종류의 설정. NvCloth의 PhaseConfig로 그대로 들어간다. UE 4.27의 FClothConstraintSetupNv 자리다.
USTRUCT()
struct FClothConstraintSetupNv
{
	GENERATED_BODY()

	// 한 번 반복할 때 제약을 얼마나 맞출지 (0~1). 1이면 매번 원래 길이로 되돌린다.
	UPROPERTY(EditAnywhere)
	float Stiffness = 1.0f;

	// Stretch/Compression 한계를 넘었을 때 Stiffness에 곱하는 값 (0~1)
	UPROPERTY(EditAnywhere)
	float StiffnessMultiplier = 1.0f;

	// 원래 길이 대비 이 배율까지는 늘어나도 봐준다 (1 = 전혀 안 늘어남)
	UPROPERTY(EditAnywhere)
	float StretchLimit = 1.0f;

	// 원래 길이 대비 이 배율까지는 줄어들어도 봐준다 (1 = 전혀 안 줄어듦)
	UPROPERTY(EditAnywhere)
	float CompressionLimit = 1.0f;
};

// 천 하나의 시뮬레이션 설정. 컴포넌트의 디테일 패널에서 고치고, 매 프레임 천에 다시 넣으므로 PIE 중에도 바로 반영된다.
// UE 4.27의 UClothConfigNv 자리다. (UE는 천 에셋이 가진 UObject지만, 여기서는 천 에셋을 자동으로 만들므로 컴포넌트가 갖는다)
USTRUCT()
struct FClothConfigNv
{
	GENERATED_BODY()

	// ---- Phase Constraint 4종 (NvCloth 쿠커가 삼각형 연결을 보고 자동으로 나눈다) ----

	// 세로(중력 방향) 늘어남
	UPROPERTY(EditAnywhere)
	FClothConstraintSetupNv VerticalConstraintConfig;

	// 가로 늘어남
	UPROPERTY(EditAnywhere)
	FClothConstraintSetupNv HorizontalConstraintConfig;

	// 접힘·구부러짐
	UPROPERTY(EditAnywhere)
	FClothConstraintSetupNv BendConstraintConfig;

	// 대각선 찌그러짐
	UPROPERTY(EditAnywhere)
	FClothConstraintSetupNv ShearConstraintConfig;

	// ---- 솔버 ----

	// 1초에 제약을 몇 번 풀지. 높을수록 덜 늘어나고 무거워진다
	UPROPERTY(EditAnywhere)
	float SolverFrequency = 120.0f;

	// Stiffness·Damping 값의 기준 주파수. SolverFrequency를 바꿔도 같은 느낌이 나게 한다
	UPROPERTY(EditAnywhere)
	float StiffnessFrequency = 100.0f;

	// ---- 움직임 ----

	// 입자 속도 감쇠 (0 = 그대로, 1 = 멈춤)
	UPROPERTY(EditAnywhere)
	float Damping = 0.4f;

	// 컴포넌트가 움직일 때 천이 따라 끌려가는 정도 (0 = 영향 없음)
	UPROPERTY(EditAnywhere)
	float LinearDrag = 0.2f;

	UPROPERTY(EditAnywhere)
	float AngularDrag = 0.2f;

	// 컴포넌트 가속이 천에 전해지는 비율 (1 = 물리적으로 정확, 0 = 캐릭터가 뛰어도 천이 안 흔들림). UE의 Inertia Scale
	UPROPERTY(EditAnywhere)
	float LinearInertiaScale = 1.0f;

	UPROPERTY(EditAnywhere)
	float AngularInertiaScale = 1.0f;

	UPROPERTY(EditAnywhere)
	float CentrifugalInertiaScale = 1.0f;

	// 월드 중력(-9.81 m/s²)에 곱하는 값
	UPROPERTY(EditAnywhere)
	float GravityScale = 1.0f;

	// 섹션 빌더가 정한 Max Distance에 곱하는 값 (0이면 모든 입자가 스키닝 위치에 붙는다)
	UPROPERTY(EditAnywhere)
	float MaxDistanceScale = 1.0f;

	// 고정 입자에서 너무 멀어지지 않게 잡아 주는 줄(tether)의 세기와 허용 길이 배율
	UPROPERTY(EditAnywhere)
	float TetherStiffness = 1.0f;

	UPROPERTY(EditAnywhere)
	float TetherLimit = 1.0f;

	// ---- 충돌 (Physics Asset의 구·캡슐) ----

	// 몸과 부딪힐지. 끄면 천이 몸을 뚫고 지나간다
	UPROPERTY(EditAnywhere)
	bool bEnableCollision = true;

	// 충돌체 반지름에 더하는 여유 (월드 단위). 천이 몸에 너무 붙어 파고드는 것처럼 보일 때 키운다
	UPROPERTY(EditAnywhere)
	float CollisionThickness = 0.01f;

	// 충돌체 표면을 미끄러지는 정도 (0 = 미끄러움, 1 = 달라붙음)
	UPROPERTY(EditAnywhere)
	float Friction = 0.1f;

	// ---- 바람 (NvCloth 내장: 삼각형마다 공기 저항과 양력) ----

	// 월드 공간 바람 속도 (m/s). 예: (0, 5, 0)이면 +Y로 분다. 0이면 바람 없음
	UPROPERTY(EditAnywhere)
	FVector WindVelocity = FVector(0.0f, 0.0f, 0.0f);

	// 바람을 받는 정도. 공기 저항(면에 수직)과 양력(면을 따라)
	UPROPERTY(EditAnywhere)
	float WindDragCoefficient = 0.5f;

	UPROPERTY(EditAnywhere)
	float WindLiftCoefficient = 0.3f;
};
