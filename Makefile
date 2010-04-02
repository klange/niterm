LIBS = -lc -lbogl

CFLAGS = -O2 -g -D_GNU_SOURCE
WARNCFLAGS += -Wall -D_GNU_SOURCE
ALLCFLAGS = $(CFLAGS) $(WARNCFLAGS) $(FBCFLAGS)

ECHO = `which echo`

architecture := $(shell dpkg-architecture -qDEB_BUILD_ARCH_CPU)

all:     niterm

bogl-term.o: bogl-term.c bogl-term.h
	@$(ECHO) -e "\033[1;35mBuilding bogl-term...\033[0m"
	@$(CC) $(ALLCFLAGS) -o bogl-term.o -c bogl-term.c

bogl-bgf.o: bogl-bgf.c bogl-bgf.h
	@$(ECHO) -e "\033[1;33mBuilding bogl-bgf...\033[0m"
	@$(CC) $(ALLCFLAGS) -o bogl-bgf.o -c bogl-bgf.c

niterm: niterm.cpp bogl-term.o bogl-bgf.o
	@$(ECHO) -e "\033[1;34mBuilding niterm...\033[0m"
	@g++ -Wall -lanthy -lbogl -o niterm niterm.cpp bogl-term.o bogl-bgf.o

clean: 
	@$(ECHO) -e "\033[1;31mCleaning...\033[0m"
	@rm -rf niterm *.o lang.h tmp.*.c $(LIB)

distclean: clean

force:

install: all
	@$(ECHO) -e "\033[1;32mInstalling...\033[0m"
	@install -m755 niterm $(DESTDIR)/usr/bin
	@chmod u+s $(DESTDIR)/usr/bin/niterm
	@install -m644 niterm.1 $(DESTDIR)/usr/share/man/man1
	@install -d $(DESTDIR)/usr/share/terminfo
	@tic -o$(DESTDIR)/usr/share/terminfo niterm.ti
	@install -d $(DESTDIR)/usr/share/niterm
	@install -m644 unifont.bgf.gz $(DESTDIR)/usr/share/niterm
	@gzip -d -f $(DESTDIR)/usr/share/niterm/unifont.bgf.gz
