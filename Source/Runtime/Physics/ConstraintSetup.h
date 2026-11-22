#pragma once

#include "Math/Transform.h"

// 두 바디(뼈)를 잇는 관절 하나의 설계도. UE 4.27의 UPhysicsConstraintTemplate(FConstraintInstance 설정부) 자리다.
// 런타임에는 이걸로 PxD6Joint를 만든다 
//
// 관절 프레임: 두 뼈 각각의 로컬 공간에 놓인 좌표계. 바인드 포즈에서 두 프레임은 월드에서 같은 자리·같은 방향이다.
//   X축 = Twist(비틀림) 축. 뼈 길이 방향으로 둔다.
//   Y축 = Swing1 축, Z축 = Swing2 축. 뼈가 옆으로 꺾이는 방향이다.
// PhysX D6 관절과 축 규칙이 같다.
struct FConstraintSetup
{
	FString JointName;
	FString ChildBoneName;    // UE의 ConstraintBone1
	FString ParentBoneName;   // UE의 ConstraintBone2

	// 위치는 메시 원본 단위. 스케일은 쓰지 않는다.
	FTransform ChildFrame;
	FTransform ParentFrame;

	// 프레임 X축 기준 비틀림 한계 (±도)
	float TwistLimitDeg = 45.0f;
	// 원뿔 한계. Y축(Swing1), Z축(Swing2) 방향으로 몇 도까지 꺾이는지
	float Swing1LimitDeg = 45.0f;
	float Swing2LimitDeg = 45.0f;
};
