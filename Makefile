# Based on https://spin.atomicobject.com/2016/08/26/makefile-c-projects/
TARGET_EXEC ?= structlangc

BUILD_DIR ?= ./build
SRC_DIRS ?= ./src

SRCS := $(shell find $(SRC_DIRS) -name *.cpp -or -name *.c -or -name *.s)
GEN := lex.yy.c grammar.tab.c
OBJEX := $(GEN:%=$(BUILD_DIR)/src/%.o)
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o) $(OBJEX)
DEPS := $(OBJS:.o=.d)

INC_DIRS := $(shell find $(SRC_DIRS) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

YACC=bison
YFLAGS=-d -v
LEX=flex
LFLAGS=

CPPFLAGS ?= $(INC_FLAGS) -MMD -MP
CFLAGS = -std=gnu11 -g -Wall -fno-omit-frame-pointer

ifndef NDEBUG
  ifneq "$(OS)" "Windows_NT"
    CFLAGS += -fsanitize=address
    LDFLAGS += -fsanitize=address
  endif
endif

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)


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



.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)

-include $(DEPS)

MKDIR_P ?= mkdir -p

