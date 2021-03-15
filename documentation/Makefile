#------------------------------------------------------------------------------
#
#  LaTeX Makefile
#
#  $Id$
#
#------------------------------------------------------------------------------

FILE := ethercat_doc

LATEX_OPTIONS := -file-line-error-style

COMMANDS := \
	alias \
	config \
	cstruct \
	data \
	debug \
	domains \
	download \
	eoe \
	foe_read \
	foe_write \
	graph \
	ip \
	master \
	pdos \
	reg_read \
	reg_write \
	sdos \
	sii_read \
	sii_write \
	slaves \
	soe_read \
	soe_write \
	states \
	upload \
	version \
	xml

EXT_PREFIX := external/ethercat_
EXT_FILES := $(addprefix $(EXT_PREFIX),$(COMMANDS))
ETHERCAT_CMD := ../tool/ethercat
ETHERCAT_HELP := $(ETHERCAT_CMD) -h

#------------------------------------------------------------------------------

all: pdf

$(EXT_FILES): $(ETHERCAT_CMD)
	@mkdir -pv external
	@echo Creating $@...
	$(shell $(subst $(EXT_PREFIX),$(ETHERCAT_HELP) ,$@) > $@)

pdf: $(EXT_FILES)
	$(MAKE) -C images
	$(MAKE) -C graphs

# compile twice to update the table of contents
	pdflatex $(LATEX_OPTIONS) $(FILE)
	pdflatex $(LATEX_OPTIONS) $(FILE)

	pdflatex $(LATEX_OPTIONS) $(FILE)_fr
	pdflatex $(LATEX_OPTIONS) $(FILE)_fr

index:
	makeindex $(FILE)
	makeindex $(FILE).nlo -s nomencl.ist -o $(FILE).nls

	makeindex $(FILE)_fr
	makeindex $(FILE)_fr.nlo -s nomencl.ist -o $(FILE)_fr.nls

clean:
	@rm -f \
		*.aux \
		*.dvi \
		*.idx \
		*.ilg \
		*.ind \
		*.lof \
		*.log \
		*.lol \
		*.lot \
		*.nlo \
		*.nls \
		*.out \
		*.pdf \
		*.toc \
		*~ \
		images/*.bak

#------------------------------------------------------------------------------
