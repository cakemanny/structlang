
SRCS = lots_of_ptr_spills.sl \
	   lots_of_spills.sl \
	   some_locals.sl
ASMS = $(SRCS:.sl=.arm64.s)
OBJS = $(ASMS:.s=.o)

OUTS = $(ASMS:.s=.out)

SLC = ../../build/debug/structlangc
LDLIBS = ../../build/debug/libslruntime.a

.PHONY: all
all: $(OUTS)

$(ASMS): $(SLC)
%.arm64.s: %.sl
	$(SLC) --target=arm64 $< -o $@
.INTERMEDIATE: $(ASMS)

%.out: %.s
	sed -n '/__DATA/,$$p' $< > $@

%.o: %.s
	$(CC) -c $< -o $@

.PHONY: test
test:
	rm -f *.out
	$(MAKE) all
	git diff --exit-code -- *.out

.PHONY: clean
clean:
	rm -f $(ASMS) $(OBJS)

.PHONY: maintainer-clean
maintainer-clean: clean
	rm -f $(OUTS)
