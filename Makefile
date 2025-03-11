micro_dev:
	@$(CC) micro.c -o micro_dev -Wall -Wextra -pedantic -std=c99
	@echo 'compiled micro_dev'
public:
	@$(CC) micro.c -o micro_dev -Wall -Wextra -pedantic -std=c99
	sudo cp micro_dev /bin/micro
	@echo 'compiled micro_dev and copied to /bin/micro'
