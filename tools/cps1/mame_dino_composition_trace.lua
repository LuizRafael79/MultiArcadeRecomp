-- Capture the live CPS-B composition state for Cadillacs and Dinosaurs.
-- Run with:
--   mame dino -rompath /home/luiz/git/mame/roms \
--     -autoboot_script tools/cps1/mame_dino_composition_trace.lua
-- The script inserts one coin, presses 1P start, then taps right to move the
-- 1P marker from Jake to Hannah. It saves reference screenshots at frames
-- 400/500 and logs changes to layer-control/video-control/priority state.

local LOG_PATH = "/tmp/dino-mame-composition.log"
local frame = 0
local previous = nil
local log = assert(io.open(LOG_PATH, "w"))
local coin_field, start_field, right_field

local function word(share, word_offset)
    return share:read_u16(word_offset * 2)
end

local function layers(control)
    return (control >> 6) & 3, (control >> 8) & 3,
        (control >> 10) & 3, (control >> 12) & 3
end

emu.register_frame_done(function()
    frame = frame + 1
    local ports = manager.machine.ioport.ports
    if not coin_field then
        coin_field = ports[":IN0"].fields["Coin 1"]
        start_field = ports[":IN0"].fields["1 Player Start"]
        right_field = ports[":IN1"].fields["P1 Right"]
    end
    if frame == 300 then coin_field:set_value(1) end
    if frame == 310 then coin_field:clear_value() end
    if frame == 360 then start_field:set_value(1) end
    if frame == 370 then start_field:clear_value() end
    if frame == 420 then right_field:set_value(1) end
    if frame == 422 then right_field:clear_value() end

    local memory = manager.machine.memory
    local a = memory.shares[":cps_a_regs"]
    local b = memory.shares[":cps_b_regs"]
    if not a or not b then return end

    -- CPS_B_21_QS2 in cps1_v.cpp: layer control $0a; priorities
    -- $0c/$0e/$00/$02; layer enables $16/$16/$16.
    local control = word(b, 0x05)
    local video = word(a, 0x11)
    local p0, p1 = word(b, 0x06), word(b, 0x07)
    local p2, p3 = word(b, 0x00), word(b, 0x01)
    local signature = string.format("%04x:%04x:%04x:%04x:%04x:%04x",
        control, video, p0, p1, p2, p3)
    if signature ~= previous then
        local l0, l1, l2, l3 = layers(control)
        log:write(string.format(
            "frame=%d control=%04x order=%d,%d,%d,%d video=%04x " ..
            "priority=%04x,%04x,%04x,%04x enables=%d,%d,%d\n",
            frame, control, l0, l1, l2, l3, video, p0, p1, p2, p3,
            (control & 0x16) ~= 0 and 1 or 0,
            (control & 0x16) ~= 0 and 1 or 0,
            (control & 0x16) ~= 0 and 1 or 0))
        log:flush()
        previous = signature
    end
    if frame == 400 or frame == 500 then
        local gfx = memory.shares[":gfxram"]
        if gfx then
            local object_base = ((word(a, 0) * 256) & 0x3ffff) & (~0x7ff)
            log:write(string.format("frame=%d object_base=%05x\n", frame, object_base))
            for entry = 0, 0x3fc, 4 do
                local x = word(gfx, object_base / 2 + entry)
                local y = word(gfx, object_base / 2 + entry + 1)
                local code = word(gfx, object_base / 2 + entry + 2)
                local attr = word(gfx, object_base / 2 + entry + 3)
                if (attr & 0xff00) == 0xff00 then
                    log:write(string.format("frame=%d object_end=%03x attr=%04x\n", frame, entry / 4, attr))
                    break
                end
                log:write(string.format(
                    "frame=%d object[%03x] x=%03x y=%03x code=%04x attr=%04x size=%dx%d\n",
                    frame, entry / 4, x & 0x1ff, y & 0x1ff, code, attr,
                    ((attr >> 8) & 0x0f) + 1, ((attr >> 12) & 0x0f) + 1))
            end
            log:flush()
        end
    end
    if frame == 400 or frame == 500 then
        local screen = manager.machine.screens[":screen"]
        if screen then
            local path = string.format("/tmp/dino-mame-select-%04d.png", frame)
            screen:snapshot(path)
            print("Dino CPS-1 reference screenshot: " .. path)
        end
    end
end, "dino_composition_trace")

print("Dino CPS-1 composition trace: " .. LOG_PATH)
