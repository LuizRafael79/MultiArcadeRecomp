-- MAME-side CPS-1 video trace for comparison with MameRecomp.
-- Run with: mame sf2ce -autoboot_script tools/cps1/mame_video_trace.lua
-- Change TARGET_FRAME to capture a different frame.

local TARGET_FRAME = 3240
local frame = 0

local function word(share, byte_offset)
    return share:read_u16(byte_offset)
end

local function dump_words(file, label, share, first, count, per_line)
    file:write(label .. "\n")
    for i = 0, count - 1 do
        if i % per_line == 0 then file:write(string.format("%04x:", first + i)) end
        file:write(string.format(" %04x", word(share, (first + i) * 2)))
        if i % per_line == per_line - 1 then file:write("\n") end
    end
    if count % per_line ~= 0 then file:write("\n") end
end

emu.register_frame_done(function()
    frame = frame + 1
    if frame ~= TARGET_FRAME - 1 and frame ~= TARGET_FRAME then return end

    local memory = manager.machine.memory
    local a = memory.shares[":cps_a_regs"]
    local b = memory.shares[":cps_b_regs"]
    local gfx = memory.shares[":gfxram"]
    if not a or not b or not gfx then
        print("CPS1 trace: required shares not found")
        return
    end

    local path = "/tmp/cps1_video_trace_frame_" .. frame .. ".txt"
    local file = assert(io.open(path, "w"))
    file:write("frame=" .. frame .. "\n")

    local order = {}
    local control = word(b, 0x13 * 2)
    for i, shift in ipairs({ 6, 8, 10, 12 }) do
        order[i] = (control >> shift) & 3
    end
    file:write(string.format("layer_control=%04x order=%d,%d,%d,%d\n",
        control, order[1], order[2], order[3], order[4]))
    for i = 0, 0x11 do
        file:write(string.format("A[%02x]=%04x%s", i, word(a, i * 2), i % 6 == 5 and "\n" or " "))
    end
    file:write("\n")
    for i = 0, 0x1f do
        file:write(string.format("B[%02x]=%04x%s", i, word(b, i * 2), i % 6 == 5 and "\n" or " "))
    end
    file:write("\n")

    -- CPS-B-21 priority registers map tile priority group to pens that cover
    -- sprites. MAME forms the foreground mask by inverting each register.
    file:write("priority groups (group: register, value, MAME foreground mask)\n")
    for group = 0, 3 do
        local value = word(b, (0x14 + group) * 2)
        file:write(string.format("%d: B[%02x]=%04x foreground=%04x\n",
            group, 0x14 + group, value, (~value) & 0xffff))
    end

    -- Object table starts at CPS-A OBJ_BASE. This dumps all entries to the
    -- hardware's 0xff00 end marker so sprite positions/overlap can be checked.
    local obj_base = (word(a, 0) * 256) & 0x3ffff
    file:write(string.format("obj_base=%05x\n", obj_base))
    for i = 0, 0x3fc, 4 do
        local y = word(gfx, obj_base + (i + 1) * 2)
        local attr = word(gfx, obj_base + (i + 3) * 2)
        if (attr & 0xff00) == 0xff00 then
            file:write(string.format("sprite_end=%03x y=%04x attr=%04x\n", i, y, attr))
            break
        end
        local x = word(gfx, obj_base + i * 2)
        local code = word(gfx, obj_base + (i + 2) * 2)
        file:write(string.format("sprite[%03x] x=%03x y=%03x code=%04x attr=%04x size=%dx%d flip=%d%d\n",
            i / 4, x & 0x1ff, y & 0x1ff, code, attr,
            ((attr >> 8) & 15) + 1, ((attr >> 12) & 15) + 1,
            (attr >> 5) & 1, (attr >> 6) & 1))
    end

    -- Dump the RAM words used as tile code/attribute pairs. The runtime can
    -- compare these directly against its GFX-RAM snapshot.
    for layer = 1, 3 do
        local base = ((word(a, layer) * 256) & 0x3ffff) & (~0x3fff)
        dump_words(file, string.format("scroll%d base=%05x", layer, base), gfx,
            base / 2, 0x4000 / 2, 8)
    end

    file:close()
    print("CPS1 trace written: " .. path)
    local screen = manager.machine.screens[":screen"]
    if screen then
        screen:snapshot("/tmp/mame_cps1_trace_frame_" .. frame .. ".png")
        print("CPS1 frame snapshot written")
    end
    if frame == TARGET_FRAME then manager.machine:exit() end
end, "frame")
