local stage = "opening"
local captions
local line
local landingTime
local endingLine

local function say(text)
    if line then line:SetText(text) end
end

function BeginPlay()
    captions = CreateWidget("Assets/UI/IntroDialogue.widget")
    if captions then
        captions:AddToViewport(1)
        line = captions:Find("DialogueText")
    end
    say("니지카   오늘 발표는 히토리 짱이 해 줄 거야.     [Space]")
end

function Tick(dt)
    if stage == "opening" then
        if SetViewTargetWithBlend(GetActorByName("BandCamera"), 0) then
            stage = "band"
        end
    elseif stage == "band" and IsKeyPressed("space") then
        SetViewTargetWithBlend(GetActorByName("HitoriCamera"), 1.15, "Cubic")
        say("히토리   저, 저는 뒤에서도 잘 보이는데요.")
        stage = "focus"
    elseif stage == "focus" and not IsCameraBlending() then
        say("히토리   저한테 왜 초점을...     [1] 경첩 유지     [2] 경첩 해제")
        stage = "choice"
    elseif stage == "choice" then
        local free = IsKeyPressed("2")
        if free or IsKeyPressed("1") then
            if free then GetActorByName("Hinge"):Destroy() end
            GetActorByName("ReleaseShelf"):Destroy()
            if free then
                endingLine = "니지카   그럼 질문은 망고 상자가 받겠습니다.     [R] 다시"
            else
                endingLine = "히토리   상자마저 저한테 올 자유가 없어요...     [R] 다시"
            end
            say("")
            landingTime = 1.35
            stage = "dropping"
        end
    elseif stage == "dropping" then
        landingTime = landingTime - dt
        if landingTime <= 0 then
            say(endingLine)
            stage = "ending"
        end
    elseif stage == "ending" and IsKeyPressed("R") then
        OpenLevel("BocchiSoundcheck")
    end
end

function EndPlay()
    if captions and captions:IsValid() then captions:RemoveFromViewport() end
end
