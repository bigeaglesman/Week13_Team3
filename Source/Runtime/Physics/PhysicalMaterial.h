#pragma once

#include "UObject/Object.h"
#include "Physics/CollisionTypes.h"
#include "PhysicalMaterial.generated.h"

namespace physx
{
	class PxMaterial;
}

// 두 물리 재질이 접촉했을 때 마찰/반발 계수를 결합하는 방식 (언리얼 EFrictionCombineMode::Type 대응)
// PhysX PxCombineMode::Enum 값(eAVERAGE=0, eMIN=1, eMULTIPLY=2, eMAX=3)과 1:1 일치한다.
enum class EPhysicsCombineMode : uint8
{
	Average = 0,
	Min = 1,
	Multiply = 2,
	Max = 3
};

// 마찰력·반발력·밀도를 정의하는 물리 재질 에셋 (언리얼 UPhysicalMaterial 대응)
UCLASS()
class UPhysicalMaterial : public UObject
{
	GENERATED_BODY()

public:
	UPhysicalMaterial() = default;
	virtual ~UPhysicalMaterial() override;

	// 이 재질에 대응하는 PxMaterial을 반환한다 (없으면 지연 생성).
	physx::PxMaterial* GetPxMaterial();

	// 프로퍼티 변경 내용을 소유 중인 PxMaterial에 즉시 반영한다.
	void UpdatePhysXMaterial();

	// 소유 중인 PxMaterial을 해제한다.
	void ReleasePhysXMaterial();

	virtual void PostEditChangeProperty(const FProperty& Property) override;

	// 물리 재질 정의를 JSON(.physmat) 파일로 저장/불러오기 한다.
	bool SaveToFile(const FString& FilePath, FString& OutError) const;
	bool LoadFromFile(const FString& FilePath, FString& OutError);

	// 경로 기준으로 캐시된 UPhysicalMaterial을 반환하거나, 없으면 파일에서 로드해 캐시한다.
	static UPhysicalMaterial* LoadOrGet(const FString& AssetPath);

	// TermGamePhys()에서 GPhysXSDK->release() 직전에 호출해 모든 PxMaterial을 정리한다.
	static void ReleaseAllPhysXMaterials();

public:
	// 운동 마찰 계수 (PxMaterial::dynamicFriction)
	UPROPERTY(EditAnywhere)
	float Friction = 0.5f;

	// 정지 마찰 계수 (PxMaterial::staticFriction)
	UPROPERTY(EditAnywhere)
	float StaticFriction = 0.5f;

	// 반발 계수 (0 = 비탄성, 1 = 완전 탄성)
	UPROPERTY(EditAnywhere)
	float Restitution = 0.1f;

	// 밀도 (g/cm^3, 언리얼 기본값 1.0 = 물)
	UPROPERTY(EditAnywhere)
	float Density = 1.0f;

	EPhysicsCombineMode FrictionCombineMode = EPhysicsCombineMode::Average;
	EPhysicsCombineMode RestitutionCombineMode = EPhysicsCombineMode::Average;

private:
	physx::PxMaterial* PhysXMaterial = nullptr;
};
