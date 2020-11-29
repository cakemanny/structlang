# Based on https://spin.atomicobject.com/2016/08/26/makefile-c-projects/
TARGET_EXEC ?= structlangc
TARGET_LIB ?= libstructlang.a

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
MAINS := $(BUILD_DIR)/src/main.o

INC_DIRS := $(shell find $(SRC_DIRS) -type d)
INC_DIRS += $(SRC_DIRS:%=$(BUILD_DIR)/%)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

YACC=bison
YFLAGS=-d -v
LEX=flex
LFLAGS=

CPPFLAGS ?= $(INC_FLAGS) -MMD -MP
CFLAGS = -std=gnu11 -g -Wall -Werror -fno-omit-frame-pointer
LDFLAGS = -L$(BUILD_DIR) -lgraph

ifndef NDEBUG
  ifneq "$(OS)" "Windows_NT"
    CFLAGS += -fsanitize=address
    LDFLAGS += -fsanitize=address
  endif
else
  CFLAGS += -O3
endif

RUSTC=rustc

all: gen lib $(BUILD_DIR)/$(TARGET_EXEC)

lib: gen $(BUILD_DIR)/$(TARGET_LIB)

gen: $(BUILD_DIR)/src/grammar.tab.h

$(BUILD_DIR)/$(TARGET_LIB): $(filter-out $(MAINS),$(OBJS))
	$(AR) rcs $@ $^

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS) $(BUILD_DIR)/libgraph.a
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/libgraph.a: src/graph.rs
	$(RUSTC) --crate-type staticlib --out-dir $(BUILD_DIR) $<

$(BUILD_DIR)/src/lex.yy.c: src/lexer.l
	$(MKDIR_P) $(dir $@)
	$(LEX) $(LFLAGS) -o $@ $<

$(BUILD_DIR)/src/lex.yy.c.o: $(BUILD_DIR)/src/lex.yy.c $(BUILD_DIR)/src/grammar.tab.h
	$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/src/grammar.tab.c $(BUILD_DIR)/src/grammar.tab.h: src/grammar.y
	$(MKDIR_P) $(dir $@)
	$(YACC) $(YFLAGS) -b $(dir $@)/grammar $<

$(BUILD_DIR)/src/grammar.tab.c.o: $(BUILD_DIR)/src/grammar.tab.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# assembly
$(BUILD_DIR)/%.s.o: %.s
	$(MKDIR_P) $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# c source
$(BUILD_DIR)/%.c.o: %.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# c++ source
$(BUILD_DIR)/%.cpp.o: %.cpp
	$(MKDIR_P) $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@



.PHONY: clean all lib gen

clean:
	$(RM) -r $(BUILD_DIR)

-include $(DEPS)

MKDIR_P ?= mkdir -p

