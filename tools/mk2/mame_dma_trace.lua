-- Capture T-Unit DMA register state at every triggered command.
-- Run MAME with: mame mk2 -autoboot_script tools/mk2/mame_dma_trace.lua
if rawget(_G, "__mk2_dma_trace_active") then return end
rawset(_G, "__mk2_dma_trace_active", true)
local base = 0x01a80000
local regs = {}
for i = 1, 32 do regs[i] = 0 end
local maps = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 16, 17, 14, 15 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }
}
local log_path = os.getenv("MULTIARCADERECOMP_MK2_DMA_LOG") or
    "build/mk2/work/mk2-mame-dma.log"
local out = assert(io.open(log_path, "w"))
out:write("# seq cmd config source x y width height palette color xstep ystep r12 r13 r16 r17 cpu_pc emulated_seconds A0 A1 A2 A3 A4 A5 A6 A7 A8 A9 A10 A11 A12 A13 A14 B0 B1 B2 B3 B4 B5 B6 B7 B8 B9 B10 B11 B12 B13 B14\n")
local sequence = 0
local cpu = manager.machine.devices[":maincpu"]
assert(cpu, "MK2 maincpu not found")
local pc_state = cpu.state and cpu.state["PC"]
local register_states = {}
for i = 0, 14 do
    register_states[#register_states + 1] = cpu.state and cpu.state["A" .. i]
end
for i = 0, 14 do
    register_states[#register_states + 1] = cpu.state and cpu.state["B" .. i]
end
local space = cpu.spaces["program"]
assert(space, "MK2 maincpu program space not found")
local tap = space:install_write_tap(base, base + 0xff, "mk2_dma_trace",
    function(offset, data, mem_mask)
        local word_offset = math.floor((offset - base) / 16)
        if word_offset < 0 or word_offset >= 16 then return end
        local config = regs[16]
        local bank = (config & 0x20) ~= 0 and 2 or 1
        local target = maps[bank][word_offset + 1] + 1
        local old = regs[target]
        regs[target] = ((old & (~mem_mask & 0xffff)) | (data & mem_mask)) & 0xffff
        if target == 2 and (regs[target] & 0x8000) ~= 0 then
            sequence = sequence + 1
            local line = string.format(
                "%d 0x%04x 0x%04x 0x%08x %d %d %d %d 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%08x %.6f",
                sequence, regs[2], regs[16], (regs[4] << 16) | regs[3],
                regs[5] & 0x3ff, regs[6] & 0x1ff,
                regs[7] & 0x3ff, regs[8] & 0x3ff,
                regs[9], regs[10], regs[11], regs[12], regs[13], regs[14],
                regs[17], regs[18], pc_state and pc_state.value or 0xffffffff,
                manager.machine.time:as_double())
            for _, state in ipairs(register_states) do
                line = line .. string.format(" 0x%08x",
                    state and state.value or 0xffffffff)
            end
            out:write(line .. "\n")
            out:flush()
        end
    end)
emu.add_machine_stop_notifier(function()
    out:flush()
    out:close()
end)
print("MK2 DMA trace active; writing " .. log_path)

local last_tap_refresh = -1
emu.register_periodic(function()
    local second = math.floor(manager.machine.time:as_double())
    if second > last_tap_refresh then
        tap:reinstall()
        last_tap_refresh = second
    end
end)

if os.getenv("MULTIARCADERECOMP_MK2_AUTOPLAY") == "1" then
    local ports = manager.machine.ioport.ports
    local function field(tag, mask)
        local port = assert(ports[tag], "missing MK2 input port " .. tag)
        return assert(port:field(mask), "missing MK2 input field " .. tag .. ":" .. mask)
    end
    local controls = {
        coin1 = field(":IN1", 0x0001), p1_start = field(":IN1", 0x0004),
        p1_accept = field(":IN0", 0x0010),
        p1_skip = field(":IN0", 0x0040),
        p1_left = field(":IN0", 0x0004), p1_right = field(":IN0", 0x0008),
    }
    local input_log
    local input_log_path = os.getenv("MULTIARCADERECOMP_MK2_INPUT_LOG")
    if input_log_path then
        input_log = assert(io.open(input_log_path, "w"))
        input_log:write("# emulated_seconds IN0 IN1 IN2 DSW\n")
        emu.add_machine_stop_notifier(function()
            input_log:flush()
            input_log:close()
        end)
    end
    local function pulse(t, starts)
        for _, start in ipairs(starts) do
            if t >= start and t < start + 0.4 then return true end
        end
        return false
    end
    local function update(field_obj, down)
        if down then field_obj:set_value(1) else field_obj:clear_value() end
    end
    local function movement_phase(t)
        if t < 32 then return false, false end
        local phase = math.floor((t - 32) / 8) % 2
        return phase == 0, phase ~= 0
    end
    local last_input_log_time = -1.0
    emu.register_periodic(function()
        local t = manager.machine.time:as_double()
        update(controls.p1_skip, pulse(t, {4}))
        update(controls.coin1, pulse(t, {10, 11}))
        update(controls.p1_start, pulse(t, {13}))
        update(controls.p1_left, pulse(t, {18}))
        update(controls.p1_accept, pulse(t, {20, 22, 24, 26, 28}))
        local p1_left, p1_right = movement_phase(t)
        update(controls.p1_left, p1_left or pulse(t, {18}))
        update(controls.p1_right, p1_right)
        if input_log and t >= last_input_log_time + 0.1 then
            input_log:write(string.format("%.2f %04x %04x %04x %04x\n", t,
                ports[":IN0"]:read(), ports[":IN1"]:read(), ports[":IN2"]:read(),
                ports[":DSW"]:read()))
            last_input_log_time = t
        end
    end)
    print("MK2 DMA trace autoplay active (continue, two credits, start, select, then walk)")
end
