CXX ?= g++

TARGET_EXEC ?= blescan

BUILD_DIR ?= ./build
SRC_DIR ?= ./src

REL_DIR ?= bin/release
DBG_DIR ?= bin/debug

SRCS := $(shell find $(SRC_DIR) -name *.cpp)
OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS:.cpp=.o)))
DEPS := $(OBJS:.o=.d)

INC_DIRS := $(shell find $(SRC_DIR) -type d) ../yaml-cpp/include
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

LIBS = -lbluetooth -pthread -lcurl-gnutls -lyaml-cpp

-include $(DEPS)

MKDIR_P ?= mkdir -p
RM ?= rm

CPPFLAGS ?= $(INC_FLAGS) -c -MMD -MP -std=c++2a
LDFLAGS = -L/usr/lib/arm-linux-gnueabihf -L../yaml-cpp

OUTPUT_DIR = $(REL_DIR)
OUTPUT = $(REL_DIR)/$(TARGET_EXEC)

### for debug output
#CPPFLAGS += -Wall -W -DDEBUG_BUILD -g

### for system-journald
CPPFLAGS += -Wall -W -Wno-psabi -DJOURNAL -g


$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	-mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) -o $@ $<

.PHONY: build
build: $(OBJS)
	-mkdir -p $(OUTPUT_DIR)
	$(CXX) $(LDFLAGS) -o $(OUTPUT) $(OBJS) $(LIBS)

.PHONY: all
all: clean build

.PHONY: clean
clean:
	$(RM) $(BUILD_DIR)/*
	$(RM) $(DBG_DIR)/$(TARGET_EXEC)
	$(RM) $(REL_DIR)/$(TARGET_EXEC)
