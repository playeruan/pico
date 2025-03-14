here:
	@$(CC) pico.c -o build/pico -Wall -Wextra -pedantic -std=c99
	@echo built pico in build/pico

install:	
	@$(CC) pico.c -o /usr/bin/pico -Wall -Wextra -pedantic -std=c99
	@echo built pico in /usr/bin/pico
