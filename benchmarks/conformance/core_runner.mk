OBJECTS += bench_runner.o
$(TARGET): bench_runner.o
bench_runner.o: bench_runner.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCONFORMANCE_STATIC_CORE -c $< -o $@
override LDFLAGS := $(filter-out -shared -Wl%,$(LDFLAGS)) -static
