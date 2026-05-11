VERSION?=DEV

.PHONY: all clean install

all:
	cmake -B build -DVERSION="$(VERSION)" -DCMAKE_BUILD_TYPE=Release -Wno-dev --no-warn-unused-cli
	cmake --build build --config Release
	chmod +x build/bin/ssdv
	chmod +x build/bin/ssdv-gui


install: all
	mkdir -p $(DESTDIR)/usr/bin
	install -m 755 build/bin/ssdv $(DESTDIR)/usr/bin
	install -m 755 build/bin/ssdv-gui $(DESTDIR)/usr/bin

clean:
	rm -rf build