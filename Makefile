PYTHON ?= python3

.PHONY: all test cxx clean

all:
	$(PYTHON) build.py

test:
	$(PYTHON) build.py test

cxx:
	$(PYTHON) build.py cxx

clean:
	$(PYTHON) build.py clean
