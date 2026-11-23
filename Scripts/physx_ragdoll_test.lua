-- 11단계 PhysX 래그돌 테스트용 Lua 스크립트
-- 캐릭터(AMyCharacter)에 연결하고 PIE에서 키를 눌러 확인한다.
-- [R] 래그돌 On / Off 토글
-- [F] 래그돌 전환 + 전방/상방 전체 충격 (AddImpulse)
-- [G] 래그돌 전환 + 상체 피격 충격 (AddImpulseAtLocation)

local bRagdoll = false

function BeginPlay()
	bRagdoll = false
	print("[PhysX RagdollTest] Ready. Press R=Ragdoll toggle, F=Launch, G=UpperBody Hit")
end

function Tick(dt)
	if IsKeyPressed("r") then
		bRagdoll = not bRagdoll
		print("[PhysX RagdollTest] SetRagdoll(" .. tostring(bRagdoll) .. ")")
		obj:SetRagdoll(bRagdoll)
	end

	if IsKeyPressed("f") then
		if not bRagdoll then
			bRagdoll = true
			obj:SetRagdoll(true)
		end
		print("[PhysX RagdollTest] AddImpulse (0, 12, 8)")
		obj:AddImpulse(FVector(0, 12, 8))
	end

	if IsKeyPressed("g") then
		if not bRagdoll then
			bRagdoll = true
			obj:SetRagdoll(true)
		end
		local hitPos = obj.Location + FVector(0, 0, 0.5)
		print("[PhysX RagdollTest] AddImpulseAtLocation (0, 25, 6)")
		obj:AddImpulseAtLocation(FVector(0, 25, 6), hitPos)
	end
end
