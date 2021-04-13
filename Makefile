CC = gcc
CFLAGS = -g -Wall -Werror -I.
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

bins = term

term_srcs = cli-command.c cli-term.c cli-tree.c event-loop.c main.c stream.c \
	    libregexp.c libunicode.c cutils.c
term_objs = $(term_srcs:.c=.o)

test_bins = t/str_kpair
tshare_srcs = t/test-runner.c t/test-helpers.c
t/str_kpair_srcs = $(tshare_srcs) t/t-str-kpairs.c str-kpairs.c
t/str_kpair_objs = $(t/str_kpair_srcs:.c=.o)

all : $(bins)

-include *.d
-include t/*.d

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
	$(V_CC)$(CC) -MM $< > $*.d

test : $(test_bins)
	@echo; echo Testing ...
	@for t in $(test_bins); do	\
		$$t;			\
	done
	@echo

$(foreach bin,$(test_bins),$(eval $(call bin_template,$(bin))))

.PHONY: clean

clean:
	$(RM) $(bins) $(test_bins) $(allobjs) *.d t/*.d
