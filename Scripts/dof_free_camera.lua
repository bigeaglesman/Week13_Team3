-- DoF 테스트 씬 공용 자유 카메라 (FreeCamera 이름의 ADefaultPawn에 붙인다).
-- 이동은 엔진 PlayerController가 처리한다:  WASD 이동, 마우스 시점 회전 (PIE 뷰포트 클릭으로 마우스 캡처)
-- 이 스크립트가 추가하는 조작:
--   1 ~ 9 : 렌즈 프리셋 적용 (씬의 카메라 액터에서 FOV와 DoF 설정을 복사). 초점은 1초 동안 부드럽게 이동
--   Z / X : 초점 거리 가깝게 / 멀게 (누르고 있기)
--   F     : 자동 초점 — 화면 중앙에 보이는 물체까지의 거리로 초점 이동
--   Q / E : 아래 / 위로 이동
--   R     : 처음 위치와 방향으로 되돌리기

-- 씬마다 있는 카메라 이름. 존재하는 것만 순서대로 프리셋이 된다.
local PresetCandidates = {
    -- DOFShowcase.scene, DOFShowcaseObjects.scene (같은 프리셋 이름 공유)
    --   1~4: Focal Distance 2.5 / 5 / 10 / 20m (f/1.4)
    --   5~7: 초점 5m, Aperture f/1.4 / f/4 / f/11
    --   8  : 초점 5m, f/1.4 + Min F-stop 4.0 (f/4로 클램프)
    "CamDOF_1_Focus2_5m",
    "CamDOF_2_Focus5m",
    "CamDOF_3_Focus10m",
    "CamDOF_4_Focus20m",
    "CamDOF_5_F1_4",
    "CamDOF_6_F4",
    "CamDOF_7_F11",
    "CamDOF_8_F1_4_MinF4",
    -- DOFBokeh.scene
    "CamBokeh_1_Hexagon_6",
    "CamBokeh_2_Pentagon_5",
    "CamBokeh_3_Square_4",
    "CamBokeh_4_Octagon_8",
    "CamBokeh_5_HalfRound_6",
    "CamBokeh_6_Circle",
    "CamBokeh_7_InFocus25m",
}

local Presets = {}
local bInitialized = false
local StartLocation = nil
local StartRotation = nil

local VerticalSpeed = 2.0     -- m/s
local FocusRate = 1.0         -- 초당 초점 거리 배율 변화 (지수)
local FocusPullTime = 1.0     -- 프리셋 전환 시 초점 이동 시간 (초)

local Pull = nil              -- { From, To, FromFOV, ToFOV, T }

local function Smooth(T)
    return T * T * (3.0 - 2.0 * T)
end

local function StartPull(ToFocus, ToFOV)
    Pull = {
        From = obj.CameraFocalDistance,
        To = ToFocus,
        FromFOV = obj.CameraFieldOfView,
        ToFOV = ToFOV or obj.CameraFieldOfView,
        T = 0.0,
    }
end

local function ApplyPreset(Index, bInstant)
    local Source = Presets[Index]
    if not Source then
        return
    end
    local PrevFocus = obj.CameraFocalDistance
    local PrevFOV = obj.CameraFieldOfView
    obj:CopyCameraSettingsFrom(Source)   -- 조리개, 날 수, 센서, 초점 등 렌즈 전체
    if bInstant or PrevFocus <= 0.0 then
        Pull = nil
        return
    end
    -- 초점과 화각만 이전 값에서 목표값으로 서서히 옮긴다 (포커스 풀)
    local ToFocus = obj.CameraFocalDistance
    local ToFOV = obj.CameraFieldOfView
    obj.CameraFocalDistance = PrevFocus
    obj.CameraFieldOfView = PrevFOV
    StartPull(ToFocus, ToFOV)
end

local function AutoFocus()
    local Origin, Forward = obj:GetCameraViewPoint()
    local Hit = Raycast(Origin + Forward * 0.05, Forward)
    if Hit then
        StartPull((Hit - Origin):Length())
    end
end

function BeginPlay()
end

function Tick(dt)
    if not bInitialized then
        bInitialized = true
        for _, Name in ipairs(PresetCandidates) do
            local Camera = GetActorByName(Name)
            if Camera then
                Presets[#Presets + 1] = Camera
            end
        end
        StartLocation = obj.Location
        StartRotation = obj.Rotation
        ApplyPreset(1, true)
        return
    end

    -- 렌즈 프리셋
    for Index = 1, math.min(#Presets, 9) do
        if IsKeyPressed(tostring(Index)) then
            ApplyPreset(Index, false)
        end
    end

    -- 초점 조절
    if IsKeyDown("Z") or IsKeyDown("X") then
        Pull = nil
        local Dir = IsKeyDown("X") and 1.0 or -1.0
        obj.CameraFocalDistance = math.max(obj.CameraFocalDistance * math.exp(Dir * FocusRate * dt), 0.2)
    end
    if IsKeyPressed("F") then
        AutoFocus()
    end

    -- 포커스 풀 진행
    if Pull then
        Pull.T = math.min(Pull.T + dt / FocusPullTime, 1.0)
        local A = Smooth(Pull.T)
        obj.CameraFocalDistance = Pull.From + (Pull.To - Pull.From) * A
        obj.CameraFieldOfView = Pull.FromFOV + (Pull.ToFOV - Pull.FromFOV) * A
        if Pull.T >= 1.0 then
            Pull = nil
        end
    end

    -- 위/아래 이동
    local Up = 0.0
    if IsKeyDown("E") then Up = Up + 1.0 end
    if IsKeyDown("Q") then Up = Up - 1.0 end
    if Up ~= 0.0 then
        local L = obj.Location
        obj.Location = FVector(L.x, L.y, L.z + Up * VerticalSpeed * dt)
    end

    -- 처음 위치로
    if IsKeyPressed("R") then
        obj.Location = StartLocation
        obj.Rotation = StartRotation
    end
end

function EndPlay()
end
