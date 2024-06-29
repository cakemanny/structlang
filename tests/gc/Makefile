SRCS = leak1.sl leak2.sl
ASMS = $(SRCS:.sl=.s)
OBJS = $(ASMS:.s=.o)

TARGETS = leak1 leak2

SLC = ../../build/debug/structlangc
LDLIBS = ../../build/debug/libslruntime.a

.PHONY: all
all: $(TARGETS)
	./leak1
	./leak2

$(ASMS): $(SLC)
%.s: %.sl
	$(SLC) $< -o $@
#.INTERMEDIATE: $(ASMS)

$(TARGETS): $(LDLIBS)
%: %.o
	$(CC) $< $(LDLIBS) -o $@

%.o: %.s
	$(CC) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(ASMS) $(OBJS) $(TARGETS)
