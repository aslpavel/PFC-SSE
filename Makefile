.PHONY: clean

all: inline.cpp
	g++ -Winline -msse4.1 -g -o inline $<

# with havy optimization
o: inline.cpp
	g++ -O3 -Winline -msse4.1 -g -o inline $<

pkgDir = pfc_sse
pkg: Makefile inline.cpp
	@printf "Making pkg ... "
	@mkdir $(pkgDir)
	@cp $^ $(pkgDir)
	@tar -cjf $(pkgDir)_$(shell date +%d%m%Y).tar.bz2 $(pkgDir)
	@rm -r $(pkgDir)
	@printf "DONE\n"

clean:
	rm inline *.tar.bz2
