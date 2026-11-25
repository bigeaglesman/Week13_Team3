local players, cameras, crashCymbal
local elapsed = 0
local origin = FVector(300, 320, -35)
local beatLength = 60 / 132

local function advance(beat, startBeat, beats)
    return math.max(0, math.min(1, (beat - startBeat) / beats))
end

local function player(name)
    local pose = GetComponentByName(name .. "Pose")
    return {
        pose = pose, position = pose.Location, yaw = pose.Rotation.yaw,
        hand = GetComponentByName(name .. "Pick")
    }
end

-- The rhythm recording voices E5, B5, C#5 and A5 from the low A string.
-- Open A5 uses the second fret on the next two strings. The final E rings as a major chord.
local rhythmChanges = {{0,7,1}, {4,2,1}, {8,4,1}, {12,2,2}, {20,2,1}, {24,7,1}}
local bassNotes = {
    40,40,47,47,52,47,44,42, 35,35,42,46,47,42,39,35,
    37,37,44,44,49,47,44,40, 33,33,40,45,49,47,45,40,
    33,40,45,47,49,47,45,44, 35,35,42,46,47,42,47,
}
-- Standard four-string tuning E1/A1/D2/G2, numbered from the lowest string.
local bassFingering = {
    [33]={5,1}, [35]={7,1}, [37]={9,1}, [39]={6,2}, [40]={7,2},
    [42]={4,3}, [44]={6,3}, [45]={7,3}, [46]={8,3}, [47]={9,3},
    [49]={6,4}, [52]={9,4},
}
local bassChanges = {}
for i,note in ipairs(bassNotes) do
    if note ~= bassNotes[i-1] then
        local fingering = bassFingering[note]
        bassChanges[#bassChanges+1] = {(i-1)*.5, fingering[1], fingering[2]}
    end
end
bassChanges[#bassChanges+1] = {24, 4, 1, true} -- Release for the final open E.

local function bindFretArm(name, shoulder)
    return {
        upper=GetComponentByName(name.."FretUpper"), fore=GetComponentByName(name.."FretFore"),
        hand=GetComponentByName(name.."FretHand"), keys={}, cursor=1,
        shoulder=shoulder,
    }
end

local function fretArm(name, score, bass)
    local bridge, scale = bass and -.28 or -.165, bass and 1.265 or .919
    local strings, spread = bass and 4 or 6, bass and .0204 or .024
    local arm = bindFretArm(name, FVector(.028,.262,1.783))
    for i,event in ipairs(score) do
        -- Each successive fret shortens the vibrating length by a semitone.
        local ratio = 2^(-(event[2]-.22)/12)
        local a = bridge + scale*ratio - (bass and .083 or .078)
        local width = spread*ratio + (bass and .040 or .037)*(1-ratio)
        local b = width*(1-2*(event[3]-1)/(strings-1)) - (bass and .150 or .135)
        local h = event[4] and .038 or (bass and .005 or .001)
        if event[4] then b=b-.024 end
        -- Guitar authoring coordinates to performer-local engine coordinates.
        local wrist = FVector(-(.122+h-.23*b+.08*a), -.095+.942755*a-.333487*b,
            1.285+.333487*a+.942755*b)
        arm.keys[i]={beat=event[1], wrist=wrist}
    end
    return arm
end

-- Contacts sampled just behind the fret wires on Hitori's imported Les Paul.
local bString = {
    [5]=FVector(-.2595,.3921,1.4572), [7]=FVector(-.2523,.3241,1.4315),
    [9]=FVector(-.2459,.2641,1.4088), [10]=FVector(-.2429,.2356,1.3981),
}
local highE = {
    [7]=FVector(-.2522,.3279,1.4212), [8]=FVector(-.2489,.2972,1.4094),
    [9]=FVector(-.2458,.2681,1.3982), [11]=FVector(-.2401,.2136,1.3774),
    [12]=FVector(-.2375,.1893,1.3680), [13]=FVector(-.2351,.1660,1.3591),
    [14]=FVector(-.2328,.1443,1.3508),
}
local indexPad = FVector(-.04604,.03692,.16133)
local ringPad = FVector(-.02109,-.01823,.14369)
local leadFingering = {
    [64]={bString,5,indexPad}, [66]={bString,7,ringPad}, [68]={bString,9,ringPad},
    [69]={bString,10,ringPad}, [71]={highE,7,indexPad}, [73]={highE,9,ringPad},
    [75]={highE,11,indexPad}, [76]={highE,12,ringPad}, [78]={highE,14,ringPad},
}
-- Beat within the bar, MIDI pitch, sounding duration in beats, optional semitone bend.
local leadBars = {
    {{0,64,.40},{.5,71,.39},{1,73,.65,true},{1.75,71,.22},{2,68,.72},{2.75,66,.22},{3,64,.40},{3.5,68,.48}},
    {{0,66,.85},{1,71,.46},{1.5,73,.33},{2,75,.74},{2.75,73,.21},{3,71,.89}},
    {{0,64,.42},{.5,71,.39},{1,73,.65,true},{1.75,71,.21},{2,68,.40},{2.5,71,.37},{3,73,.88}},
    {{0,76,.90,true},{1,73,.42},{1.5,71,.45},{2,69,.72},{2.75,68,.22},{3,66,.44},{3.5,64,.40}},
    {{0,69,.68},{.75,71,.22},{1,73,.40},{1.5,76,.93,true},{2.5,73,.41},{3,76,.42},{3.5,78,.44}},
    {{0,78,.75,true},{.75,76,.20},{1,75,.40},{1.5,73,.39},{2,71,.42},{2.5,75,.24},{3,78,.33}},
    {{0,76,3.6}},
}

local function leadArm()
    local arm = bindFretArm("Hitori", FVector(.0293,.2828,1.7061))
    for bar, notes in ipairs(leadBars) do
        for _, note in ipairs(notes) do
            local string, wire, finger = table.unpack(leadFingering[note[2]])
            local bend = note[4]
            local contact = string[bend and wire-1 or wire]
            arm.keys[#arm.keys+1] = {
                beat=(bar-1)*4+note[1], wrist=contact-finger, duration=note[3], bend=bend,
            }
        end
    end
    return arm
end

local acrossStrings = FVector(0,-.3485,.9373)
local function leadExpression(key, beat)
    local age = (beat-key.beat)*beatLength
    local remaining = (key.beat+key.duration-beat)*beatLength
    local offset = key.bend and .017*(1-math.exp(-age/.037)) or 0
    if key.duration > .61 and remaining > 0 then
        local envelope = advance(age,.07,.22)*math.min(1,remaining/.065)
        offset = offset + .0055*envelope*math.sin(2*math.pi*5.6*age)
    end
    return acrossStrings*offset
end

local function fret(arm, beat)
    local keys = arm.keys
    if beat < keys[arm.cursor].beat then arm.cursor=1 end
    while keys[arm.cursor+1] and beat >= keys[arm.cursor+1].beat do arm.cursor=arm.cursor+1 end
    local here, upcoming = keys[arm.cursor], keys[arm.cursor+1]
    local wrist = here.wrist
    if here.duration then wrist=wrist+leadExpression(here,beat) end
    if upcoming then
        local span=math.min(.38,(upcoming.beat-here.beat)*.65)
        if here.duration then
            local gap=upcoming.beat-here.beat-here.duration
            span=math.min(span,math.max(.11,math.min(.24,gap+.075)))
        end
        local t=advance(beat,upcoming.beat-span,span)
        wrist=wrist+(upcoming.wrist-wrist)*(t*t*(3-2*t))
    end
    -- Two rigid links meet at a low, outward elbow as the wrist follows the strings.
    local upperLength, foreLength = .300,.305
    local delta=wrist-arm.shoulder
    local distance=delta:Length()
    local axis=delta/distance
    local down=FVector(.03,.19,-1)
    local bend=(down-axis*down:Dot(axis)):Normalize()
    local along=(upperLength^2-foreLength^2+distance^2)/(2*distance)
    local elbow=arm.shoulder+axis*along+bend*math.sqrt(math.max(0,upperLength^2-along^2))
    local upper,fore=FRotator(0,0,0),FRotator(0,0,0)
    upper:LookDirection(elbow-arm.shoulder)
    fore:LookDirection(wrist-elbow)
    arm.upper.Location,arm.upper.Rotation=arm.shoulder,upper
    arm.fore.Location,arm.fore.Rotation=elbow,fore
    arm.hand.Location=wrist
end

-- Elbow pitch/yaw place the capped end of each stick on its own playing surface.
-- The two arms reach the same snare head from opposite sides.
local hat, leftSnare, rackTom = {8.34, 23.3}, {-3.63, -43.67}, {-12.71, -9.63}
local rightSnare, floorTom, crash = {-3.63, 40.70}, {-14.11, -45.43}, {24.33, -27.21}
local leftHits, rightHits = {}, {}
local function hit(hand, beat, surface, rebound)
    hand[#hand + 1] = {beat = beat, pitch = surface[1], yaw = surface[2], rebound = rebound}
end

-- The kit part in EncoreLive.wav is six bars at 132 BPM, followed by the ring-out.
for bar = 0, 5 do
    local start = 4 * bar
    for eighth = 0, (bar == 5 and 4 or 7) do
        local open = eighth == 7 and (bar == 1 or bar == 3 or bar == 4)
        hit(leftHits, start + eighth / 2, hat, open and 28 or (eighth % 2 == 0 and 22 or 15))
    end
    if bar % 2 == 0 then hit(rightHits, start, crash, 31) end
    hit(rightHits, start + 1, rightSnare, 34)
    if bar == 1 or bar == 3 then hit(rightHits, start + 2.75, rightSnare, 10) end
    if bar < 5 then hit(rightHits, start + 3, rightSnare, 34) end
end
hit(leftHits, 22.5, leftSnare, 19)
hit(rightHits, 22.75, rightSnare, 18)
hit(leftHits, 23, rackTom, 26)
hit(rightHits, 23.25, floorTom, 18)
hit(rightHits, 23.5, floorTom, 25)
hit(leftHits, 24, leftSnare, 28)
hit(rightHits, 24, crash, 37)

local function stickRotation(hits, beat)
    local i = #hits
    while i > 1 and beat < hits[i].beat do i = i - 1 end
    local last, nextHit = hits[i], hits[i + 1]
    -- The capture samples every third 60 Hz tick. Keep contact on the closest 20 fps frame.
    local halfFrame = .025 / beatLength
    if nextHit and nextHit.beat - beat <= halfFrame then
        return FRotator(nextHit.pitch, nextHit.yaw, 0)
    end
    if beat - last.beat <= halfFrame then return FRotator(last.pitch, last.yaw, 0) end
    local since = beat - last.beat - halfFrame
    if not nextHit then
        local rebound = math.sin(math.pi * math.min(1, since / .85))
        local rest = math.min(1, since / .85)
        return FRotator(last.pitch + 10 * rest + last.rebound * rebound, last.yaw, 0)
    end
    local span = nextHit.beat - last.beat - 2 * halfFrame
    local t = math.min(1, since / span)
    local blend = t*t*(3 - 2*t)
    local up = math.min(1, since / math.min(.32, span * .54))
    local down = math.min(1, (1 - t) * span / math.min(.30, span * .46))
    local lift = math.sin(math.pi * .5 * math.min(up, down))
    local height = last.rebound + (nextHit.rebound - last.rebound) * blend
    return FRotator(last.pitch + (nextHit.pitch - last.pitch) * blend + height * lift,
        last.yaw + (nextHit.yaw - last.yaw) * blend, 0)
end

local function bindStage()
    players = { player("Hitori"), player("Ikuyo"), player("Ryo"), player("Nijika") }
    players[1].fretting=leadArm()
    players[2].fretting=fretArm("Ikuyo",rhythmChanges,false)
    players[3].fretting=fretArm("Ryo",bassChanges,true)
    players[4].otherHand = GetComponentByName("NijikaSnare")
    crashCymbal = GetComponentByName("CrashCymbal")
    cameras = {
        GetActorByName("EncoreLiveWide"), GetActorByName("EncoreLiveGuitars"),
        GetActorByName("EncoreLiveRhythm"), GetActorByName("EncoreLiveHero")
    }
end

local function frame(camera, eye, target)
    local rotation = FRotator(0, 0, 0)
    rotation:LookDirection(target - eye)
    SetCameraRelativeTransform(camera, origin + eye, rotation)
end

function Tick(dt)
    if GetGameValue("EncoreConcert", 0) ~= 1 then
        elapsed = 0
        return
    end
    if not players then bindStage() end
    local beat = elapsed / beatLength
    -- Let the last stroke follow through and come to rest while the chord rings.
    local followThrough = advance(beat, 24, .75)
    local playingBeat = math.min(beat, 24) + .28 * (1 - (1 - followThrough)^2)
    local angle = playingBeat * 2 * math.pi
    for i = 1, 3 do
        local p = players[i]
        local phase = angle - (i - 1) * .34
        local sway = math.sin(phase / 2)
        local bounce = (1 - math.cos(phase)) * .5
        p.pose.Location = p.position + FVector(0, sway * .027, bounce * (i == 2 and .052 or .028))
        p.pose.Rotation = FRotator(1.8 * math.sin(phase), p.yaw + 2.1 * sway, (i == 1 and 4.4 or 2.4) * sway)
        local strum = math.sin(phase * (i == 3 and 1 or 2))
        p.hand.Rotation = FRotator(2 * strum, 0, (i == 3 and 8 or 13) * strum)
    end

    fret(players[1].fretting, beat)
    fret(players[2].fretting, beat)
    fret(players[3].fretting, beat)
    players[4].hand.Rotation = stickRotation(leftHits, beat)
    players[4].otherHand.Rotation = stickRotation(rightHits, beat)
    local lastCrash = math.min(24, 8 * math.floor(beat / 8))
    local age = math.max(0, (beat - lastCrash) * beatLength - .025)
    local tilt = 0
    if age < 1.15 then
        tilt = (lastCrash == 24 and 13 or 9) * math.sin(age * 36) * math.exp(-4.2 * age)
            * math.min(1, (1.15 - age) / .15)
    end
    crashCymbal.Rotation = FRotator(-.2 * tilt, 0, tilt)

    -- Each camera starts on its authored mark. Small moves stay inside the club even across a hard cut.
    local a, b, c, d = advance(beat, 0, 8), advance(beat, 8, 8), advance(beat, 16, 4), advance(beat, 20, 8)
    frame(cameras[1], FVector(-3.90 + .18*a, -.09 + .20*a, 1.42), FVector(.36, .01, 1.63))
    frame(cameras[2], FVector(-2.62 + .15*b, -2.31 + .26*b, 1.56), FVector(-.13, -1.27 + .11*b, 1.49))
    frame(cameras[3], FVector(-1.87 + .06*c, .68 + .10*c, 1.88), FVector(.87, 1.46, 1.59))
    frame(cameras[4], FVector(-3.56 - .38*d, .43 - .36*d, .94 + .20*d), FVector(.38, .08, 1.57))
    elapsed = elapsed + dt
end
