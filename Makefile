SRC	= src/main.c

OBJ	= $(SRC:.c=.o)

NAME	= packrat

RM	= rm -f

CC	= gcc

CFLAGS	= -I./include

LDFLAGS  = -pthread


all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o $(NAME)
	$(RM) $(OBJ)

clean:
	$(RM) $(OBJ)
	$(RM) $(NAME)

fclean: clean
	$(RM) $(NAME)

re: fclean all

.PHONY: all clean fclean re