# Copyright (c) 2011 Tim van der Molen <tbvdm@xs4all.nl>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

.if exists(config.mk)
.include "config.mk"
.endif

PROG=		siren
VERSION!=	cat version
DIST=		${PROG}-${VERSION}

SRCS+=		bind.c browser.c cache.c command.c conf.c dir.c format.c \
		history.c library.c log.c menu.c msg.c option.c path.c \
		player.c plugin.c prompt.c queue.c screen.c siren.c track.c \
		view.c xmalloc.c xpathconf.c
OBJS=		${SRCS:S,c$,o,}
LOBJS=		${SRCS:S,c$,ln,}

IP_SRCS=	${IP:S,^,ip/,:S,$,.c,}
IP_LIBS=	${IP_SRCS:S,.c$,.so,}
IP_OBJS=	${IP_SRCS:S,.c$,.lo,}
IP_LOBJS=	${IP_SRCS:S,.c$,.ln,}

OP_SRCS=	${OP:S,^,op/,:S,$,.c,}
OP_LIBS=	${OP_SRCS:S,.c$,.so,}
OP_OBJS=	${OP_SRCS:S,.c$,.lo,}
OP_LOBJS=	${OP_SRCS:S,.c$,.ln,}

CC?=		cc
CTAGS?=		ctags
LINT?=		lint
MKDEP?=		mkdep

INSTALL_DIR=	install -dm 755
INSTALL_BIN=	install -m 555
INSTALL_MAN=	install -m 444

CDIAGFLAGS+=	-Wall -W -Wbad-function-cast -Wcast-align -Wcast-qual \
		-Wformat=2 -Wpointer-arith -Wshadow -Wundef -Wwrite-strings
CFLAGS+=	${CDIAGFLAGS}
CPPFLAGS+=	-DVERSION=\"${VERSION}\"
LDFLAGS+=	-lcurses -pthread -Wl,--export-dynamic
LINTFLAGS?=	-hx
MKDEPFLAGS?=	-a

# lint does not understand "-pthread", so use "-lpthread".
.if make(lint)
LDFLAGS+=	-lpthread
.endif

.PHONY: all clean cleandir cleanlog depend dist install lint

.SUFFIXES: .c .ln .lo .o .so

.c.ln:
	${LINT} ${LINTFLAGS} ${CPPFLAGS} ${CPPFLAGS_${@:T:R}} -i -o $@ $<

.c.lo:
	${CC} ${CFLAGS} ${CPPFLAGS} ${CPPFLAGS_${@:T:R}} -fPIC -c -o $@ $<

.c.o:
	${CC} ${CFLAGS} ${CPPFLAGS} -c -o $@ $<

.lo.so:
	${CC} -fPIC -shared -o $@ $< ${LDFLAGS_${@:T:R}}

all: ${PROG} ${IP_LIBS} ${OP_LIBS}

${PROG}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LDFLAGS}

.depend: ${SRCS} ${IP_SRCS} ${OP_SRCS} ${PROG}.h
	${MKDEP} ${MKDEPFLAGS} ${CPPFLAGS} ${SRCS}
.for src in ${IP_SRCS} ${OP_SRCS}
	${MKDEP} ${MKDEPFLAGS} ${CPPFLAGS_${src:T:R}} ${src}
.endfor

clean:
	rm -f core *.core ${PROG} ${OBJS} ${LOBJS}
	rm -f ${IP_LIBS} ${IP_OBJS} ${IP_LOBJS}
	rm -f ${OP_LIBS} ${OP_OBJS} ${OP_LOBJS}

cleandir: clean cleanlog
	rm -f .depend tags config.h config.mk

cleanlog:
	rm -f *.log

depend: .depend

dist:
	hg archive -X .hg\* -r ${VERSION} ${DIST}
	chmod -R go+rX ${DIST}
	GZIP=-9 tar -czf ${DIST}.tar.gz ${DIST}
	rm -fr ${DIST}
	gpg -bu 4cdfe96f ${DIST}.tar.gz

install:
	${INSTALL_DIR} ${DESTDIR}${BINDIR}
	${INSTALL_DIR} ${DESTDIR}${MANDIR}/man1
	${INSTALL_DIR} ${DESTDIR}${PLUGINDIR}
	${INSTALL_DIR} ${DESTDIR}${PLUGINDIR}/ip
	${INSTALL_DIR} ${DESTDIR}${PLUGINDIR}/op
	${INSTALL_BIN} ${PROG} ${DESTDIR}${BINDIR}
	${INSTALL_MAN} ${PROG}.1 ${DESTDIR}${MANDIR}/man1
	${INSTALL_BIN} ${IP_LIBS} ${DESTDIR}${PLUGINDIR}/ip
	${INSTALL_BIN} ${OP_LIBS} ${DESTDIR}${PLUGINDIR}/op

lint: ${LOBJS} ${IP_LOBJS} ${OP_LOBJS}
	${LINT} ${LINTFLAGS} ${LDFLAGS:M-l*} ${LOBJS} ${IP_LOBJS} ${OP_LOBJS}

tags: ${SRCS} ${IP_SRCS} ${OP_SRCS} ${PROG}.h
	${CTAGS} -dtw ${SRCS} ${IP_SRCS} ${OP_SRCS}

uninstall:
	rm -f ${DESTDIR}${BINDIR}/${PROG}
	rm -f ${DESTDIR}${MANDIR}/man1/${PROG}.1
	rm -fr ${DESTDIR}${PLUGINDIR}
