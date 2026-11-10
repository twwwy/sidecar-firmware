# Original FM4; plugin-only builds. SPDX-License-Identifier: MIT
NAME := fm4
SRCS := fm4_dsp.c fm4_tables.c fm4_rmp.c
PY := python3
.DEFAULT_GOAL := native
include ../../sdk/rmp/plugin.mk
native: libfm4.dylib
TESTFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -I../../sdk -I. -DRMP_NATIVE
build/check: tests/check.c $(SRCS) fm4_dsp.h fm4_tables.h
	@mkdir -p build
	$(CC) $(TESTFLAGS) tests/check.c $(SRCS) -lm -o $@
check: build/check
	./build/check
	$(PY) tests/package.py
sanitize:
	@mkdir -p build
	$(CC) $(filter-out -O2,$(TESTFLAGS)) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/check.c $(SRCS) -lm -o build/check-sanitize
	./build/check-sanitize
render: native
	$(PY) tests/render.py
.PHONY: native check sanitize render
build/bench: tests/bench.c fm4_dsp.c fm4_tables.c fm4_dsp.h
	@mkdir -p build
	$(CC) $(TESTFLAGS) tests/bench.c fm4_dsp.c fm4_tables.c -lm -o $@
bench: build/bench
	./build/bench > build/timing.csv
	@cat build/timing.csv
references:
	@mkdir -p build
	$(CC) $(TESTFLAGS) -shared -fPIC -DFM4_REFERENCE_FACTOR=8 $(SRCS) -lm -o build/ref8.dylib
	$(CC) $(TESTFLAGS) -shared -fPIC -DFM4_REFERENCE_FACTOR=16 $(SRCS) -lm -o build/ref16.dylib
	$(CC) $(TESTFLAGS) -shared -fPIC -DFM4_REFERENCE_FACTOR=8 -DFM4_REFERENCE_DELAY=1 $(SRCS) -lm -o build/ref8-short-delay.dylib
.PHONY: bench references
build/filter.dylib: tests/filter.c
	@mkdir -p build
	$(CC) -std=c11 -O2 -shared -fPIC $< -lm -o $@
aliasing: native references build/filter.dylib
	$(PY) tests/aliasing.py
.PHONY: aliasing
RMPSRC := ../../teensy/libraries/RytmPlugins/src
build/package_run: tests/package_run.cpp
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I$(RMPSRC) $< $(RMPSRC)/rmp_loader.cpp $(RMPSRC)/rmp_manifest.cpp -o $@
package-check: build/package_run
	$(PY) tests/package.py
	./build/package_run fm4.a64.rmp
.PHONY: package-check
report:
	$(PY) tests/report.py
.PHONY: report
build/admission: tests/admission.cpp fm4_dsp.h ../../core/inst_alloc.cpp
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Werror -I. -I../../core -I../../core/host -I$(RMPSRC) tests/admission.cpp ../../core/inst_alloc.cpp -o $@
admission: build/admission
	./build/admission
.PHONY: admission
