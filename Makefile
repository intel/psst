VERSION = 2.1

BINDIR = /usr/bin
MANDIR = /usr/share/man/man1
WARNFLAGS = -Wall -Wformat
CC ?= gcc
CFLAGS += -D VERSION=\"$(VERSION)\"
CFLAGS += -D_LINUX_ -Wall -O2 -Wfloat-equal
DBG_CFLAGS = -DDEBUG -g -O0
LDFLAGS += -DPASS2
TARGET = psst

INSTALL_PROGRAM = install -m 755 -p
DEL_FILE = rm -f

SRC_PATH = ./src
OBJS =  $(SRC_PATH)/parse_config.o $(SRC_PATH)/logger.o $(SRC_PATH)/rapl.o \
	$(SRC_PATH)/perf_msr.o $(SRC_PATH)/psst.o
OBJS +=

psst: $(OBJS) Makefile
	$(CC) ${CFLAGS} $(LDFLAGS) $(OBJS) -o $(TARGET) -lpthread -lrt -lm

install:
	mkdir -p $(BINDIR)
	$(INSTALL_PROGRAM) "$(TARGET)" "$(BINDIR)/$(TARGET)"
	gzip -c psst.1 > psst.1.gz
	mv -f psst.1.gz $(MANDIR)

uninstall:
	$(DEL_FILE) "$(BINDIR)/$(TARGET)"

clean:
	find . -name "*.o" | xargs $(DEL_FILE)
	rm -f $(TARGET)

dist:
	git tag v$(VERSION)
	git archive --format=tar --prefix="$(TARGET)-$(VERSION)/" v$(VERSION) | \
	gzip > $(TARGET)-$(VERSION).tar.gz
