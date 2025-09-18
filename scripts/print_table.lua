
local function print_table(table, indent)
	indent = indent or 0
	for k, v in pairs(table) do
		if type(v) == "table" then
			print(string.rep("\t", indent) .. tostring(k) .. ":")
			print_table(v, indent + 1)
		else
			print(string.rep("\t", indent) .. tostring(k) .. " = " .. tostring(v))
		end
	end
end

return print_table