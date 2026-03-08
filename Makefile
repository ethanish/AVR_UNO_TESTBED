PROJECTS ?= P0 P1 P2 P3 P4 P5 P6 P7 P8
PROJECT_DIR := Tutorials

.PHONY: help list all build test clean

help:
	@echo "Targets:"
	@echo "  make build   - build all projects"
	@echo "  make test    - test all projects"
	@echo "  make all     - build + test"
	@echo "  make clean   - clean all projects"
	@echo "  make list    - list project directories"
	@echo "  make build PROJECTS='P0 P3'"

list:
	@for p in $(PROJECTS); do \
		echo "$(PROJECT_DIR)/$$p"; \
	done

all: build test

build:
	@set -e; \
	for p in $(PROJECTS); do \
		dir="$(PROJECT_DIR)/$$p"; \
		if [ -f "$$dir/Makefile" ]; then \
			echo "==> BUILD $$dir"; \
			$(MAKE) -C "$$dir" build; \
		else \
			echo "==> SKIP  $$dir (no Makefile)"; \
		fi; \
	done

test:
	@set -e; \
	for p in $(PROJECTS); do \
		dir="$(PROJECT_DIR)/$$p"; \
		if [ -f "$$dir/Makefile" ]; then \
			echo "==> TEST  $$dir"; \
			$(MAKE) -C "$$dir" test; \
		else \
			echo "==> SKIP  $$dir (no Makefile)"; \
		fi; \
	done

clean:
	@set -e; \
	for p in $(PROJECTS); do \
		dir="$(PROJECT_DIR)/$$p"; \
		if [ -f "$$dir/Makefile" ]; then \
			echo "==> CLEAN $$dir"; \
			$(MAKE) -C "$$dir" clean; \
		else \
			echo "==> SKIP  $$dir (no Makefile)"; \
		fi; \
	done
