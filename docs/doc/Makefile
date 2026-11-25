SPHINXBUILD ?= sphinx-build
BUILDDIR ?= _build

.PHONY: html en ru
html: en ru
	cp landing.html "$(BUILDDIR)/html/index.html"

en ru:
	$(SPHINXBUILD) -b html -W --keep-going -c . -D language=$@ -d "$(BUILDDIR)/doctrees/$@" $@ "$(BUILDDIR)/html/$@"
