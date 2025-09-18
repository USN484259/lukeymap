# lukeymap

Lua scripted key remapping program



## Startup and command line



## Standard Lua API

Only part of the standard Lua API are available to scripts

Below are available modules
+ coroutine
+ table
+ string
+ utf8
+ bit32
+ math

In global space, these functions are unavailable

+ dofile
+ loadfile
+ load
+ loadstring

The 'require' function is a modified version, see below.


## Additional API

### sys

sys.main : string
sys.meminfo() => used, limit
sys.exit([exit_now])	-- running until next event loop iteration if not exit_now
sys.gettime() => sec, nsec
sys.timer(handler_func) => timer_obj


### timer object

timer:close()
timer:handler([new_handler]) => old_handler
timer:set(sec, [nsec])		-- relatime
timer:set(sec_float)		-- relatime
timer:set(true, sec, [nsec])	-- abstime
timer:set(true, sec_float)	-- abstime
timer:get() => sec, nsec
timer:cancel()


### device

device.open(devname, [handler_func]) => evdev_obj
device.create(evdev_obj) => uinput_obj
device.create(table) => uinput_obj

device.type_name(type_id) => type_name
device.code_name(type_id, code) => code_name
device.value_name(type_id, code, value) => value_name
device.type_num(type_name) => type_id
device.code_num(code_name) => code, type_id
device.value_num(type_id, code, value_name) => value
device.value_num(type_name, code_name, value_name) => value


### evdev object

evdev:close()
evdev:info() => table
evdev:handler([new_handler]) => old_handler
evdev:monitor([onoff])
evdev:grab([onoff])
evdev:read() => array


### uinput object

uinput:close()
uinput:name() => devname
uinput:write(array)


### handler_func

timer_handler(timer_obj)
evdev_handler(evdev_obj)
event_handler(op, devname) -- op is "add" or "del"


## modules and 'require'

module are Lua source files either loaded by 'require' calls, or initially loaded as 'main' module

When a module is loaded, it is called with parameters as follows:
The first parameter is the module name (usually the filename).
For module loaded by 'require', the following parameters are those passed to 'require' call.
For main module, the following parameters are those on the command line.

On startup, the 'sys.main' variable is set to the module name of the main module. Thus scripts can check if they are loaded as main module by comparing the first parameter against sys.main.
Note that the 'sys.main' variable is writable by scripts, so it is possible to do "chain loading", that is, a "mixin" script is loaded, possibly modify the environment, then set sys.main to the name of next module and chain loading it.

For main module, it should return one function that is called whenever a device is added or removed (see event_handler).
For module loaded by 'require', it could return any number of values to the caller. Caller and callee should agree on the number and type of the values returned.
Note that a module may return different set of values based on wether they are loaded as main module.

require(filename, ...) => ...

The 'require' call takes the first parameter as module filename, the following parameters are passed to the module.
'require' only searches files starting from the 'scripts dir', that is the path specified by '-d' switch on the command line, or the working directory by default. Module filename may omit ".lua" extension, in this case, the exact filename is checked first, if the file is not found, the filename is appended ".lua" extension and checked again. The appended extension is not carried into the module name when the module is loaded and called.

