-- DOFTest.scene 전용: 키보드로 DoF 카메라를 전환한다.
--   1 : CamDOF_Main     (초점 6m, f/1.6)  즉시 컷
--   2 : CamDOF_FarFocus (초점 18m, f/2.0) 즉시 컷
--   F : 두 카메라 사이를 2초 동안 블렌드 (포커스 풀 — 위치와 PostProcess가 함께 보간됨)

local BlendTime = 2.0
local CurrentName = "CamDOF_Main"
local bInitialized = false

local function SwitchTo(Name, Time)
    local Target = GetActorByName(Name)
    if not Target then
        return
    end
    if SetViewTargetWithBlend(Target, Time, "EaseInOut") then
        CurrentName = Name
    end
end

function BeginPlay()
end

function Tick(dt)
    -- PlayerController가 BeginPlay에서 Pawn으로 ViewTarget을 잡을 수 있으므로 첫 Tick에서 덮어쓴다.
    if not bInitialized then
        bInitialized = true
        SwitchTo("CamDOF_Main", 0)
        return
    end

    if IsKeyPressed("1") then
        SwitchTo("CamDOF_Main", 0)
    elseif IsKeyPressed("2") then
        SwitchTo("CamDOF_FarFocus", 0)
    elseif IsKeyPressed("F") and not IsCameraBlending() then
        if CurrentName == "CamDOF_Main" then
            SwitchTo("CamDOF_FarFocus", BlendTime)
        else
            SwitchTo("CamDOF_Main", BlendTime)
        end
    end
end

function EndPlay()
end
