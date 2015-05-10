# Copyright © 2004-2014 Jakub Wilk <jwilk@jwilk.net>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

CC = gcc -std=gnu99
CFLAGS = -g -O2 -Wall -fcilkplus
CPPFLAGS = 
LDFLAGS = 
LDLIBS =  -lncurses   -lm

CFILES = $(wildcard *.c)
OFILES = $(CFILES:.c=.o)

.PHONY: all
all: nonogram

include Makefile.dep

$(OFILES): %.o: %.c

nonogram: $(OFILES)
	$(LINK.c) $(^) $(LOADLIBES) $(LDLIBS) -o $(@)

.PHONY: test
test: nonogram
	./nonogram < test-input

.PHONY: clean
clean:
	rm -f *.o nonogram doc/*.1

.PHONY: distclean
distclean: clean
	rm -f config.log config.status
	find . -name '*.in' | sed -e 's/[.]in$$//' | xargs -t rm -f

# vim:ts=4 sts=4 sw=4 noet
