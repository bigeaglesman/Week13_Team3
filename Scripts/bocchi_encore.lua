-- Listed before the truck so a checkpoint places its chassis before PhysX creates it.
local truck, bandCamera, boxCamera, stuntCamera, revealCamera, shirtCamera
local hud, speaker, subtitle, hint
local phase, elapsed = "initializing", 0
local lastPosition, landingCameraPosition
local lastCue
local pick, pickOrigin, pickVelocity, pickYaw, pickTime
local peeked = false
local concertShots = { "EncoreLiveWide", "EncoreLiveGuitars", "EncoreLiveRhythm", "EncoreLiveHero" }
local concertShot = 1
local beat = 60 / 132
local voices = {
    ["니지카"] = "EncoreNijika",
    ["료"] = "EncoreRyo",
    ["키타"] = "EncoreKita",
    ["히토리"] = "EncoreHitori",
}

local function cue(who, text, controls)
    if lastCue == text then return end
    lastCue = text
    if hud then
        speaker:SetText(who)
        subtitle:SetText(text)
        hint:SetText(controls or "")
    end
    local voice = voices[who]
    if voice and text ~= "" then
        PlaySound2D("Assets/Sounds/" .. voice .. ".wav", 0.5)
    end
end

local function enterPhase(name)
    phase, elapsed = name, 0
end

local function onboard(x, y, z)
    local angle = math.rad(truck.Rotation.yaw)
    local c, s = math.cos(angle), math.sin(angle)
    return truck.Location + FVector(x*c-y*s, x*s+y*c, z)
end

local function aim(camera, eye, target)
    local rotation = FRotator(0, 0, 0)
    rotation:LookDirection(target-eye)
    SetCameraRelativeTransform(camera, eye, rotation)
end

local function launchPick(dt, p)
    local bump = lastPosition.x < 17.5 and p.x >= 17.5 and math.abs(p.y) < 6
    local loadingRamp = lastPosition.y < 40 and p.y >= 40 and p.x > 50 and p.x < 66
    if not pickOrigin and (bump or loadingRamp) then
        pick = GetComponentByName("LooseGuitarPick")
        pickOrigin = onboard(-1.9, -0.72, 2.52)
        local chassisVelocity = (p - lastPosition) / math.max(dt, 0.001)
        pickVelocity = FVector(chassisVelocity.x, chassisVelocity.y, 0) + (onboard(-3.1, -2.4, 4.9) - p)
        pickTime, pickYaw = 0, truck.Rotation.yaw
        if bump then
            cue("적재함", "덜컹. 망고 상자 안에서 기타 피크 하나가 튀었다.", "과속방지턱    |    뒷바퀴와 셔츠 밑단")
        end
    end
end

local function updatePick(dt)
    if not pick or not pickTime then return end
    local t = pickTime
    local pos = pickOrigin + pickVelocity * t - FVector(0, 0, 4.905 * t * t)
    if t > 0.5 and pos.z < 0.45 then
        local surface = Raycast(FVector(pos.x, pos.y, 0.5), FVector(0, 0, -1))
        local ground = surface and surface.z or 0
        if pos.z <= ground + 0.012 then
            pick.Location = FVector(pos.x, pos.y, ground + 0.012)
            pick.Rotation = FRotator(90, pickYaw + 30, 0)
            pickTime = nil
            return
        end
    end
    pick.Location = pos
    pick.Rotation = FRotator(22 * math.sin(t * 8), pickYaw + 20 + 180 * t, 18 + 270 * t)
    pickTime = t + dt
end

local function drive(checkpoint)
    SetVehicleDriverInputEnabled(truck, true)
    truck:ResetCameraLag()
    SetViewTargetWithBlend(truck, checkpoint and 0 or 0.7, "Cubic")
    lastPosition = truck.Location
    enterPhase("road")
    cue("결속밴드 장비차", checkpoint and "하역장 진입. 정지선에서 급정거!" or "공연장 뒤편 하역장까지 장비를 배달해 주세요.",
        "W·S 가속 / 감속    A·D 조향    Space 급정거    R 하역장부터")
