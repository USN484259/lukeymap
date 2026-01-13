
local print_table = require("print_table")

local function main(names)
	local timer = sys.timer(function() sys.exit() end)
	timer:set(0, 1)
	return function (op, devname)
		if names and not names[devname] then return end
		if op ~= "add" then return end
		local stat, dev = pcall(device.open, devname)
		if not stat then return end
		local info = dev:info()
		if names then
			print(devname .. ':')
			print_table(info, 1)
			print()
		else
			print(devname, info.name)
		end

		dev:close()
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
