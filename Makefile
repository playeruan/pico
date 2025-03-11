dev:
	@$(CC) pico.c -o pico -Wall -Wextra -pedantic -std=c99
	@echo 'compiled pico'
public:
	@$(CC) pico.c -o pico -Wall -Wextra -pedantic -std=c99
	sudo cp pico /bin/pico
	@echo 'compiled pico and copied to /bin/pico'
