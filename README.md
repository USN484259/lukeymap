# lukeymap

Lua scripted key remapping program

## Startup and command line

### Command line usage
```
lukeymap [options] main_module [params]
```
Options:
+ -n, --nice=NICE		set the priority using nice(2)
+ -t, --time=TIME		set script running time limit in ms
+ -m, --memory=MEMORY	set memory limit for Lua runtime, supports K/M/G postfix
+ -l, --lock			lock memory using mlockall(2), must be used with -m

*main_module* is the Lua script file being loaded and executed, *params* are parameters passed to the script. See **modules and require()** for more details. 

### Builtin modules

Below are builtin modules that can be run as main_module:

+ list_devices  
	If run without parameters, list current available input devices. Otherwise, parse parameters as device names and show details of mentioned devices.
+ log_keys  
	Takes parameters as device names, monitor these devices and print their input events to standard output.
+ remap  
	Takes one parameter as the config file, do key remapping based on configuration. See **High-level API** for details.

Below are builtin modules that are "libraries" and cannot be run as main_module, but are dependencies of other builtin modules. You can also require and use them.

+ print_table  
	Prints Lua nested tables in a structured format. Cannot handle reference loops.
+ device_manager  
	Provides a handler function that could help you manage the pairs of original device and the matching remap device.

### Examples



### Standard Lua API

Only part of the standard Lua API are available to scripts

Below are available modules
+ coroutine
+ table
+ string
+ utf8
+ bit32
+ math

In global space, these functions are **unavailable**

+ dofile
+ loadfile
+ load
+ loadstring

The *require* function is a modified version, see below.


## High-level API
This is the interface provided by **remap** module. Load **remap** as main module, pass the config file as the parameter, and it will handle key remapping based on config file contents. Below is the overview of the config file.

### Getting the key code

