local module_name, config_file = ...

local print_table = require("print_table")
local device_manager = require("device_manager")

local EV_KEY = device.type_num("EV_KEY")

local function table_join(a, b)
	return table.move(b, 1, #b, #a + 1, a)
end

local function table_reverse(t)
	local i = 1
	local j = #t
	while i < j do
		t[i], t[j] = t[j], t[i]
		i = i + 1
		j = j - 1
	end
	return t
end

local KeyParser = {
	__index = function(self, key)
		local code, _ = device.code_num(self.prefix .. key)
		if not code then error("unknown key " .. key) end
		rawset(self, key, code)
		return code
	end
}

local function load_config(config_file)
	return require(config_file)
end


local function press_keys(keys, press, skip)
	local result = {}
	for _, k in ipairs(keys) do
		if skip and skip == k then
		elseif type(k) == "number" then
			table.insert(result, {
				type = EV_KEY,
				code = k,
				value = press and 1 or 0,
			})
		end
	end
	if not press then
		table_reverse(result)
	end
	return result
end


local function default_handler(ev, target, rule)
	local key_down = (ev.value > 0)

	if key_down and rule.active then return end
	if not (key_down or rule.active) then return end

	local mod, handler = table.unpack(rule)
	local result = {}

	if not key_down then
		table_join(result, press_keys(target, false))
		table_join(result, press_keys(mod, true, ev.code))
		rule.active = false
	else
		table_join(result, press_keys(mod, false, ev.code))
		table_join(result, press_keys(target, true))
		rule.active = true
	end
	return result
end

local function remap_keys(dev, rules, key_state, ev)
	local key_down = (ev.value > 0)
	key_state[ev.code] = key_down

	for i, rule in ipairs(rules) do
		local mod, handler = table.unpack(rule)
		local is_func = (type(handler) == "function")
		local arg = nil

		if key_down then
			if not is_func then
				if mod[#mod] ~= ev.code then goto _continue end
			end

			for i, k in ipairs(mod) do
				if i ~= #mod and not key_state[k] then goto _continue end
			end
			arg = ev
		else
			for _, k in ipairs(mod) do
				if k == ev.code then
					arg = false
					break
				end
			end
			if arg == nil then goto _continue end
		end

		if is_func then
			local result = handler(arg, key_state, rule, dev)
			if result then return result end
		else
			return default_handler(ev, handler, rule)
		end

	::_continue::
	end
end

local function remap_custom(dev, rules, key_state, ev)
	for i, rule in ipairs(rules) do
		local mod, handler = table.unpack(rule)
		if type(handler) ~= "function" then goto _continue end

		for _, k in ipairs(mod) do
			if not key_state[k] then goto _continue end
		end

		local result = handler(ev, key_state, rule, dev)
		if result then return result end

	::_continue::
	end
end

local function remap(dev, rules, key_state, ev_list)
	local new_list = {}
	for _, ev in ipairs(ev_list) do
		local result = nil
		if ev.type == EV_KEY then
			result = remap_keys(dev, rules, key_state, ev)
		else
			result = remap_custom(dev, rules, key_state, ev)
		end
		if result then
			new_list = table_join(new_list, result)
		else
			table.insert(new_list, ev)
		end
	end
	return new_list
end


local device_map = {}

local function handle_event(dev)
	local rec = device_map[dev]
	if not rec then return end
	local ev_list = dev:read()
	ev_list = remap(dev, rec.rules, rec.key_state, ev_list)
	rec.sink:write(ev_list)
end

local function match_dev(config, info)
	for _, entry in ipairs(config) do
		local match, rules = table.unpack(entry)
		if type(match) ~= "function" then
			for k, v in pairs(match) do
				if k == "name" then
					if not string.find(info[k], v) then goto _continue end
				elseif info[k] ~= v then
					goto _continue
				end
			end
		elseif not match(info) then
			goto _continue
		end
		if true then
			return table.pack(table.unpack(rules))
		end

	::_continue::
	end
end

local function main(config_file)
	local config = load_config(config_file)

	return device_manager(
		-- match function
		function (info) return match_dev(config, info) end,
		-- new function
		function(rec, rules)
			local dev = rec.src
			print("new mapping", rec.src_name, "=>", rec.sink_name)

			dev:handler(handle_event)
			dev:grab(true)
			dev:monitor(true)
			rec.rules = rules
			rec.key_state = {}
			device_map[dev] = rec
		end,
		-- del function
		function(rec)
			print("del mapping", rec.src_name, "=>", rec.sink_name)
			device_map[rec.src] = nil
		end
	)
end


if module_name == sys.main then
	-- global objects
	KEY = setmetatable({prefix = "KEY_"}, KeyParser)
	BTN = setmetatable({prefix = "BTN_"}, KeyParser)

	return main(config_file)
end
