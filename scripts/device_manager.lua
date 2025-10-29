
return function(match_func, new_func, del_func)

	local device_map = {}

	return function(op, devname)
		if op == "add" then
			if not device_map[devname] then
				local dev, udev, udev_name

				local stat, object = pcall(function()
					dev = device.open(devname)
					local info = dev:info()
					local arg = match_func(info, devname)
					if not arg then
						return
					end

					udev = device.create(dev)
					udev_name = udev:name()
					local object = {
						info = info,
						src = dev,
						src_name = devname,
						sink = udev,
						sink_name = udev_name,
					}

					if new_func then
						new_func(object, arg)
					end
					return object
				end)
				if stat and object then
					device_map[devname] = object
					device_map[udev_name] = object
					return object
				end
				if not stat then
					print(devname, object)
				end
				if udev then udev:close() end
				if dev then dev:close() end
			end
		elseif op == "del" then
			object = device_map[devname]
			if type(object) == "table" then
				device_map[object.src_name] = nil
				device_map[object.sink_name] = nil

				if del_func then
					pcall(del_func, object)
				end
				object.sink:close()
				object.src:close()

				return true
			end
		end
	end

end
