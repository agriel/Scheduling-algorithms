SCHEDULER=hw6i1.c
BINARIES=hw6
CCOPT=-g --std=gnu99
LIBS=-lpthread

all: $(BINARIES)

hw6: main.c hw6.h $(SCHEDULER)
	gcc $(CCOPT) main.c $(SCHEDULER) -o hw6 $(LIBS)


highload: clean $(SCHEDULER)
	@gcc -DDURATION=30 -DDELAY=3000 -DLOG_LEVEL=9 -DPASSENGERS=50 -DFLOORS=27 -DELEVATORS=2 -g --std=c99 main.c $(SCHEDULER) -o hw6 $(LIBS) $(CCOPT)
	@./hw6 2> /tmp/logs.tmp

mediumload: clean $(SCHEDULER)
	@gcc -DDURATION=30 -DDELAY=3000 -DLOG_LEVEL=9 -DPASSENGERS=50 -DFLOORS=27 -DELEVATORS=4 -g --std=c99 main.c $(SCHEDULER) -o hw6 $(LIBS) $(CCOPT)
	@./hw6 2> /tmp/logs.tmp

lowload: clean $(SCHEDULER)
	@gcc -DDURATION=30 -DDELAY=3000 -DLOG_LEVEL=9 -DPASSENGERS=50 -DFLOORS=27 -DELEVATORS=10 -g --std=c99 main.c $(SCHEDULER) -o hw6 $(LIBS) $(CCOPT)
	@./hw6 2> /tmp/logs.tmp

clean:
	@rm -rf *~ *.dSYM $(BINARIES)

