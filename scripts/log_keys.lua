local module_name, args = ...

local function main(args)

	local device_map = {}

	local function handle_event(dev)
		local ev_list = dev:read()
		for _, ev in ipairs(ev_list) do
			local typeid = device.type_name(ev.type) or ev.type
			local code = device.code_name(ev.type, ev.code) or ev.code
			local value = device.value_name(ev.type, ev.code, ev.value) or ev.value
			print(typeid, code, value)
		end
	end

	return function(op, devname)
		if op == "add" then
			local dev = device.open(devname)
			dev:handler(handle_event)
			dev:monitor(true)
			device_map[devname] = dev
		elseif op == "del" then
			local dev = device_map[devname]
			if dev then
				device_map[devname] = nil
				dev:close()
			end
		end
	end
end


if module_name == sys.main then
	return main(args)
end