end

function BeginPlay()
    truck = GetActorByName("EncoreTruck")
    bandCamera = GetActorByName("BandCamera")
    boxCamera = GetActorByName("CargoCamera")
    stuntCamera = GetActorByName("StuntCamera")
    revealCamera = GetActorByName("RevealCamera")
    shirtCamera = GetActorByName("ShirtCamera")
    hud = CreateWidget("Assets/UI/EncoreCaptions.widget")
    if hud then
        speaker, subtitle, hint = hud:Find("Speaker"), hud:Find("Subtitle"), hud:Find("Hint")
        hud:AddToViewport(3)
    end
    if type(SetVehicleDriverInputEnabled) ~= "function" or type(GetBoneWorldLocation) ~= "function" then
        SetViewTargetWithBlend(bandCamera, 0)
        cue("앙코르 시연", "이 에디터에는 앙코르 차량·스턴트 기능이 없습니다.", "PIE 종료 후 새로 빌드한 에디터에서 씬을 열어 주세요.")
        return
    end
    SetGameValue("EncoreDive", 0)
    SetGameValue("EncoreReveal", 0)
    SetGameValue("EncoreShirtDrop", 0)
    SetGameValue("EncoreConcert", 0)
    local checkpoint = GetGameValue("EncoreCheckpoint", 0) == 1
    SetGameValue("EncoreCheckpoint", 0)
    if checkpoint then
        local mark = GetActorByName("BrakeRehearsalStart")
        truck.Location, truck.Rotation = mark.Location, mark.Rotation
        drive(true)
    else
        SetVehicleDriverInputEnabled(truck, false)
        SetViewTargetWithBlend(bandCamera, 0)
        enterPhase("intro")
        cue("니지카", "기타, 앰프, 굿즈. 다 실었지?", "앙코르 90초 전    |    Enter 출발")
    end
end

local function dive()
    SetVehicleDriverInputEnabled(truck, false)
    SetGameValue("EncoreDive", 1)
    landingCameraPosition = onboard(6, -8, 2.65)
    local hips = GetBoneWorldLocation(truck, "DivingMannequin", "pelvis")
    aim(stuntCamera, landingCameraPosition, hips or onboard(1, 0, 2))
    SetViewTargetWithBlend(stuntCamera, 0.18, "Cubic")
    cue("키타", "히토리 선배가 먼저 무대로 뛰어들었어요!")
    enterPhase("dive")
end

local function trackPassenger(dt)
    local hips = GetBoneWorldLocation(truck, "DivingMannequin", "pelvis")
    if hips then
        -- Keep the moving ragdoll on screen even for a late or angled stop.
        local offset = phase == "inspect" and onboard(0.75, -2.9, 1.2) or onboard(1.6, -6.3, 2.1)
        local desired = hips + (offset - truck.Location)
        landingCameraPosition = landingCameraPosition + (desired-landingCameraPosition)*math.min(dt*2,1)
        aim(stuntCamera, landingCameraPosition, hips + FVector(0, 0, 0.25))
    end
end

