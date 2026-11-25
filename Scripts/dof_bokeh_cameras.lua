-- DOFBokeh.scene 전용: 조리개 모양별 보케 비교 카메라 전환.
--   1 : 6날 육각형      2 : 5날 오각형      3 : 4날 사각형      4 : 8날 팔각형
--   5 : 6날, 반쯤 둥근 (F = MinF + 0.2)
--   6 : 원형 (F = MinF, 조리개 완전 개방)
--   7 : 비교용 — 점광원(25m)에 초점, 보케 없음
--   Q / E : 이전 / 다음 카메라로 1.0초 블렌드

local Cameras = {
    "CamBokeh_1_Hexagon_6",
    "CamBokeh_2_Pentagon_5",
    "CamBokeh_3_Square_4",
    "CamBokeh_4_Octagon_8",
    "CamBokeh_5_HalfRound_6",
    "CamBokeh_6_Circle",
    "CamBokeh_7_InFocus25m",
}
local BlendTime = 1.0
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
