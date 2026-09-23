# mega-net
#
# A convenience wrapper. The build itself lives in build.py, which is the
# supported entry point on all three development platforms -- on Windows,
# where make is not a given, run `python build.py test` directly.

PYTHON ?= python3

.PHONY: test m65 all clean

test:
	@$(PYTHON) build.py test

m65:
	@$(PYTHON) build.py m65

all:
	@$(PYTHON) build.py all

clean:
	@$(PYTHON) build.py clean
