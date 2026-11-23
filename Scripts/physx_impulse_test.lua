-- 9단계 PhysX 힘/충격 API 테스트용 Lua 스크립트
-- Dynamic StaticMesh 액터(또는 진자/체인 끝단 상자)에 붙이고 PIE에서 키를 눌러 확인한다.
-- [1] AddImpulse: 수직 상방 충격 (위로 점프)
-- [2] AddImpulseAtLocation: 윗면 모서리에 수평 충격 (회전 토크 + 날아감)
-- [3] AddForce (누르고 있기): 지속적인 상방 추진력 (중력 이기고 떠오름)

function BeginPlay()
	print("[PhysX ImpulseTest] Attached to " .. tostring(obj.Name))
end

function Tick(dt)
	if IsKeyPressed("1") then
		print("[PhysX ImpulseTest] 1 -> AddImpulse (0, 0, 12)")
		obj:AddImpulse(FVector(0, 0, 12))
	end

	if IsKeyPressed("2") then
		local hitPos = obj.Location + FVector(0, 0, 0.45)
		print("[PhysX ImpulseTest] 2 -> AddImpulseAtLocation (0, 10, 4)")
		obj:AddImpulseAtLocation(FVector(0, 10, 4), hitPos)
	end

	if IsKeyDown("3") then
		obj:AddForce(FVector(0, 0, 25))
	end
end
