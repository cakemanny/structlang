SRCS = leak1.sl
ASMS = $(SRCS:.sl=.s)
OBJS = $(ASMS:.s=.o)

TARGETS = leak1

SLC = ../../build/debug/structlangc
LDLIBS = ../../build/debug/libslruntime.a

.PHONY: all
all: $(TARGETS)
	./leak1

$(ASMS): $(SLC)
%.s: %.sl
	$(SLC) --target=arm64 $< -o $@
#.INTERMEDIATE: $(ASMS)

$(TARGETS): $(LDLIBS)
%: %.o
	$(CC) $< $(LDLIBS) -o $@

%.o: %.s
	$(CC) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(ASMS) $(OBJS) $(TARGETS)
