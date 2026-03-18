CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -std=c11 -O2
CPPFLAGS ?= -Iinclude
LDFLAGS ?=
LDLIBS ?= -lm -ldl
EXPORT_LDFLAGS ?= -rdynamic

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj

LIB_OBJS = \
	$(OBJ_DIR)/common.o \
	$(OBJ_DIR)/parse.o \
	$(OBJ_DIR)/link.o \
	$(OBJ_DIR)/runtime_api.o \
	$(OBJ_DIR)/runtime.o
TOOLS = $(BUILD_DIR)/maestroc $(BUILD_DIR)/maestrovm
TOOLS += $(BUILD_DIR)/maestroexts
TESTS = $(BUILD_DIR)/smoke $(BUILD_DIR)/hostrun
DEEP_TESTS = $(BUILD_DIR)/dllapi
DLL_FIXTURES := \
	$(BUILD_DIR)/tests/dll/plugin_ok.so \
	$(BUILD_DIR)/tests/dll/plugin_duplicate.so \
	$(BUILD_DIR)/tests/dll/plugin_fail.so \
	$(BUILD_DIR)/tests/dll/plugin_missing.so
DLL_ARTIFACT := $(BUILD_DIR)/tests/dllapi.mstro
DLL_ARTIFACT_SRCS := \
	tests/mstr/dll/int.mstr \
	tests/mstr/dll/float.mstr \
	tests/mstr/dll/bool.mstr \
	tests/mstr/dll/string.mstr \
	tests/mstr/dll/symbol.mstr \
	tests/mstr/dll/list.mstr \
	tests/mstr/dll/object-probe.mstr \
	tests/mstr/dll/describe.mstr
EXAMPLE_CATS := basics external json modules refs state
EXAMPLE_ARTIFACTS := $(patsubst %,$(BUILD_DIR)/examples/%.mstro,$(EXAMPLE_CATS))

.PHONY: all clean test runtime tools examples test-mstr test-deep

all: runtime tools $(TESTS)

runtime: $(BUILD_DIR)/libmaestro.a $(BUILD_DIR)/maestrovm

tools: $(BUILD_DIR)/maestroc $(BUILD_DIR)/maestroexts

examples: tools $(EXAMPLE_ARTIFACTS)

$(BUILD_DIR)/libmaestro.a: $(LIB_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(AR) rcs $@ $(LIB_OBJS)

$(OBJ_DIR)/common.o: src/common.c src/maestro_int.h include/maestro/maestro.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/parse.o: src/parse.c src/maestro_int.h include/maestro/maestro.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/link.o: src/link.c src/maestro_int.h include/maestro/maestro.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/runtime_api.o: src/runtime_api.c src/maestro_int.h include/maestro/maestro.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/runtime.o: src/runtime.c src/maestro_int.h include/maestro/maestro.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/maestroc.o: tools/maestroc.c include/maestro/maestro.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/maestrovm.o: tools/maestrovm.c include/maestro/maestro.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/maestroexts.o: tools/maestroexts.c include/maestro/maestro.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/smoke.o: tests/smoke.c include/maestro/maestro.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/hostrun.o: tests/hostrun.c include/maestro/maestro.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/dllapi.o: tests/dllapi.c include/maestro/maestro.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/maestroc: $(OBJ_DIR)/maestroc.o $(BUILD_DIR)/libmaestro.a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJ_DIR)/maestroc.o $(BUILD_DIR)/libmaestro.a $(LDLIBS)

$(BUILD_DIR)/maestrovm: $(OBJ_DIR)/maestrovm.o $(BUILD_DIR)/libmaestro.a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) $(EXPORT_LDFLAGS) -o $@ $(OBJ_DIR)/maestrovm.o $(BUILD_DIR)/libmaestro.a $(LDLIBS)

$(BUILD_DIR)/maestroexts: $(OBJ_DIR)/maestroexts.o $(BUILD_DIR)/libmaestro.a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJ_DIR)/maestroexts.o $(BUILD_DIR)/libmaestro.a $(LDLIBS)

$(BUILD_DIR)/smoke: $(OBJ_DIR)/smoke.o $(BUILD_DIR)/libmaestro.a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJ_DIR)/smoke.o $(BUILD_DIR)/libmaestro.a $(LDLIBS)

$(BUILD_DIR)/hostrun: $(OBJ_DIR)/hostrun.o $(BUILD_DIR)/libmaestro.a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJ_DIR)/hostrun.o $(BUILD_DIR)/libmaestro.a $(LDLIBS)

$(BUILD_DIR)/dllapi: $(OBJ_DIR)/dllapi.o $(BUILD_DIR)/libmaestro.a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) $(EXPORT_LDFLAGS) -o $@ $(OBJ_DIR)/dllapi.o $(BUILD_DIR)/libmaestro.a $(LDLIBS)

$(BUILD_DIR)/tests/dll/%.so: tests/dll/%.c include/maestro/maestro.h
	@mkdir -p $(BUILD_DIR)/tests/dll
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -o $@ $<

$(BUILD_DIR)/tests/dllapi.mstro: Makefile $(BUILD_DIR)/maestroc $(DLL_ARTIFACT_SRCS)
	@mkdir -p $(BUILD_DIR)/tests
	./$(BUILD_DIR)/maestroc -f $(DLL_ARTIFACT_SRCS) -o $@

$(BUILD_DIR)/examples/%.mstro: $(BUILD_DIR)/maestroc
	@mkdir -p $(BUILD_DIR)/examples
	./$(BUILD_DIR)/maestroc -d examples/$* -o $@

test: all
	./$(BUILD_DIR)/smoke

test-mstr: tools $(BUILD_DIR)/hostrun
	python3 tests/run_tests.py

test-deep: tools $(BUILD_DIR)/hostrun $(DEEP_TESTS) $(DLL_FIXTURES) $(DLL_ARTIFACT)
	python3 tests/run_tests.py --deep
	./$(BUILD_DIR)/dllapi $(BUILD_DIR)/tests/dllapi.mstro $(BUILD_DIR)/tests/dll/plugin_ok.so $(BUILD_DIR)/tests/dll/plugin_duplicate.so $(BUILD_DIR)/tests/dll/plugin_fail.so $(BUILD_DIR)/tests/dll/plugin_missing.so
	python3 tests/test_maestrovm.py

clean:
	rm -rf $(BUILD_DIR)
