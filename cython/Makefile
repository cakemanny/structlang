
CBUILD_DIR := ../build/release
LIBFILE = $(CBUILD_DIR)/libstructlang.a

all:
	test -f $(LIBFILE)
	venv/bin/pip install --upgrade pip
	venv/bin/pip install -r requirements.txt
	CBUILD_DIR=$(CBUILD_DIR) venv/bin/python setup.py build_ext -i

clean:
	rm -Rf build/ *.c *.so __pycache__/

.PHONY: all