function Tick(dt)
    if phase == "initializing" or phase == "travel" then return end
    elapsed = elapsed + dt
    if IsKeyPressed("R") or IsKeyPressed("T") then
        -- Level travel rebuilds vehicle, joints and cloth together.
        SetGameValue("EncoreCheckpoint", IsKeyPressed("R") and 1 or 0)
        enterPhase("travel")
        OpenLevel("BocchiEncore")
        return
    end
    updatePick(dt)

    if phase == "intro" then
        if elapsed > 12 or IsKeyPressed("enter") then
            drive(false)
        elseif elapsed > 9 and not peeked then
            peeked = true
            aim(revealCamera, onboard(-3.5, 2.7, 3.0), onboard(-1.55, 0, 2.35))
            SetViewTargetWithBlend(revealCamera, 0.65, "Cubic")
        elseif elapsed > 6.5 and lastCue ~= "분명히 다 실었는데... 히토리는?" then
            SetViewTargetWithBlend(boxCamera, 1.3, "Cubic")
            cue("니지카", "분명히 다 실었는데... 히토리는?", "Enter 출발")
        elseif elapsed > 3 and elapsed <= 6.5 then
            cue("료", "응. 무거운 건 분명히 다 실었어.", "Enter 출발")
        end
    elseif phase == "road" then
        local p = truck.Location
        local speed = (p-lastPosition):Length() / math.max(dt, 0.001)
        launchPick(dt, p)
        lastPosition = p
        if p.x > 49 and p.x < 67 and p.y > 40 then
            cue("하역장", "분홍색 정지선에서 브레이크!", "Space 급정거    R 하역장부터")
            if (p.y >= 43 and IsKeyDown("space")) or (p.y >= 46 and speed < 1) or p.y >= 48 then dive() end
        elseif p.x > 27 and p.y < 18 then
            cue("니지카", "거기서 오른쪽! 공연장은 노란 화살표 쪽이야.", "D 우회전    R 하역장부터")
        end
    elseif phase == "dive" then
        trackPassenger(dt)
        if elapsed > 2.2 then
            cue("료", "저거 굿즈야.", "에디터 Collision으로 바디 보기    |    Enter 진짜 히토리 찾기    R 급정거 재연")
            enterPhase("inspect")
        end
    elseif phase == "inspect" then
        trackPassenger(dt)
        if IsKeyPressed("enter") then
            SetGameValue("EncoreReveal", 1)
            aim(revealCamera, onboard(-4.8, 3.4, 3.9), onboard(-1.55, 0, 2.6))
            SetViewTargetWithBlend(revealCamera, 1, "Cubic")
            cue("니지카", "그럼 진짜 히토리는 어디에...")
            enterPhase("reveal")
        end
    elseif phase == "reveal" then
        aim(revealCamera, onboard(-4.8, 3.4, 3.9), onboard(-1.55, 0, 2.7))
        if elapsed > 6 then
            aim(shirtCamera, onboard(-5.35, -2.65, 2.65), onboard(-2.42, -0.62, 2.48))
            SetViewTargetWithBlend(shirtCamera, 0.8, "Cubic")
            cue("", "")
            enterPhase("shirt")
        elseif elapsed > 1.65 then
            cue("히토리", "저... 저도 하역해야 하나요?")
        end
    elseif phase == "shirt" then
        if elapsed >= 1 and GetGameValue("EncoreShirtDrop", 0) == 0 then
            SetGameValue("EncoreShirtDrop", 1)
        elseif elapsed >= 2.2 then
            PlaySound2D("Assets/Sounds/EncoreLastNote.wav", 0.65)
            cue("결속밴드", "공연을 시작합니다.")
            enterPhase("pickup")
        end
    elseif phase == "pickup" and elapsed >= 2 * beat then
        cue("", "")
        if hud then
            hud:Find("CaptionPlate").Visible = false
            hud:Find("Margin").Visible = false
        end
        SetGameValue("EncoreConcert", 1)
        SetViewTargetWithBlend(GetActorByName(concertShots[1]), 0)
        obj:PlayAudio()
        enterPhase("concert")
    elseif phase == "concert" then
        local barBeat = elapsed / beat
        local shot = barBeat >= 20 and 4 or barBeat >= 16 and 3 or barBeat >= 8 and 2 or 1
        if shot ~= concertShot then
            concertShot = shot
            SetViewTargetWithBlend(GetActorByName(concertShots[shot]), 0)
        end
        if elapsed < 12.8 then return end
        if hud then
            hud:Find("Blackout").Visible = true
            hint:SetText("R 급정거 재연    T 처음부터")
        end
        SetGameValue("EncoreConcert", 0)
        enterPhase("credits")
    end
end

function OnReload()
    enterPhase("travel")
    OpenLevel("BocchiEncore")
end

function EndPlay()
    if hud and hud:IsValid() then hud:RemoveFromViewport() end
end
