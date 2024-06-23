# Based on https://spin.atomicobject.com/2016/08/26/makefile-c-projects/
TARGET_EXEC ?= structlangc
TEST_EXEC ?= structlangc.test
TARGET_LIB ?= libstructlang.a
RUNTIME_LIB ?= libslruntime.a

BUILD_DIR ?= ./build
SRC_DIRS ?= ./src

# don't mix release and debug objs
ifndef NDEBUG
  BUILD_DIR := $(BUILD_DIR)/debug
else
  BUILD_DIR := $(BUILD_DIR)/release
endif

SRCS := $(shell find $(SRC_DIRS) -name *.cpp -or -name *.c -or -name *.s)
GEN := lex.yy.c grammar.tab.c
OBJEX := $(GEN:%=$(BUILD_DIR)/src/%.o)
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o) $(OBJEX)
DEPS := $(OBJS:.o=.d)
COMPILE_DB_PARTS := $(addsuffix .json,$(OBJS))

MAINS := $(BUILD_DIR)/./src/main.c.o
LIB_OBJS := $(filter-out $(MAINS),$(OBJS))


INC_DIRS := $(shell find $(SRC_DIRS) -type d)
INC_DIRS += $(SRC_DIRS:%=$(BUILD_DIR)/%)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

YACC=bison
YFLAGS=-d -v
LEX=flex
LFLAGS=

CPPFLAGS ?= $(INC_FLAGS) -MMD -MP
CFLAGS = -std=gnu11 -g -Wall -Wvla -Werror -fno-omit-frame-pointer
LDFLAGS =

RTCFLAGS = -std=gnu11 -g -Wall -Werror -fno-omit-frame-pointer -O1

ifdef PROFILE
  CFLAGS += -O1 -fprofile-instr-generate -fcoverage-mapping
  LDFLAGS += -fprofile-instr-generate -fcoverage-mapping
else ifndef NDEBUG
  ifeq "$(OS)" "Windows_NT"
  else ifdef NASAN
  else
    CFLAGS += -fsanitize=address
    LDFLAGS += -fsanitize=address
  endif
else
  CFLAGS += -O3 -DNDEBUG
endif

.PHONY: all lib gen runtime
all: gen lib \
	$(BUILD_DIR)/$(TARGET_EXEC) \
	$(BUILD_DIR)/$(TEST_EXEC) \
	$(BUILD_DIR)/compile_commands.json runtime

lib: gen $(BUILD_DIR)/$(TARGET_LIB)

gen: $(BUILD_DIR)/src/grammar.tab.h

runtime: $(BUILD_DIR)/$(RUNTIME_LIB)

$(BUILD_DIR)/$(TARGET_LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(BUILD_DIR)/$(TARGET_EXEC) \
$(BUILD_DIR)/$(TEST_EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/$(RUNTIME_LIB): runtime/runtime.c
	$(MKDIR_P) $(BUILD_DIR)/runtime
	$(CC) -g -Wall -Wvla -Werror -fno-omit-frame-pointer -c $< -o $(BUILD_DIR)/runtime/runtime.o
	$(AR) rcs $@ $(BUILD_DIR)/runtime/runtime.o

$(BUILD_DIR)/src/lex.yy.c: src/lexer.l
	$(MKDIR_P) $(dir $@)
	$(LEX) $(LFLAGS) -o $@ $<

$(BUILD_DIR)/src/lex.yy.c.o: $(BUILD_DIR)/src/lex.yy.c $(BUILD_DIR)/src/grammar.tab.h
	$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) -MJ $(@).json $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/src/grammar.tab.c $(BUILD_DIR)/src/grammar.tab.h: src/grammar.y
	$(MKDIR_P) $(dir $@)
	$(YACC) $(YFLAGS) -b $(dir $@)/grammar $<

$(BUILD_DIR)/src/grammar.tab.c.o: $(BUILD_DIR)/src/grammar.tab.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) -MJ $(@).json $(CFLAGS) -c $< -o $@

# assembly
$(BUILD_DIR)/%.s.o: %.s
	$(MKDIR_P) $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# c source
$(BUILD_DIR)/%.c.o: %.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) -MJ $(@).json $(CFLAGS) -c $< -o $@

# c++ source
$(BUILD_DIR)/%.cpp.o: %.cpp
	$(MKDIR_P) $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/compile_commands.json: $(OBJS) $(COMPILE_DB_PARTS)
	sed -e '1s/^/[\'$$'\n''/' -e '$$s/,$$/\'$$'\n'']/' $(COMPILE_DB_PARTS) > $@

.PHONY: clean
clean:
	$(RM) -r $(BUILD_DIR)

ifneq "$(MAKECMDGOALS)" "clean"
  -include $(DEPS)
endif

MKDIR_P ?= mkdir -p

.PHONY: check
check: gen $(BUILD_DIR)/$(TEST_EXEC)
	$(BUILD_DIR)/$(TEST_EXEC)

.PHONY: test
test: check
	./tests/parsing
	./tests/semantics
	./tests/activation
	./tests/codegen
	$(MAKE) -C ./tests/stackmaps test
