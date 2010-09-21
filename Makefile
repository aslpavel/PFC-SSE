.PHONY: clean

all: inline.cpp
	g++ -msse4.1 -g -o inline $<

clean:
	rm inline
