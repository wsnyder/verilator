#*****************************************************************************
# DESCRIPTION: Verilator documentation: Makefile pre-configure version
#
# This file is part of Verilator.
#
# Code available from: https://verilator.org
#
# Copyright 2003-2025 by Wilson Snyder. This program is free software; you
# can redistribute it and/or modify it under the terms of either the GNU
# Lesser General Public License Version 3 or the Perl Artistic License
# Version 2.0.
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
#
######################################################################
#
# This file is intended only to be called from the top-level Verilator Makefile.

RST2HTML = rst2html
PYTHON3 = python3
DOXYGEN = doxygen
SPHINXBUILD ?= sphinx-build

SOURCEDIR = guide
BUILDDIR = _build

SPHINXOPTS ?= -c guide -j 4
ifneq ($(VERILATOR_ANALYTICS_ID),)
  SPHINXOPTS += -D html_theme_options.analytics_id=$(VERILATOR_ANALYTICS_ID)
endif

######################################################################

.SUFFIXES:

default:
	@echo "error: make not supported here, run 'make docs' from Verilator top-level"

######################################################################
# Intermediate rules

vl-extract: ../bin/verilator ../Changes
	ln -sf ../spelling.txt guide/spelling.txt
	mkdir -p _build/gen
	$(PYTHON3) bin/vl_sphinx_extract ../bin/verilator
	sed 's/`/\&96;/g' < ../Changes > _build/gen/Changes

######################################################################
# HTML

server: html
	cd _build/html && $(PYTHON3) -m http.server

%.html: %.rst
	$(RST2HTML) $< $@

######################################################################
# PDF

.PHONY: verilator.pdf

verilator.pdf: pdf
pdf:
	$(MAKE) vl-extract
	$(MAKE) latex
	$(PYTHON3) bin/vl_sphinx_fix _build
	$(MAKE) -C _build/latex
	cp _build/latex/verilator.pdf ..

######################################################################

html latex linkcheck::
	$(MAKE) vl-extract
	$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS)
	$(PYTHON3) bin/vl_sphinx_fix _build

spelling::
	$(MAKE) vl-extract
	$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS)
	sort -o guide/spelling.txt guide/spelling.txt

help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS)

clean mostlyclean distclean maintainer-clean::
	rm -rf _build
	rm -f $(SCRIPTS) *.tmp
	rm -f *.aux *.cp *.cps *.dvi *.fn *.fns *.ky *.kys *.log
	rm -f *.pg *.pgs *.toc *.tp *.tps *.vr *.vrs *.idx
	rm -f *.ev *.evs *.ov *.ovs *.cv *.cvs *.ma *.mas
	rm -f *.tex
	rm -f guide/spelling.txt

distclean maintainer-clean::
	rm -f *.info* *.1 *.html *.pdf $(INFOS)
