CC       ?= gcc
AR       ?= ar
NM       ?= nm

CPPFLAGS += -D_GNU_SOURCE -Iinclude
CFLAGS   ?= -O2 -g
WARNINGS  = -std=c11 -Wall -Wextra -Wpedantic -Werror -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes
NO_ALLOC_BUILTINS = -fno-builtin-malloc -fno-builtin-free \
                    -fno-builtin-calloc -fno-builtin-realloc
LDLIBS   += -pthread

BUILD_DIR = build
LIB       = $(BUILD_DIR)/libmemctl.a
ALLOC_OBJ = $(BUILD_DIR)/allocator.o
TEST_BIN  = $(BUILD_DIR)/test_runner

.PHONY: all test check check-symbols clean

all: $(LIB) $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ALLOC_OBJ): src/allocator.c include/libmemctl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(NO_ALLOC_BUILTINS) -c $< -o $@

$(LIB): $(ALLOC_OBJ)
	$(AR) rcs $@ $^

$(TEST_BIN): tests/test_runner.c include/libmemctl.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(LIB) $(LDLIBS) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

check-symbols: $(ALLOC_OBJ)
	@bad="$$( $(NM) -u $(ALLOC_OBJ) | sed 's/.* U //' | sed 's/@.*//' | \
		grep -E '^(malloc|free|calloc|realloc|aligned_alloc|posix_memalign|memalign|valloc|pvalloc|strdup|sbrk)$$' || true )"; \
	if [ -n "$$bad" ]; then \
		echo "Forbidden allocator symbol(s) referenced by allocator.o:"; \
		echo "$$bad"; exit 1; \
	else \
		echo "Allocator symbol check passed."; \
	fi

check: check-symbols test size-classes

$(BUILD_DIR)/test_size_classes: tests/test_size_classes.c src/allocator.c include/libmemctl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(NO_ALLOC_BUILTINS) $< $(LDLIBS) -o $@

.PHONY: size-classes
size-classes: $(BUILD_DIR)/test_size_classes
	./$(BUILD_DIR)/test_size_classes

clean:
	rm -rf $(BUILD_DIR)


EXT_BIN = $(BUILD_DIR)/test_extended
WRAPS = -Wl,--wrap=mmap -Wl,--wrap=munmap -Wl,--wrap=mremap
$(EXT_BIN): tests/test_extended.c tests/vm_probe.c tests/vm_probe.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) tests/test_extended.c tests/vm_probe.c $(LIB) $(LDLIBS) $(WRAPS) -o $@

.PHONY: extended sanitize thread-sanitize
extended: $(EXT_BIN)
	./$(EXT_BIN)

STRESS_BIN = $(BUILD_DIR)/test_stress
$(STRESS_BIN): tests/test_stress.c tests/vm_probe.c tests/vm_probe.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) tests/test_stress.c tests/vm_probe.c $(LIB) $(LDLIBS) $(WRAPS) -o $@

.PHONY: stress
stress: $(STRESS_BIN)
	./$(STRESS_BIN)

sanitize:
	$(MAKE) BUILD_DIR=build-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' check extended stress

thread-sanitize:
	$(MAKE) BUILD_DIR=build-tsan CFLAGS='-O1 -g -fsanitize=thread -fno-omit-frame-pointer' check extended stress

$(BUILD_DIR)/bench_candidate: bench/throughput.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(LIB) $(LDLIBS) -o $@
$(BUILD_DIR)/bench_reference: bench/throughput.c reference/allocator_baseline.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $^ $(LDLIBS) -o $@
$(BUILD_DIR)/bench_libc: bench/throughput.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -DUSE_LIBC $< $(LDLIBS) -o $@
.PHONY: bench-build
bench-build: $(BUILD_DIR)/bench_candidate $(BUILD_DIR)/bench_reference $(BUILD_DIR)/bench_libc

$(BUILD_DIR)/space_candidate: bench/space.c tests/vm_probe.c $(LIB)
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) $(WARNINGS) bench/space.c tests/vm_probe.c $(LIB) $(LDLIBS) $(WRAPS) -o $@
$(BUILD_DIR)/space_reference: bench/space.c tests/vm_probe.c reference/allocator_baseline.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) $(WARNINGS) $^ $(LDLIBS) $(WRAPS) -o $@
.PHONY: space-build
space-build: $(BUILD_DIR)/space_candidate $(BUILD_DIR)/space_reference

$(BUILD_DIR)/demo: src/demo.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(LIB) $(LDLIBS) -o $@
.PHONY: demo
demo: $(BUILD_DIR)/demo
	./$(BUILD_DIR)/demo
