.PHONY: clean

# Compiller
ifeq ($(shell uname),FreeBSD)
Compiller=gcc44 -lstdc++
else
Compiller=g++
endif

all: pfc_sse.cpp
	$(Compiller) -Winline -msse3 -g -o pfc_sse $<

# with havy optimization
o: pfc_sse.cpp
	$(Compiller) -O3 -Winline -msse4.1 -g -o pfc_sse $<

pkgDir = pfc_sse_$(shell date +%d%m%Y)
pkg: Makefile pfc_sse.cpp
	@printf "Making pkg ... "
	@mkdir $(pkgDir)
	@cp $^ $(pkgDir)
	@tar -cjf $(pkgDir).tar.bz2 $(pkgDir)
	@rm -r $(pkgDir)
	@printf "DONE\n"

clean:
	rm pfc_sse *.tar.bz2
