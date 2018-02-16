CC=gcc
CFLAGS=-W -Wall -pedantic -g
LDFLAGS= -lpthread
EXEC=run.out
SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)

all: $(EXEC)

$(EXEC): $(OBJ)
	@$(CC) -o $@ $^ $(LDFLAGS)


%.o: %.c
	@$(CC) -o $@ -c $< $(CFLAGS)
				
clean:
	@rm -rf *.o

mrproper: clean
	@$(RM) $(EXEC) $(OBJ)
