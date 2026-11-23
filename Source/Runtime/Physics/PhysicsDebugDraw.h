#pragma once

class UWorld;
class FLineBatcher;

// Collision 쇼 플래그: 물리 바디를 만들 컴포넌트들의 충돌 도형을 와이어프레임으로 그린다.
// 시뮬레이션 전(에디터 월드)에도 보이도록 PhysX가 아니라 설계도(도형 데이터)에서 그린다.
// 시뮬레이션 중인 게임 월드는 FPhysicsScene::DrawDebug(PhysX 시각화)가 실제 바디 위치를 그린다.
//   하늘색 = 움직이지 않는 바디(키네마틱), 연두색 = 시뮬레이션 바디. UE의 Collision 색과 같다.
void DrawPhysicsCollision(UWorld* World, FLineBatcher& LineBatcher);
