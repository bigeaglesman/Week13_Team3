-- DOFShowcase.scene 전용: 키보드로 DoF 카메라를 전환한다.
--   1 : 초점 5m,   f/1.4, 6날  (기본, 육각 보케)
--   2 : 초점 2.5m, f/2.0, 5날  (근경 막대에 초점, 오각 보케가 크게)
--   3 : 초점 28m,  f/1.4, 8날  (보케 벽에 초점, 근경/초점면이 크게 흐려짐)
--   4 : 초점 5m,   f/1.0 = MinFstop (조리개 완전 개방 → 원형 보케)
--   5 : 1번과 같은 렌즈 값에 FOV 60 (광각이면 CoC가 작아 흐림이 거의 없음을 비교)
--   Q / E : 이전 / 다음 카메라로 1.5초 블렌드 (포커스 풀 — 위치는 모두 같고 PP와 FOV만 보간)

local Cameras = {
    "CamDOF_1_Focus5m_Hex",
    "CamDOF_2_Focus2_5m_Pent",
    "CamDOF_3_Focus28m_Oct",
    "CamDOF_4_Focus5m_Circle",
    "CamDOF_5_Wide60_Compare",
}
local BlendTime = 1.5
local CurrentIndex = 1
local bInitialized = false

local function SwitchTo(Index, Time)
    local Target = GetActorByName(Cameras[Index])
    if not Target then
        return
    end
    if SetViewTargetWithBlend(Target, Time, "EaseInOut") then
        CurrentIndex = Index
    end
end

function BeginPlay()
end

function Tick(dt)
    -- PlayerController가 BeginPlay에서 Pawn으로 ViewTarget을 잡을 수 있으므로 첫 Tick에서 덮어쓴다.
    if not bInitialized then
        bInitialized = true
        SwitchTo(1, 0)
        return
    end

    for Index = 1, #Cameras do
        if IsKeyPressed(tostring(Index)) then
            SwitchTo(Index, 0)
            return
        end
    end

    if IsCameraBlending() then
        return
    end

    if IsKeyPressed("E") then
        SwitchTo(CurrentIndex % #Cameras + 1, BlendTime)
    elseif IsKeyPressed("Q") then
        SwitchTo((CurrentIndex - 2) % #Cameras + 1, BlendTime)
    end
end

function EndPlay()
end
