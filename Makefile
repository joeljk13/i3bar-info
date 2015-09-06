.PHONY: all
all: i3bar-info

CFLAGS = -g -O2 -Wall
prefix = /usr

i3bar-info: i3bar-info.o

.PHONY: install
install:
	mkdir -p $(prefix)/bin
	cp i3bar-info $(prefix)/bin

.PHONY: uninstall
uninstall:
	rm $(prefix)/bin/i3bar-info

.PHONY: clean
clean:
	rm -f i3bar-info *.[ios]
