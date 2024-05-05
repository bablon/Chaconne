CC = gcc
CFLAGS = -g -Wall -Werror -Wno-unused-function -I.
LDFLAGS =

ifneq ($(V),1)
	V=0
else
	V=1
endif

V_CC_0 = @echo "    CC     " $@;
V_CC_1 =
V_CC = $(V_CC_$(V))

V_LN_0 = @echo "    LINK   " $@;
V_LN_1 =
V_LN = $(V_LN_$(V))

V_GEN_0 = @echo "    GEN    " $@;
V_GEN_1 =
V_GEN = $(V_GEN_$(V))

has_lex_yacc = 1
lex_path = $(shell which lex)
yacc_path = $(shell which yacc)
ifeq ($(lex_path),)
	has_lex_yacc =
endif
ifeq ($(yacc_path),)
	has_lex_yacc =
endif

bins = chaconne

genfiles = cpuid_desc.c
ifneq ($(has_lex_yacc),)
genfiles += calc_l.c
genfiles += calc_y.c
genfiles += calc_y.h
endif

chaconne_srcs = cli-command.c
chaconne_srcs += cli-term.c
chaconne_srcs += cli-tree.c
chaconne_srcs += event-loop.c
chaconne_srcs += main.c
chaconne_srcs += stream.c
chaconne_srcs += libregexp.c
chaconne_srcs += libunicode.c
chaconne_srcs += cutils.c
chaconne_srcs += hashtable.c
chaconne_srcs += vector.c
chaconne_srcs += heap.c
chaconne_srcs += test.c
chaconne_srcs += range.c
chaconne_srcs += cpuid.c
chaconne_srcs += cpuid_info.c
chaconne_srcs += cpuid_desc.c
chaconne_srcs += mdio.c

ifneq ($(has_lex_yacc),)
chaconne_srcs += calc_y.c
chaconne_srcs += calc_l.c
chaconne_srcs += calc_cmd.c
endif
chaconne_objs = $(chaconne_srcs:.c=.o)

test_bins = t/str_kpair
tshare_srcs = t/test-runner.c t/test-helpers.c
t/str_kpair_srcs = $(tshare_srcs) t/t-str-kpairs.c str-kpairs.c
t/str_kpair_objs = $(t/str_kpair_srcs:.c=.o)

all : $(bins)

-include *.d
-include t/*.d

cpuid_desc.c : cpuid.txt
	@cp $< .cpuid.desc
	$(V_GEN)xxd -i .cpuid.desc > $@
	@rm .cpuid.desc

calc_l.c : calc.l calc_y.h
	$(V_GEN)lex -o $@ $<

calc_y.h : calc_y.c

calc_y.c : calc.y
	$(V_GEN)yacc -o $@ -d $<

define bin_template
allobjs += $($(1)_objs)
ifeq ($($(1)_objs),)
$(1) : $(1).o
	$$(V_LN)$$(CC) -o $$@ $$< $$(LDFLAGS)
else
$(1) : $($(1)_objs)
	$$(V_LN)$$(CC) -o $$@ $$^ $$(LDFLAGS)
endif
endef

$(foreach bin,$(bins),$(eval $(call bin_template,$(bin))))

%.o : %.c
	$(V_CC)$(CC) $(CFLAGS) -c -o $@ $<
	@$(CC) -MM $< > $*.d

test : $(test_bins)
	@echo; echo Testing ...
	@for t in $(test_bins); do	\
		$$t;			\
	done
	@echo

$(foreach bin,$(test_bins),$(eval $(call bin_template,$(bin))))

.PHONY: clean

clean:
	$(RM) $(genfiles) $(bins) $(test_bins) $(allobjs) *.d t/*.d
