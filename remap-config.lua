#!/usr/bin/env lua

local EV_KEY = device.type_num("EV_KEY")
local LED_NUML, EV_LED = device.code_num("LED_NUML")

return {{
	-- match
	{
		name = "Kensington Orbit Fusion Wireless Trackball"
	},
	-- rules
	{{
		-- make side key as right key on this trackball mouse
		{BTN.SIDE}, {BTN.RIGHT}
	}}
}, {
	-- match
	function(info)
		return info.events.EV_KEY
	end,
	-- rules
	{{
		-- ANother WAy TO IMplement CApsLOck DElay FIx
		-- see also https://github.com/hexvalid/Linux-CapsLock-Delay-Fixer
		{},
		function (ev, key_state)
			return ev.type == EV_KEY and ev.code == KEY.CAPSLOCK and ev.value == 1
		end,
		function (ev, arg, rule)
			if arg then
				return {
					ev, {type = ev.type, code = ev.code, value = 0}
				}
			else
				return {}
			end
		end
	}, {
		{},
		function(ev, key_state)
			if ev.type == EV_LED and ev.code == LED_NUML then
				key_state.numpad_off = (ev.value == 0)
			end
		end,
		function (ev, arg, rule) end	-- never called
	}, {
		-- reuse numpad keys as shortcuts
		{"numpad_off", KEY.KP0}, {KEY.LEFTMETA}
	}, {
		{"numpad_off", KEY.KPDOT}, {KEY.LEFTCTRL, KEY.B}
	}, {
		{"numpad_off", KEY.KP1}, {KEY.LEFTCTRL, KEY.X}
	}, {
		{"numpad_off", KEY.KP2}, {KEY.LEFTCTRL, KEY.C}
	}, {
		{"numpad_off", KEY.KP3}, {KEY.LEFTCTRL, KEY.V}
	}, {
		{"numpad_off", KEY.KP4}, {KEY.LEFTCTRL, KEY.A}
	}, {
		{"numpad_off", KEY.KP5}, {KEY.LEFTCTRL, KEY.S}
	}, {
		{"numpad_off", KEY.KP6}, {KEY.LEFTCTRL, KEY.D}
	}}
}}
