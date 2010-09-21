.PHONY: clean

all: inline.cpp
	g++ -Winline -msse4.1 -g -o inline $<

clean:
	rm inline
