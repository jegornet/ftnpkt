# ftnpkt — build file (C / CMake)
#
# Common targets:
#   make            build the ftnpkt binary (Release) into ./bin
#   make debug      build a debug binary into ./build
#   make test       build then run a create/addmsg/dump smoke round-trip
#   make rpm        build source + binary RPM packages into ./bin
#   make clean      remove ./build and ./bin

CMAKE   ?= cmake
BINDIR   = bin
CMD      = ftnpkt
# Single source of truth for the version is CMakeLists.txt.
VERSION  = $(shell sed -n 's/^[[:space:]]*VERSION[[:space:]]*\([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
distdir  = ftnpkt-$(VERSION)
DISTFILES = CMakeLists.txt LICENSE Makefile ftnpkt.spec src

.PHONY: all build debug test dist rpm clean

all: build

# Build the single ftnpkt binary into ./bin/ftnpkt.
build:
	@mkdir -p $(BINDIR)
	$(CMAKE) -S . -B build -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build build -j
	@cp build/$(CMD) $(BINDIR)/$(CMD)

debug:
	$(CMAKE) -S . -B build -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build build -j

# Build, then run an end-to-end create -> addmsg -> dump smoke test.
test: build
	@out=$$(mktemp -d)/p.pkt; \
	./$(BINDIR)/$(CMD) create $$out --from-addr 2:382/736 --to-addr 2:382/999 --password pwd >/dev/null; \
	./$(BINDIR)/$(CMD) addmsg $$out 'Hello, FidoNet!' --from Jegor --from-addr 2:382/736 \
		--subject Test --areatag LINUX.GEN --charset cp866 --addkludge 'CHRS: CP866 2' >/dev/null; \
	dump=$$(./$(BINDIR)/$(CMD) dump $$out); \
	echo "$$dump" | grep -q 'Packet type: Type-2e' && \
	echo "$$dump" | grep -q 'Messages: 1' && \
	echo "$$dump" | grep -q 'fromUserName: Jegor' && \
	echo "smoke test OK"; rm -rf $$(dirname $$out)

clean:
	rm -rf build $(BINDIR)

# Build a source tarball ftnpkt-<version>.tar.gz with a matching top-level
# directory (required by the spec's %setup macro). Reproducible: zeroed owner.
dist:
	@rm -rf $(distdir)
	@mkdir -p $(distdir)
	@cp -a $(DISTFILES) $(distdir)/
	tar --owner=0 --group=0 --numeric-owner -czf $(distdir).tar.gz $(distdir)
	@rm -rf $(distdir)
	@echo "created $(distdir).tar.gz"

# Build source + binary RPMs from a freshly created source tarball.
rpm: dist
	rpmbuild -ta $(distdir).tar.gz
