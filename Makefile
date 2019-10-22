CFLAGS=-std=gnu99 -Wall -Wextra -g
#CFLAGS=-std=gnu99 -Wall -Wextra -g -DNDEBUG
UNAME_S := $(shell uname -s)
test_mmal: n_mmal.o test_mmal.o
	gcc -o $@ $^
test: test_mmal
ifeq ($(UNAME_S),Linux)
		setarch `uname -m` -R ./test_mmal
else
		./test_mmal
endif
n_mmal.o: n_mmal.c mmal.h
test_mmal.o: test_mmal.c mmal.h
clean:
	-rm n_mmal.o test_mmal.o test_mmal
