.PHONY: clean all

# Compiller
ifeq ($(shell uname),FreeBSD)
Compiller=gcc44 -lstdc++ -Winline -g
else
Compiller=g++ -Wall -pedantic -Winline -g
endif
OCompiller=$(Compiller) -O3

all: pfc_sse pfc_unpack_sse

pfc_sse: pfc_sse.cpp
	$(Compiller) -o $@ $<
	$(OCompiller) -o o_$@ $<


pfc_unpack_sse: pfc_unpack_sse.cpp
	$(Compiller) -o $@ $<
	$(OCompiller) -o o_$@ $<

pkgDir = pfc_sse_$(shell date +%d%m%Y)
pkg: Makefile pfc_sse.cpp pfc_unpack_sse.cpp
	@printf "Making pkg ... "
	@mkdir $(pkgDir)
	@cp $^ $(pkgDir)
	@tar -cjf $(pkgDir).tar.bz2 $(pkgDir)
	@rm -r $(pkgDir)
	@printf "DONE\n"

clean:
	rm *pfc_sse *pfc_unpack_sse *.tar.bz2
