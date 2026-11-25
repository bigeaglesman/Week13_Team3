#include "EnginePCH.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicsCore.h"
#include "Physics/BodySetup.h"
#include "UObject/Casts.h"
#include "Engine/World.h"
#include "Physics/PhysicsScene.h"
#include "UObject/UObjectIterator.h"

#include <PxPhysicsAPI.h>
#include <cmath>

using namespace physx;

UPhysicalMaterial::~UPhysicalMaterial()
{
	// PhysicsCore가 먼저 내려갔으면 PxPhysics와 함께 이미 해제됐다.
	if (PxMaterialHandle && FPhysicsCore::Get().IsInitialized())
	{
		PxMaterialHandle->release();
	}
	PxMaterialHandle = nullptr;

	// 주인(UBodySetup)보다 먼저 지워지면 주인의 포인터를 비운다. (엔진 종료 때 일괄 삭제)
	if (UBodySetup* Owner = Cast<UBodySetup>(GetOuter()))
	{
		Owner->OnPhysMaterialDestroyed(this);
	}
}

bool UPhysicalMaterial::IsValid() const
{
	return std::isfinite(StaticFriction) && StaticFriction >= 0.0f
		&& std::isfinite(DynamicFriction) && DynamicFriction >= 0.0f
		&& std::isfinite(Restitution) && Restitution >= 0.0f && Restitution <= 1.0f;
}

PxMaterial* UPhysicalMaterial::GetPxMaterial()
{
	if (PxMaterialHandle)
	{
		return PxMaterialHandle;
	}

	FPhysicsCore& Core = FPhysicsCore::Get();
	if (!Core.IsInitialized() || !IsValid())
	{
		return nullptr;
	}

	PxMaterialHandle = Core.GetPhysics()->createMaterial(StaticFriction, DynamicFriction, Restitution);
	if (PxMaterialHandle)
	{
		// 역참조. 접촉 이벤트에서 어느 재질끼리 부딪혔는지 알 때 쓴다.
		PxMaterialHandle->userData = this;
	}
	return PxMaterialHandle;
}

void UPhysicalMaterial::UpdatePxMaterial()
{
	if (!PxMaterialHandle || !IsValid())
	{
		return;
	}

	// PxMaterial은 씬 오브젝트가 아니라 씬 락이 필요 없다. 다음 simulate부터 반영된다.
	PxMaterialHandle->setStaticFriction(StaticFriction);
	PxMaterialHandle->setDynamicFriction(DynamicFriction);
	PxMaterialHandle->setRestitution(Restitution);
}

void UPhysicalMaterial::PostEditChangeProperty(const FProperty& Property)
{
	UpdatePxMaterial();

	if (Property.Name != "VehicleSurfaceType")
	{
		return;
	}

	for (TObjectIterator<UWorld> It; It; ++It)
	{
		UWorld* World = *It;

		if (!World)
		{
			continue;
		}

		if (FPhysicsScene* PhysicsScene = World->GetPhysicsScene())
		{
			PhysicsScene->RefreshVehicleFrictionPairs();
		}
	}
}