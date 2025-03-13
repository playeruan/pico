here:
	@$(CC) pico.c -o build/pico -Wall -Wextra -pedantic -std=c99
	@echo built pico in build/pico

install:	
	@sudo $(CC) pico.c -o $(HOME)/bin/pico -Wall -Wextra -pedantic -std=c99
	@echo built pico in $(HOME)/bin/pico
