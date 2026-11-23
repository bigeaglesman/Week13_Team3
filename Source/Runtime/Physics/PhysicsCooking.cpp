#include "EnginePCH.h"
#include "Physics/PhysicsCooking.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysXConversion.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <utility>
#include <vector>

using namespace physx;

namespace
{
	bool ToPxPoints(const TArray<FVector>& Points, std::vector<PxVec3>& Out)
	{
		Out.reserve(Points.Num());
		for (const FVector& P : Points)
		{
			const PxVec3 V = ToPx(P);
			if (!V.isFinite())
			{
				return false;
			}
			Out.push_back(V);
		}
		return true;
	}
}

PxConvexMesh* FPhysicsCooking::CookConvexMesh(const TArray<FVector>& Points)
{
	FPhysicsCore& Core = FPhysicsCore::Get();
	if (!Core.IsInitialized() || Points.Num() < 4)
	{
		return nullptr;
	}

	std::vector<PxVec3> PxPoints;
	if (!ToPxPoints(Points, PxPoints))
	{
		return nullptr;
	}

	PxConvexMeshDesc Desc;
	Desc.points.count  = static_cast<PxU32>(PxPoints.size());
	Desc.points.stride = sizeof(PxVec3);
	Desc.points.data   = PxPoints.data();
	// 점 구름에서 껍질을 직접 계산한다. 원점에서 먼 메시도 정밀도가 떨어지지 않게 정점을 옮겨서 계산한다.
	Desc.flags = PxConvexFlag::eCOMPUTE_CONVEX | PxConvexFlag::eSHIFT_VERTICES;
	Desc.vertexLimit = 255;   // PhysX 볼록 메시 한계

	if (!Desc.isValid())
	{
		return nullptr;
	}

	PxConvexMeshCookingResult::Enum Result = PxConvexMeshCookingResult::eSUCCESS;
	PxConvexMesh* Mesh = Core.GetCooking()->createConvexMesh(Desc, Core.GetPhysics()->getPhysicsInsertionCallback(), &Result);
	if (!Mesh)
	{
		HTR_LOG(Error, "[PhysX] Convex mesh cooking failed. Points={}, Result={}", Points.Num(), static_cast<int32>(Result));
	}
	return Mesh;
}

bool FPhysicsCooking::ComputeConvexHull(const TArray<FVector>& Points, TArray<FVector>& OutHullVertices, TArray<uint32>& OutEdges)
{
	OutHullVertices.Empty();
	OutEdges.Empty();

	PxConvexMesh* Mesh = CookConvexMesh(Points);
	if (!Mesh)
	{
		return false;
	}

	const PxVec3* Vertices = Mesh->getVertices();
	for (PxU32 i = 0; i < Mesh->getNbVertices(); ++i)
	{
		OutHullVertices.Add(ToFVector(Vertices[i]));
	}

	// 면(다각형)마다 둘레를 돌며 모서리를 모은다. 이웃한 두 면이 같은 모서리를 공유하므로 한 번만 넣는다.
	const PxU8* IndexBuffer = Mesh->getIndexBuffer();
	std::vector<std::pair<PxU32, PxU32>> Seen;
	for (PxU32 p = 0; p < Mesh->getNbPolygons(); ++p)
	{
		PxHullPolygon Polygon;
		if (!Mesh->getPolygonData(p, Polygon))
		{
			continue;
		}
		for (PxU32 k = 0; k < Polygon.mNbVerts; ++k)
		{
			PxU32 A = IndexBuffer[Polygon.mIndexBase + k];
			PxU32 B = IndexBuffer[Polygon.mIndexBase + (k + 1) % Polygon.mNbVerts];
			if (A > B) std::swap(A, B);
			if (std::find(Seen.begin(), Seen.end(), std::make_pair(A, B)) != Seen.end())
			{
				continue;
			}
			Seen.emplace_back(A, B);
			OutEdges.Add(A);
			OutEdges.Add(B);
		}
	}

	Mesh->release();
	return OutHullVertices.Num() >= 4;
}

PxTriangleMesh* FPhysicsCooking::CookTriangleMesh(const TArray<FVector>& Vertices, const TArray<uint32>& Indices, bool bFlipWinding)
{
	FPhysicsCore& Core = FPhysicsCore::Get();
	if (!Core.IsInitialized() || Vertices.Num() < 3 || Indices.Num() < 3 || Indices.Num() % 3 != 0)
	{
		return nullptr;
	}

	std::vector<PxVec3> PxPoints;
	if (!ToPxPoints(Vertices, PxPoints))
	{
		return nullptr;
	}

	std::vector<PxU32> Triangles;
	Triangles.reserve(Indices.Num());
	const uint32 NumVertices = static_cast<uint32>(Vertices.Num());
	for (int32 i = 0; i < Indices.Num(); i += 3)
	{
		const uint32 I0 = Indices[i];
		const uint32 I1 = Indices[i + 1];
		const uint32 I2 = Indices[i + 2];
		if (I0 >= NumVertices || I1 >= NumVertices || I2 >= NumVertices)
		{
			return nullptr;
		}
		Triangles.push_back(I0);
		Triangles.push_back(bFlipWinding ? I2 : I1);
		Triangles.push_back(bFlipWinding ? I1 : I2);
	}

	PxTriangleMeshDesc Desc;
	Desc.points.count     = static_cast<PxU32>(PxPoints.size());
	Desc.points.stride    = sizeof(PxVec3);
	Desc.points.data      = PxPoints.data();
	Desc.triangles.count  = static_cast<PxU32>(Triangles.size() / 3);
	Desc.triangles.stride = sizeof(PxU32) * 3;
	Desc.triangles.data   = Triangles.data();

	if (!Desc.isValid())
	{
		return nullptr;
	}

	PxTriangleMeshCookingResult::Enum Result = PxTriangleMeshCookingResult::eSUCCESS;
	PxTriangleMesh* Mesh = Core.GetCooking()->createTriangleMesh(Desc, Core.GetPhysics()->getPhysicsInsertionCallback(), &Result);
	if (!Mesh)
	{
		HTR_LOG(Error, "[PhysX] Triangle mesh cooking failed. Triangles={}, Result={}", Desc.triangles.count, static_cast<int32>(Result));
	}
	else if (Result == PxTriangleMeshCookingResult::eLARGE_TRIANGLE)
	{
		// 동작은 하지만 큰 삼각형은 접촉 정밀도가 떨어진다.
		HTR_LOG(Warning, "[PhysX] Triangle mesh has very large triangles. Contacts may be inaccurate.");
	}
	return Mesh;
}