**remap** module exports two global objects *KEY* and *BTN*. They are the *device.code_num()* wrapper, but uses Lua metatable and syntax sugar to make it easier to retrieve key codes by name. Below are the examples. For key code and key names, refer to [linux/input-event-codes.h](https://elixir.bootlin.com/linux/v6.18.3/source/include/uapi/linux/input-event-codes.h).

```lua
KEY.A		-- #define KEY_A			30
KEY.LEFTMETA	-- #define KEY_LEFTMETA		125
KEY.KPENTER	-- #define KEY_KPENTER		96
BTN.RIGHT	-- #define BTN_RIGHT		0x111
```

### Config file overview

The config file is a Lua script, and is loaded by builtin **remap** main module. The config file should returns a Lua *table* of *configurations*. Below is an example:

```lua

local config_1 = {
	match_1,
	rules_1,
}
local config_2 = {
	match_2,
	rules_2,
}
local config_3 = {
	match_3,
	rules_3,
}

return {
	config_1,
	config_2,
	config_3,
}
```

Each *configuration* contains *match* part and *rules* part. For each input device, **remap** module itreates over the *configuraions* and use the *match* part to check against the device. On the first matched *configuration*, its *rules* part will be applied to this device, and following *configuraions* will **not** be checked for this device.


### The match part

The *match* part can be either a *function* or a *table*.

In the *function* form, the function is called with the device info as the parameter, the info is obtained from *evdev:info()*. If the function returns a *true* value, this *configuration* is considered matched.

In the *table* form, it should contain zero or more key-value pairs. For each pair, it is matched against the device info. The same key must also exist in the device info table and its value must also match. One exception is the *name* key, which checks whether its value is a sub-string of the *name* field in device info. If all pairs match the device info, this *configuration* is considered matched. For an empty table, it matches *any* device.

```lua
local match_1 = {
	name = "Keyboard",
	vendor = 1234
}

local match_2 = function(info)
	return info.events.EV_KEY and info.events.EV_REL
end

```


### The rules part (simple)

The *rules* part is a *table* that contains one or more *rule*. Each *rule* contains two tables: *watch* and *target*. The *watch* and *target* are *table* of one or more key codes.

```lua
local rules_1 = {
	{ {KEY.KP0}, {KEY.LEFTMETA} },
	{ {KEY.KPDOT}, {KEY.LEFTCTRL, KEY.C} },
	{ {KEY.RIGHTCTRL, KEY.RIGHTSHIFT, BTN.LEFT}, {KEY.LEFTMETA, BTN.LEFT} },
}
```

When all keys in he *watch* are physically pressed, **and** the last pressed key is the last element in *watch*, this *rule* is *activated*. The keys in *watch* are reported released in reverse order, and keys in *target* are reported pressed in order.

For an activated *rule*, when **any** of the key in *watch* is physically released, it is *deactivated*. The keys in *target* are reported released in reverse order, then keys in *watch* are reported pressed in order, except the physically released key.


### The rules part (detailed)

The *rules* part is a *table* that contains one or more *rule*. Each *rule* contains two or three tables: the *mod* part, *trigger* part and *handler* part.

+ The *mod* part is a *table* that contains zero or more key codes or *virtual keys*.
+ The *trigger* part can be omitted, or be either a *function* or a key code.
+ The *handler* part can be either a *function* or a *table* of one or more key codes.
+ If the *trigger* part is a *function*, then *handler* part must also be a *function*.
+ If *trigger* part is omitted, the last key code in *mod* part will be poped and become the *trigger* part.


The **remap** module also manages a *key_state* table for each device. In this table, key is the key code, and value is the current state of the key.
When an input event is received and is key event, the *key_state* will update to reflect the key status change **before** processing *trigger* and/or *handler* of the *rule*.
The *trigger* function is allowed to set non-integer keys in the *key_state*, to represent *virtual keys*. *virtual keys* can be used in *mod* part to implement *layers*. When reporting key press or release, *virtual keys* will be skipped. Note since the *virtual keys* are in the *key_state* table, they are viewable and settable by all *rules* of the same device.

When a key press event is received, and *trigger* is a key code that matches the event, it will check in the *key_state* to see if **all** keys in *mod* is in pressed state. If so, the current *rule* will be *activated*.

When a key release event is received, it will check in the *key_state* to see if **any** key in *mod* is in released state, or if *trigger* is a key code that matches the event. If so, the current *rule* will be *deactivated*.

If an event is received and *trigger* is a *function*, the *trigger* is called with the *event object* and the *key_state* as parameters. If it returns a *true* value, the the current *rule* will be *activated*.

When the *rule* is *activated* and *handler* is a *table*, and *rule.active* is a *false* value, the keys in *mod* and *trigger* are reported released in reverse order, and keys in *handler* are reported pressed in order. It will also set *rule.active* to *true* to prevent re-activation.

When the *rule* is *deactivated* and *handler* is a *table*, the keys in *handler* are reported released in reverse order, and keys in *mode* and *trigger* are reported pressed in order, excluding the key that caused the deactivation. It will also set *rule.active* to *false*.

When *rule* is *activated* or *deactivated* and *handler* is a *function*, it is called with the *event object*, *arg* and the *rule* as parameters. If *activated*, *arg* is the value returned by *trigger*, or *true* if *trigger* is not a *function*. If *deactivated*, *arg* is *nil*. The *handler* function should return a table containing zero or more *event objects* that would be reported, or a *false* value to make the original event to be reported as is.

*handler* function is allowed to use non-integer keys in *rule* table to store its own data and states.

```lua
-- ANother WAy TO IMplement CApsLOck DElay FIx
-- see also https://github.com/hexvalid/Linux-CapsLock-Delay-Fixer

local EV_KEY = device.type_num("EV_KEY")

local rules_2 = {{
	-- mod
	{},

	-- trigger
	function(ev, key_state)
		return ev.type == EV_KEY and ev.code == KEY.CAPSLOCK and ev.value == 1
	end,

	-- handler
	function (ev, arg, rule)
		if arg then
			return {
				ev, {type = ev.type, code = ev.code, value = 0}
			}
		else
			return {}
		end
	end
}}
```

## Low-level API
<sub>This section is mostly written by Copilot, GPT 4.1</sub>

This is the raw API provided by **lukeymap** C code. High-level API is implemented on this.
You can use low-level API to write complex remapping logic, or advanced usage such as emit keys based on joystick inputs.

### sys

**sys.main**
: (string) The name of the main module. This variable is set to the module name of the main script at startup. It can be reassigned by scripts to support chain loading.

**sys.meminfo** ()
: Returns two integers, *used* and *limit*, representing the current memory usage and the memory limit (in bytes) for the Lua environment. *limit* may be *nil* if there is no memory limit.

**sys.exit** ([exit_now])
: Requests program termination. If *exit_now* is *true*, exits immediately; otherwise, continue running and exit at the next event loop iteration. Note that this function **does** return if *exit_now* is not *true*.

**sys.gettime** ()
: Returns the current monotonic time as two integers: seconds and nanoseconds since an unspecified epoch. Useful for measuring intervals.

**sys.timer** (timer_handler)
: Creates and returns a new *timer object*. The *timer_handler* is a function to be called when the timer expires.


### timer object

A *timer object* represents a single-shot timer, created by *sys.timer*.

**timer:close** ()
: Closes the timer and releases its resources. After closing, the timer object is invalid and should not be used.

**timer:handler** ([timer_handler])
: Gets or sets the handler function for the timer. If *timer_handler* is provided, sets it as the new handler and returns the previous handler. If omitted, returns the current handler.

**timer:set** (sec [, nsec])
: Sets the timer to expire after *sec* seconds and optional *nsec* nanoseconds (relative time). Accepts either two integers or a single floating-point value for seconds.

**timer:set** (true, sec [, nsec])
: Sets the timer to expire at an absolute time, specified as seconds and optional nanoseconds since the monotonic clock epoch. Accepts either two integers or a single floating-point value for seconds.

**timer:get** ()
: Returns the remaining time until expiration as two integers: seconds and nanoseconds.

**timer:cancel** ()
: Cancels the timer if it is running. The handler will not be called until the timer is set again.


### device

The *device* module provides functions for interacting with Linux input devices and creating virtual devices.

**device.open** (devname [, evdev_handler])
: Opens the input device named *devname* and returns an *evdev object*. If *evdev_handler* is provided, it is set as the event handler for the device.

**device.create** (evdev_obj)
: Creates a uinput device based on the properties of the given *evdev object*. Returns a *uinput object*.

**device.create** (table)
: Creates a uinput device using the configuration specified in the Lua table. Returns a *uinput object*.

**device.type_name** (type_id)
: Returns the event type name for the given event type. Returns *nil* if not found.

**device.code_name** (type_id, code)
: Returns the event code name for the given event type and code. Returns *nil* if not found.

**device.value_name** (type_id, code, value)
: Returns the value name for the given event type, code, and value. Returns *nil* if not found.

**device.type_num** (type_name)
: Returns the integer event type for the given type name. Returns *nil* if not found.

**device.code_num** (code_name)
: Returns two integers event code and type for the given code name. Returns *nil* if not found.

**device.value_num** (type_id, code, value_name)
: Returns the integer value for the given event type, code, and value name. Returns *nil* if not found.

**device.value_num** (type_name, code_name, value_name)
: Returns the integer value for the given type name, code name, and value name. Returns *nil* if not found.


### evdev object

An *evdev object* represents an open Linux input device, as returned by *device.open*.

**evdev:close** ()
: Closes the device and releases its resources. The object becomes invalid after closing.

**evdev:info** ()
: Returns a table containing information about the device, such as name, physical location, and supported event types and codes.

**evdev:handler** ([evdev_handler])
: Gets or sets the event handler function for this device. If *evdev_handler* is provided, sets it and returns the previous handler. If omitted, returns the current handler.

**evdev:monitor** (onoff)
: Enables or disables monitoring of the device. If *onoff* is *true*, monitoring is enabled; if *false*, it is disabled.

**evdev:grab** (onoff)
: Grabs or ungrabs the device for exclusive access. If *onoff* is *true*, grabs the device; if *false*, releases it.

**evdev:read** ()
: Reads and returns an array of incoming input events from the device. Each event is represented as a *table* with fields type, code and value.


### uinput object

A *uinput object* represents a virtual input device created with *device.create*.

**uinput:close** ()
: Closes the uinput device and releases its resources. The object becomes invalid after closing.

**uinput:name** ()
: Returns the device node name of the uinput device, such as "eventX".

**uinput:write** (array)
: Writes an array of input events to the uinput device. Each event should be a *table* with fields type, code and value.


### handler_func

Below are the prototype of various handler functions.

**timer_handler** (timer_obj)
: Called when a timer expires. Receives the *timer object* as its only argument.

**evdev_handler** (evdev_obj)
: Called when one or more input events are received for the device. Receives the *evdev object* as its only argument.

**device_handler** (op, devname)
: Called when a device is added or removed. *op* is a string, either "add" or "del"; *devname* is the device name.


## modules and require()

module are Lua source files either loaded by 'require' calls, or initially loaded as 'main' module

When a module is loaded, it is called with parameters as follows:
The first parameter is the module name (same as the filename).
For module loaded by 'require', the following parameters are those passed to 'require' call.
For main module, the following parameters are those on the command line.

On startup, the 'sys.main' variable is set to the module name of the main module. Thus scripts can check if they are loaded as main module by comparing the first parameter against sys.main.
Note that the 'sys.main' variable is writable by scripts, so it is possible to do "chain loading", that is, a "mixin" script is loaded, possibly modify the environment, then set sys.main to the name of next module and chain loading it.

For main module, it should return one function as *device_handler*. It is called whenever a device is added or removed.
For module loaded by 'require', it could return any number of values to the caller. Caller and callee should agree on the number and type of the values returned.
Note that a module may return different set of values based on wether they are loaded as main module and the parameters passed in.

require(filename, ...) => ...

The 'require' call takes the first parameter as module filename, the following parameters are passed to the module.

If the filename starts with '/', then it is treated as the absolute path (including the extension name).

Else, the filename is parsed in the "a.b.c" form, and will load "a/b/c.lua" relative to the working directory. If '/' appears in filename, it is processed the same as '.'

In either case, '..' is not accepted, and would result in error.

