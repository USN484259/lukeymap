INSTALL_TOP= /usr/local


all:	lukeymap

lukeymap clean:
	cd src/ && $(MAKE) $@


install:
	install -m 0755 src/lukeymap $(INSTALL_TOP)/bin/
	install -m 0644 -D -t $(INSTALL_TOP)/lib/lukeymap/ lib/*.lua
	install -m 0644 -D -b -T remap-config.lua /etc/lukeymap/remap.conf
	install -m 0644 -b lukeymap-remap.service /etc/systemd/system/


.PHONY:	all lukeymap clean install

