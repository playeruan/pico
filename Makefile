dev:
	@$(CC) pico.c -o build/pico -Wall -Wextra -pedantic -std=c99
	@echo 'compiled pico'
install:
	@sudo $(CC) pico.c -o /bin/pico -Wall -Wextra -pedantic -std=c99
	@echo 'compiled pico in /bin/pico'
