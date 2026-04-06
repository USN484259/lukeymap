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
		return info.events.EV_KEY and info.events.EV_LED and not info.events.EV_REL
	end,
	-- rules
	{{
		-- ANother WAy TO IMplement CApsLOck DElay FIx
		-- see also https://github.com/hexvalid/Linux-CapsLock-Delay-Fixer
		{},
		function (ev)
			if ev and ev.type == EV_KEY and ev.code == KEY.CAPSLOCK then
				if ev.value == 1 then
					return {
						ev, {type = ev.type, code = ev.code, value = 0}
					}
				else
					return {}
				end
			end
		end,
	}, {
		{},
		function(ev, key_state, rule, dev)
			if key_state.numpad_off == nil then
				-- initial call, get numlock state
				key_state.numpad_off = (not dev:led(LED_NUML))
			elseif ev and ev.type == EV_LED and ev.code == LED_NUML then
				key_state.numpad_off = (ev.value == 0)
			end
		end,
	}, {
		-- reuse numpad keys as shortcuts
		{"numpad_off", KEY.KP0}, {KEY.RIGHTMETA}
	}, {
		{"numpad_off", KEY.KPDOT}, {KEY.RIGHTALT}
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
	}, {
		{"numpad_off", KEY.KP7}, {KEY.LEFTCTRL, KEY.W}
	}, {
		{"numpad_off", KEY.KP8}, {KEY.LEFTCTRL, KEY.E}
	}, {
		{"numpad_off", KEY.KP9}, {KEY.LEFTCTRL, KEY.R}
	}, {
		{"numpad_off", KEY.KPMINUS}, {KEY.ESC},
	}, {
		{"numpad_off", KEY.KPPLUS}, {KEY.TAB},
	}}
}}
