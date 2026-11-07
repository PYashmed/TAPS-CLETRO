# INET_ROOT is relative to this project root unless an absolute path is supplied.
INET_ROOT ?= ../inet4.6
INET_ABS := $(abspath $(INET_ROOT))
.PHONY: all makefiles clean
all:
	$(MAKE) -C src INET4_6_PROJ="$(INET_ABS)"
makefiles:
	cd src && opp_makemake --make-so -f --deep -o satnet3d -KINET4_6_PROJ=../../inet4.6 -DINET_IMPORT '-I$$(INET4_6_PROJ)/src' '-L$$(INET4_6_PROJ)/src' '-lINET$$(D)'
clean:
	$(MAKE) -C src clean INET4_6_PROJ="$(INET_ABS)"
