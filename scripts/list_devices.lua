
local module_name, devname = ...

local print_table = require("print_table")

if module_name == sys.main then
	local timer = sys.timer(function() sys.exit() end)
	timer:set(0, 1)
	return function (op, path)
		if devname and path ~= devname then return end
		if op ~= "add" then return end
		local stat, dev = pcall(device.open, path)
		if not stat then return end
		local info = dev:info()
		if devname then
			print(path .. ':')
			print_table(info, 1)
			print()
		else
			print(path, info.name)
		end

		dev:close()
	end
end