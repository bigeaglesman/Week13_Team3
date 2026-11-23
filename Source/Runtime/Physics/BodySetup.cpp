#include "EnginePCH.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicsAsset.h"
#include "Render/Mesh.h"
#include "UObject/Casts.h"

#include "Serialization/MathSerialization.h"
#include "Serialization/StructuredArchiveSlots.h"

namespace
{
	// 회전은 FRotator를 거치지 않고 쿼터니언 그대로 저장한다 (Pitch ±90도 근처에서 값이 튀지 않게).
	void SerializeQuat(FStructuredArchiveSlot Slot, FQuat& Q)
	{
		FStructuredArchiveRecord Record = Slot.EnterRecord();
		Record << SA_VALUE("X", Q.X) << SA_VALUE("Y", Q.Y) << SA_VALUE("Z", Q.Z) << SA_VALUE("W", Q.W);
	}

	// 배열 하나를 "Name": [ {...}, ... ]로 저장·로드한다. 로드할 때는 개수만큼 늘린 뒤 채운다.
	template <typename T, typename FuncType>
	void SerializeElemArray(FStructuredArchive::FRecord Record, const char* Name, TArray<T>& Elems, FuncType&& SerializeElem)
	{
		FArchive& Ar = Record.GetUnderlyingArchive();
		int32 Num = Ar.IsSaving() ? Elems.Num() : 0;
		FStructuredArchive::FArray Array = Record.EnterArray(Name, Num);
		if (Ar.IsLoading())
		{
			Elems.Empty();
			Elems.SetNum(Num);
		}
		for (int32 i = 0; i < Num; ++i)
		{
			SerializeElem(Array.EnterElement().EnterRecord(), Elems[i]);
		}
	}

	void SerializeAggGeom(FStructuredArchive::FRecord Record, FKAggregateGeom& Geom)
	{
		SerializeElemArray(Record, "Spheres", Geom.SphereElems, [](FStructuredArchive::FRecord Elem, FKSphereElem& S)
		{
			Elem << SA_VALUE("Center", S.Center) << SA_VALUE("Radius", S.Radius);
		});

		SerializeElemArray(Record, "Boxes", Geom.BoxElems, [](FStructuredArchive::FRecord Elem, FKBoxElem& B)
		{
			Elem << SA_VALUE("Center", B.Center);
			SerializeQuat(Elem.EnterField("Rotation"), B.Rotation);
			Elem << SA_VALUE("HalfExtent", B.HalfExtent);
		});

		SerializeElemArray(Record, "Sphyls", Geom.SphylElems, [](FStructuredArchive::FRecord Elem, FKSphylElem& S)
		{
			Elem << SA_VALUE("Center", S.Center);
			SerializeQuat(Elem.EnterField("Rotation"), S.Rotation);
			Elem << SA_VALUE("Radius", S.Radius) << SA_VALUE("Length", S.Length);
		});

		SerializeElemArray(Record, "Convexes", Geom.ConvexElems, [](FStructuredArchive::FRecord Elem, FKConvexElem& C)
		{
			FArchive& Ar = Elem.GetUnderlyingArchive();
			int32 Num = Ar.IsSaving() ? C.VertexData.Num() : 0;
			FStructuredArchive::FArray Points = Elem.EnterArray("VertexData", Num);
			if (Ar.IsLoading())
			{
				C.VertexData.Empty();
				C.VertexData.SetNum(Num);
			}
			for (int32 i = 0; i < Num; ++i)
			{
				Points.EnterElement() << C.VertexData[i];
			}
		});

		// TriMeshElems는 메시에서 런타임에 만드는 도형이라 에셋에 저장하지 않는다.
	}
}

// 소유 관계: UPhysicsAsset·UStaticMesh → UBodySetup → UPhysicalMaterial. 주인이 소멸자에서 자식을 지운다.
// 엔진 종료 때 FEngineLoop::DestroyAllObjectsExceptEngine()이 모든 UObject를 끝에서부터 지우므로
// 자식이 먼저 지워질 수 있다. 그때 자식은 주인의 포인터를 비워 이중 삭제를 막는다.
UBodySetup::~UBodySetup()
{
	ClearPhysMaterial();

	if (UPhysicsAsset* Asset = Cast<UPhysicsAsset>(GetOuter()))
	{
		Asset->OnBodySetupDestroyed(this);
	}
	else if (UStaticMesh* Mesh = Cast<UStaticMesh>(GetOuter()))
	{
		Mesh->OnBodySetupDestroyed(this);
	}
}

void UBodySetup::OnPhysMaterialDestroyed(UPhysicalMaterial* Material)
{
	if (PhysMaterial == Material)
	{
		PhysMaterial = nullptr;
	}
	if (DefaultInstance.PhysMaterialOverride == Material)
	{
		DefaultInstance.PhysMaterialOverride = nullptr;
	}
}

UPhysicalMaterial* UBodySetup::GetOrCreatePhysMaterial()
{
	if (!PhysMaterial)
	{
		PhysMaterial = NewObject<UPhysicalMaterial>(this);
		DefaultInstance.PhysMaterialOverride = PhysMaterial;
	}
	return PhysMaterial;
}

void UBodySetup::ClearPhysMaterial()
{
	// 소멸자가 OnPhysMaterialDestroyed로 다시 들어오므로 먼저 비운다.
	UPhysicalMaterial* Material = PhysMaterial;
	PhysMaterial = nullptr;
	if (DefaultInstance.PhysMaterialOverride == Material)
	{
		DefaultInstance.PhysMaterialOverride = nullptr;
	}
	delete Material;
}

void UBodySetup::Serialize(FStructuredArchive::FRecord Record)
{
	// BoneName, DefaultInstance (UPROPERTY)
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	SerializeAggGeom(Record.EnterRecord("AggGeom"), AggGeom);

	// 재질은 있을 때만 쓴다. 없는 파일을 읽으면 기본 재질을 쓴다.
	const bool bHasMaterial = Ar.IsSaving() && PhysMaterial != nullptr;
	if (TOptional<FStructuredArchive::FSlot> MaterialSlot = Record.TryEnterField("PhysMaterial", bHasMaterial))
	{
		if (Ar.IsLoading())
		{
			GetOrCreatePhysMaterial();
		}
		PhysMaterial->Serialize(MaterialSlot.GetValue().EnterRecord());
	}
	else if (Ar.IsLoading())
	{
		ClearPhysMaterial();
	}

	// 런타임 바디가 이 설계도의 재질을 쓰도록 이어 둔다 (DefaultInstance를 복사하면 같이 따라간다).
	DefaultInstance.PhysMaterialOverride = PhysMaterial;
}
