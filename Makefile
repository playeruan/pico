dev:
	@$(CC) pico.c -o build/pico -Wall -Wextra -pedantic -std=c99
	@echo 'compiled pico'
install:
	@$(CC) pico.c -o build/pico -Wall -Wextra -pedantic -std=c99
	sudo cp build/pico /bin/pico
	@echo 'compiled pico and copied to /bin/pico'
