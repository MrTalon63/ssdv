VERSION?=2.0.0

.PHONY: all clean install

all:
	cmake -B build -DVERSION="$(VERSION)" -DCMAKE_BUILD_TYPE=Release
	cmake --build build --config Release

install: all
	mkdir -p $(DESTDIR)/usr/bin
	install -m 755 build/bin/ssdv $(DESTDIR)/usr/bin
	install -m 755 build/bin/ssdv-gui $(DESTDIR)/usr/bin

clean:
	rm -rf build