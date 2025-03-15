# pico 

pico is a minimal text editor for linux systems.

This is a project I made for fun to learn more about interacting with the terminal in C following the guide at https://viewsourcecode.org/snaptoken/kilo.

## Keybinds

### global
- CTRL-S -> save
- CTRL-Q -> quit
- CTRL-0 -> go to start of line
- CTRL-D -> delete line
- ESC    -> switch to **normal mode**

### normal mode
- h    -> move cursor left
- j    -> move cursor down
- k    -> move cursor up
- l    -> move cursor right
- g    -> go to line
- 0    -> go to start of line
- G    -> go to end of file
- s, / -> search
- i    -> switch to **insert mode**
- a    -> switch to **insert mode** to the right of the cursor
- A    -> go to end of line and switch to **insert mode**
- ;    -> go to end of line, insert ";" if not present and switch to **insert mode**
- o    -> insert new line below and switch to **insert mode**
- O    -> insert new line above and switch to **insert mode**
