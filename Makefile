# Makefile for Scheduler (SCHED)
PROG=	sched
SRCS=	main.c schedproc.cpp

DPADD+=	${LIBSYS} ${LIBTIMERS}
LDADD+=	-lsys -ltimers

MAN=

BINDIR?= /usr/sbin

CPPFLAGS.main.c+=	-I${NETBSDSRCDIR}
CPPFLAGS.schedproc.cpp+=	-I${NETBSDSRCDIR}

.include <minix.bootprog.mk>
