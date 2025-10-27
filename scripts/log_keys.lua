
local function main(names)

	local dev_map = {}

	local function handle_event(dev)
		local ev_list = dev:read()
		local name = dev_map[dev]
		for _, ev in ipairs(ev_list) do
			local typeid = device.type_name(ev.type) or ev.type
			local code = device.code_name(ev.type, ev.code) or ev.code
			local value = device.value_name(ev.type, ev.code, ev.value) or ev.value
			print(name, typeid, code, value)
		end
	end

	return function(op, devname)
		if names and not names[devname] then
			return false
		end
		if op == "add" then
			local dev = device.open(devname)
			dev:handler(handle_event)
			dev:monitor(true)
			dev_map[dev] = devname
			return true
		elseif op == "del" then
			for dev, name in pairs(dev_map) do
				if name == devname then
					dev_map[dev] = nil
					dev:close()
					return true
				end
			end
		end
	end
end

local module_name = ...

if module_name == sys.main then
	local names = {}
	local i = 2
	while true do
		local name = select(i, ...)
		if not name then break end
		i = i + 1
		names[name] = true
	end

	return main(i > 2 and names or nil)
end
