-- The two clips and the NvCloth shoulder pins share this carrier.
-- When it slips off the rack, the carton rim catches it below.
local carrier, mount
local height, velocity = 0, 0
local travel = 1.27

function BeginPlay()
    carrier = GetComponentByName("ShirtCarrier")
    mount = carrier.Location
end

function Tick(dt)
    if height >= travel or GetGameValue("EncoreShirtDrop", 0) == 0 then return end
    height = math.min(travel, height + velocity * dt + 4.905 * dt * dt)
    velocity = velocity + 9.81 * dt
    carrier.Location = FVector(mount.x, mount.y, mount.z - height)
end
