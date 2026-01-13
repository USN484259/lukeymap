#!/usr/bin/env lua

local EV_KEY = device.type_num("EV_KEY")

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
		-- reuse numpad keys as shortcuts
		{KEY.KP0}, {KEY.LEFTMETA}
	}, {
		{KEY.KPDOT}, {KEY.LEFTCTRL, KEY.B}
	}, {
		{KEY.KP1}, {KEY.LEFTCTRL, KEY.X}
	}, {
		{KEY.KP2}, {KEY.LEFTCTRL, KEY.C}
	}, {
		{KEY.KP3}, {KEY.LEFTCTRL, KEY.V}
	}, {
		{KEY.KP4}, {KEY.LEFTCTRL, KEY.A}
	}, {
		{KEY.KP5}, {KEY.LEFTCTRL, KEY.S}
	}, {
		{KEY.KP6}, {KEY.LEFTCTRL, KEY.D}
	}}
}}
