#pragma once

#include "SceneComponent.h"
#include "../Render/Shader.h"
#include "../Render/Mesh.h"
#include "Render/RenderPacket.h"
#include "Render/GeometryGenerator.h"
#include "Collision/HitResult.h"
#include "Physics/BodyInstance.h"
#include "Core/Delegate/Delegate.h"
#include "PrimitiveComponent.generated.h"

class FPrimitiveSceneProxy;
class AActor;
class UPrimitiveComponent;

// (Self, Other, OtherComp). 담당: C
DECLARE_DELEGATE(FComponentOverlapSignature, UPrimitiveComponent*, AActor*, UPrimitiveComponent*);
DECLARE_DELEGATE(FComponentHitSignature, UPrimitiveComponent*, AActor*, UPrimitiveComponent*);

struct FOverlapInfo
{
	UPrimitiveComponent* OtherComp = nullptr;
};

enum class EPrimitiveType
{
	Sphere,
	Cube,
	Cone,
	Plane,

	Particle,
	ParticleQuad
};

UCLASS()
class UPrimitiveComponent :public USceneComponent
{
	GENERATED_BODY()

	// Material은 여기서 등록하지 않는다. 컴포넌트마다 머티리얼을 다루는 방식이 달라서
	// (빌보드는 단일 머티리얼, 스태틱 메시는 슬롯별 덮어쓰기) 각자 등록한다.
public:
	UPrimitiveComponent();
	virtual ~UPrimitiveComponent();

	virtual void BeginPlay() override;

	// Todo: subuv
	//void SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue);
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext);

	virtual const FStaticMeshData* GetMeshData() const { return nullptr; }

	// Outline처럼 컴포넌트를 통째로 한 번 더 그릴 때 쓰는 GPU 메시.
	// 메시가 없거나(텍스트) View에 따라 형상이 정해지는 컴포넌트(빌보드·파티클)는
	// 컴포넌트 월드 행렬만으로 같은 그림을 못 만들므로 nullptr을 돌려준다.
	virtual UStaticMesh* GetRenderMesh() const { return nullptr; }

	virtual int32 GetNumMaterials() const { return 0; }
	virtual UMaterial* GetMaterial(int32 SlotIndex) const { return nullptr; }
	virtual void SetMaterial(int32 SlotIndex, UMaterial* InMaterial) {}
	//
	// FShader* GetShader() const { return Shader.get(); };

	void SetVisible(bool bInVisible) { SetVisibility(bInVisible); }

	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit);
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT);
	virtual FBox CalcLocalBounds() const override
	{
		const FStaticMeshData* Data = GetMeshData();
		return Data ? Data->AABB : Super::CalcLocalBounds();
	}

	virtual FPrimitiveSceneProxy* CreateSceneProxy();
	FPrimitiveSceneProxy* GetSceneProxy() const { return SceneProxy; }

	void MarkRenderStateDirty() override;

	virtual void OnTransformDirty() override;

	virtual bool MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit = nullptr) override;

	virtual void PostEditChangeProperty(const FProperty& Property) override;

	void SetCastShadow(bool NewValue);
	void SetReceiveShadow(bool NewValue);

	UPROPERTY(EditAnywhere)
	bool bCastShadow = true;
	UPROPERTY(EditAnywhere)
	bool bReceiveShadow = true;
	UPROPERTY(EditAnywhere)
	bool bReceivesDecals = true;

	// ---- 충돌 이벤트 (담당: C) ----
	FComponentOverlapSignature OnComponentBeginOverlap;
	FComponentOverlapSignature OnComponentEndOverlap;
	FComponentHitSignature OnComponentHit;

	UPROPERTY(EditAnywhere)
	bool bGenerateOverlapEvents = true;
	UPROPERTY(EditAnywhere)
	bool bBlockComponent = false;

	// 이 컴포넌트의 충돌 오브젝트 채널 (기본값: WorldStatic)
	ECollisionChannel CollisionObjectType = ECollisionChannel::ECC_WorldStatic;

	UPROPERTY(EditAnywhere)
	int32 TranslucencySortPriority = 0;

	bool IsOverlappingComponent(const UPrimitiveComponent* Other) const;
	const TArray<FOverlapInfo>& GetOverlapInfos() const { return OverlapInfos; }

	// FPhysScene 이벤트 콜백(FlushSimulationEvents)에서 호출한다.
	void BeginComponentOverlap(UPrimitiveComponent* OtherComp);
	void EndComponentOverlap(UPrimitiveComponent* OtherComp);
	void DispatchHit(UPrimitiveComponent* OtherComp);

	// 현재 충돌 설정(채널, Block/Overlap, ActorEnableCollision)을 PhysX Shape에 반영한다.
	void UpdateCollisionFilter();
	void ClearOverlaps();
	// ---- 물리 시뮬레이션 설정 ----
	UPROPERTY(EditAnywhere)
	bool bSimulatePhysics = false;
	UPROPERTY(EditAnywhere)
	bool bEnableGravity = true;
	UPROPERTY(EditAnywhere)
	float MassInKg = 1.0f;
	UPROPERTY(EditAnywhere)
	float LinearDamping = 0.01f;
	UPROPERTY(EditAnywhere)
	float AngularDamping = 0.05f;

	virtual void SetSimulatePhysics(bool bSimulate);
	virtual bool IsSimulatingPhysics() const;
	void SetEnableGravity(bool bGravityEnabled);
	bool IsGravityEnabled() const { return bEnableGravity; }

	virtual void AddForce(const FVector& Force, bool bAccelChange = false);
	virtual void AddImpulse(const FVector& Impulse, bool bVelChange = false);
	virtual void AddImpulseAtLocation(const FVector& Impulse, const FVector& Location);

	// PhysX 시뮬레이션 결과(위치·회전·속도)를 이 컴포넌트의 Transform에 반영한다.
	virtual void SyncComponentToRBPhysics();

	FBodyInstance BodyInstance;
	FBodyInstance* GetBodyInstance() { return &BodyInstance; }
	const FBodyInstance* GetBodyInstance() const { return &BodyInstance; }

protected:
	virtual void OnUnregister() override;
	TArray<FOverlapInfo> OverlapInfos;

	virtual bool ShouldCreateRenderState() const override	
	{
		return true;
	}

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;

	virtual bool ShouldCreatePhysicsState() const override { return true; }
	virtual bool BuildBodyShapeDesc(FBodyShapeDesc& OutDesc) const { return false; }
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;

	bool TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult);
	bool TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT);

private:
	friend class FScene;
	FPrimitiveSceneProxy* SceneProxy = nullptr;
	FVector CachedBodyScale = FVector::OneVector;
	bool bUpdatingFromPhysics = false;
};