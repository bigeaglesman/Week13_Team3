-- 7단계 PhysX 충돌 필터 & 이벤트(Overlap / Hit) 테스트용 Lua 스크립트

function BeginPlay()
    print("[PhysX Test][BeginPlay] Self=" .. tostring(obj.UUID))
end

function EndPlay()
    print("[PhysX Test][EndPlay] Self=" .. tostring(obj.UUID))
end

function OnBeginOverlap(OtherActor)
    local otherId = OtherActor and tostring(OtherActor.UUID) or "nil"
    print("[PhysX Test][BeginOverlap] Self=" .. tostring(obj.UUID) .. " <-> Other=" .. otherId)
end

function OnEndOverlap(OtherActor)
    local otherId = OtherActor and tostring(OtherActor.UUID) or "nil"
    print("[PhysX Test][EndOverlap] Self=" .. tostring(obj.UUID) .. " <-> Other=" .. otherId)
end

function OnHit(OtherActor)
    local otherId = OtherActor and tostring(OtherActor.UUID) or "nil"
    print("[PhysX Test][Hit] Self=" .. tostring(obj.UUID) .. " <-> Other=" .. otherId)
end
