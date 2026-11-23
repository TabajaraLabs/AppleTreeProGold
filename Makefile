SRC = src/atpg.c src/plat_a2.c src/a2io.s
all: build/ATPG.SYSTEM build/appletree.po test/atpg_linux
build/ATPG.SYSTEM: $(SRC) src/plat.h
	mkdir -p build
	cl65 -t apple2 -C apple2-system.cfg -Os -Cl -Wl -D,__STACKSIZE__=0x400 -m build/atpg.map -o $@ $(SRC)
build/appletree.po: build/ATPG.SYSTEM tools/mkpo.py
	python3 tools/mkpo.py $@ APPLETREE build/ATPG.SYSTEM:FF:2000
test/atpg_linux: src/atpg.c src/plat_linux.c src/plat.h
	gcc -O1 -o $@ src/atpg.c src/plat_linux.c
clean:
	rm -rf build test/atpg_linux
