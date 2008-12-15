include config.mk

DEP = oi.h oi_file.h oi_async.h
SRC = oi.c oi_file.c oi_async.c
OBJ = ${SRC:.c=.o}

VERSION = 0.1
NAME=liboi
OUTPUT_LIB=$(NAME).$(SUFFIX).$(VERSION)
OUTPUT_A=$(NAME).a

LINKER=$(CC) $(LDOPT)

all: options $(OUTPUT_LIB) $(OUTPUT_A) test/ping_pong test/connection_interruption test/file test/sleeping_tasks

options:
	@echo ${NAME} build options:
	@echo "CC       = ${CC}"
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "LDOPT    = ${LDOPT}"
	@echo "SUFFIX   = ${SUFFIX}"
	@echo "SONAME   = ${SONAME}"
	@echo

$(OUTPUT_LIB): $(OBJ) 
	@echo LINK $@
	@$(LINKER) -o $(OUTPUT_LIB) $(OBJ) $(SONAME) $(LIBS)

$(OUTPUT_A): $(OBJ)
	@echo AR $@
	@$(AR) cru $(OUTPUT_A) $(OBJ)
	@echo RANLIB $@
	@$(RANLIB) $(OUTPUT_A)

.c.o:
	@echo CC $<
	${CC} -c ${CFLAGS} $<

${OBJ}: ${DEP}

FAIL=echo "\033[1;31mFAIL\033[m"
PASS=echo "\033[1;32mPASS\033[m"
TEST= && $(PASS) || $(FAIL)

test: test/ping_pong test/connection_interruption test/sleeping_tasks
	@echo "ping pong"
	@echo -n "- unix: "
	@./test/ping_pong unix $(TEST)
	@echo -n "- tcp: "
	@./test/ping_pong tcp $(TEST)
	@echo -n "- unix secure: "
	@./test/ping_pong unix secure $(TEST)
	@echo -n "- tcp secure: "
	@./test/ping_pong tcp secure $(TEST)
	@echo "connection interruption"
	@echo -n "- unix: "
	@./test/connection_interruption unix $(TEST)
	@echo -n "- tcp: "
	@./test/connection_interruption tcp $(TEST)
	@echo -n "- unix secure: "
	@./test/connection_interruption unix secure $(TEST)
	@echo -n "- tcp secure: "
	@./test/connection_interruption tcp secure $(TEST)
	@echo -n "sleeping tasks: "
	@./test/sleeping_tasks $(TEST)


test/ping_pong: test/ping_pong.c $(OUTPUT_A)
	@echo BUILDING test/ping_pong
	@$(CC) -I. $(LIBS) $(CFLAGS) -lev -o $@ $^

test/connection_interruption: test/connection_interruption.c $(OUTPUT_A)
	@echo BUILDING test/connection_interruption
	@$(CC) -I. $(LIBS) $(CFLAGS) -lev -o $@ $^

test/fancy_copy: test/fancy_copy.c $(OUTPUT_A)
	@echo BUILDING test/fancy_copy
	@$(CC) -I. $(LIBS) $(CFLAGS) -lev -o $@ $^

test/file: test/file.c $(OUTPUT_A)
	@echo BUILDING test/file
	@$(CC) -I. $(LIBS) $(CFLAGS) -lev -o $@ $^

test/sleeping_tasks: test/sleeping_tasks.c $(OUTPUT_A)
	@echo BUILDING test/sleeping_tasks
	@$(CC) -I. $(LIBS) $(CFLAGS) -lev -o $@ $^

clean:
	@echo CLEANING
	@rm -f ${OBJ} $(OUTPUT_A) $(OUTPUT_LIB) $(NAME)-${VERSION}.tar.gz 
	@rm -f test/ping_pong test/connection_interruption test/fancy_copy test/file
	@rm -f test/sleeping_tasks


install: $(OUTPUT_LIB) $(OUTPUT_A)
	@echo INSTALLING ${OUTPUT_A} and ${OUTPUT_LIB} to ${PREFIX}/lib
	install -d -m755 ${PREFIX}/lib
	install -d -m755 ${PREFIX}/include
	install -m644 ${OUTPUT_A} ${PREFIX}/lib
	install -m755 ${OUTPUT_LIB} ${PREFIX}/lib
	ln -sfn $(PREFIX)/lib/$(OUTPUT_LIB) $(PREFIX)/lib/$(NAME).so
	@echo INSTALLING headers to ${PREFIX}/include
	install -m644 oi.h ${PREFIX}/include 

uninstall:
	@echo REMOVING so from ${PREFIX}/lib
	rm -f ${PREFIX}/lib/${NAME}.*
	@echo REMOVING headers from ${PREFIX}/include
	rm -f ${PREFIX}/include/oi.h

.PHONY: all options clean clobber install uninstall test 
