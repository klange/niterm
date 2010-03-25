LIBS = -lc -lbogl

CFLAGS = -O2 -g -D_GNU_SOURCE
WARNCFLAGS += -Wall -D_GNU_SOURCE
ALLCFLAGS = $(CFLAGS) $(WARNCFLAGS) $(FBCFLAGS)

architecture := $(shell dpkg-architecture -qDEB_BUILD_ARCH_CPU)

all:     niterm

bogl-term.o: bogl-term.c bogl-term.h
	$(CC) $(ALLCFLAGS) -o bogl-term.o -c bogl-term.c

bogl-bgf.o: bogl-bgf.c bogl-bgf.h
	$(CC) $(ALLCFLAGS) -o bogl-bgf.o -c bogl-bgf.c

niterm: niterm.cpp bogl-term.o bogl-bgf.o
	g++ -Wall -lbogl -o niterm niterm.cpp bogl-term.o bogl-bgf.o

clean: 
	rm -rf niterm *.o lang.h tmp.*.c $(LIB)

distclean: clean
	rm -f $(LIB) .depend *~ .nfs*

force:

install: all
	install -m755 niterm $(DESTDIR)/usr/bin
	install -m644 niterm.1 $(DESTDIR)/usr/share/man/man1
	install -d $(DESTDIR)/usr/share/terminfo
	tic -o$(DESTDIR)/usr/share/terminfo niterm.ti
