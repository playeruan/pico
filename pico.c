/*
* https://viewsourcecode.org/snaptoken/kilo
*/

/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

/*** defines ***/

#define CLEAR_SCREEN_STRING "\x1b[2J"
#define CLEAR_LINE_STRING "\x1b[K"
#define RESET_MOUSE_POS_STRING "\x1b[H"

#define RESET_ESCAPE "\x1b[m"
#define BOLD_ESCAPE "\x1b[1m"
#define UNDERLINE_ESCAPE "\x1b[4m"
#define INVERT_ESCAPE "\x1b[7m"

#define PICO_VERSION "1.2.0"

#define CTRL_KEY(k) ((k) & 0x1f)
#define MIN(a, b) a < b ? a : b
#define MAX(a, b) a > b ? a : b

#define SCROLL_PADDING 4
#define TAB_STOP 2

#define QUIT_TIMES 3

typedef char bool;

/*** data ***/

typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
  unsigned char *hl;
} erow;

struct editorConfig {
  int cx, cy;
  int rx; // x counting multi-column characters
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  unsigned int dirty;
  char linestart;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termios;
};

struct editorConfig config;

enum EditorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  KEY_PAGE_UP,
  KEY_PAGE_DOWN,
  KEY_HOME,
  KEY_END,
  KEY_DEL,
};

enum EditorHighlight {
  HL_NORMAL = 0,
  HL_NUMBER,
  HL_BRACE,
  HL_STAR,
  HL_STRING,
  HL_MATCH,
};

/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, size_t maxlen, void (*callback)(char*, int));
char getCloseBrace(char c);

/*** terminal ***/

void die(const char *s){
  write(STDOUT_FILENO, CLEAR_SCREEN_STRING, 4);
  write(STDOUT_FILENO, RESET_MOUSE_POS_STRING, 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {

  if (tcgetattr(STDIN_FILENO, &config.orig_termios) == -1)
    die("tcgetattr");

  atexit(disableRawMode);

  struct termios raw = config.orig_termios;

  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_iflag &= ~(IXON | BRKINT | BRKINT | ISTRIP | ICRNL);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |=  (CS8);

  raw.c_cc[VMIN]  = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey() {
  int nread;
  char c;

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~'){
          switch (seq[1]) {
            case '1': return KEY_HOME;
            case '3': return KEY_DEL;
            case '4': return KEY_END;
            case '5': return KEY_PAGE_UP;
            case '6': return KEY_PAGE_DOWN;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return KEY_HOME;
          case 'F': return KEY_END;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return KEY_HOME;
        case 'F': return KEY_DEL;
      }
    }

    return '\x1b';
  }

  return c;

}

int getCursorPosition(int *rows, int *cols) {
  
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) 
    return -1;

  while (i < sizeof(buf) -1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }

  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return -1;

}

int getWindowSize(int *rows, int *cols){
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
    if (write(STDOUT_FILENO, "\x1b[999C;\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
  }

  return 0;
}

char getCharUnderCursor(){
  return config.row[config.cy].render[config.cx];
}

/*** syntax hightlighting ***/

bool is_separator(int c) {
  return isspace(c) || c == '\0' || strchr("\",.()+-/*=~%<>[];", c) != NULL;
}

bool is_string_brace(int c) {
  return strchr("'\"", c) != NULL;
}

bool is_brace(int c){
  return strchr("()[]{}<>", c) != NULL;
}

void editorUpdateSyntax(erow *row) {
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);

  bool prev_is_sep = 1;
  int last_string_brace = 0;

  int i = 0;
  while (i < row->rsize){
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;   

    if (is_string_brace(c)){
      row->hl[i] = HL_STRING;
      if (last_string_brace && c == last_string_brace) {
        last_string_brace = 0;
      } else if (last_string_brace == 0){
        last_string_brace = c; 
      }
    } else if (last_string_brace) {
      row->hl[i] = HL_STRING;
      if (c == '\\' && i + 1 < row->rsize){
          row->hl[i + 1] = HL_STRING;
          i++;
      }
    } else if (c == '*') {
      row->hl[i] = HL_STAR;
    } else if ((isdigit(c) && (prev_is_sep || prev_hl == HL_NUMBER)) ||
        (c == '.' && prev_hl == HL_NUMBER)) {
      row->hl[i] = HL_NUMBER;
      i++;
      prev_is_sep = 0;
    } else if (is_brace(c)) {
      row->hl[i] = HL_BRACE; 
    }
  
    prev_is_sep = is_separator(c);
    i++;
  }
}

int editorSyntaxToColor(int hl){
  switch (hl) {
    case HL_NUMBER: return 31;
    case HL_STRING: return 32;
    case HL_BRACE : return 33;
    case HL_MATCH : return 34;
    case HL_STAR  : return 35;
    default       : return 37;
  }
}

/*** row operations ***/

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  for (int i = 0; i < cx; i++){
    if (row->chars[i] == '\t')
      rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    rx++;
  }
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0, cx = 0;
  for (cx = 0; cx < row->size; cx++){
    if (row->chars[cx] == '\t')
      cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
    cur_rx++;

    if (cur_rx > rx) return cx;
  }
  return cx;
}

int isCharOpen(int c){
  return (c == '(' || c == '[' || c == '{' || c == '"');
}

char getCloseBrace(char c){
  switch (c){
    case '(' : return ')' ;
    case '[' : return ']' ;
    case '{' : return '}' ;
    case '"' : return '"' ; 
    case '\'': return '\'';
    case '<' : return '>' ;
    default  : return ' ' ;
  }
}

void editorUpdateRow(erow *row) {
  int tabs = 0;

  for (int i = 0; i < row->size; i++)
    if (row->chars[i] == '\t') tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);

  int idx = 0;
  for (int i = 0; i < row->size; i++) {
    if (row->chars[i] == '\t') {
      row->render[idx++] = ' ';
      while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[i];
    }
  }

  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
  
}

void editorInsertRow(int at, char *s, size_t len){
  if (at < 0 || at > config.numrows) return;

  config.row = realloc(config.row, sizeof(erow) * (config.numrows + 1));
  memmove(&config.row[at + 1], &config.row[at],
          sizeof(erow) * (config.numrows - at));

  config.row = (erow*) realloc(config.row, sizeof(erow) * (config.numrows + 1));

  config.row[at].size = len;
  config.row[at].chars = malloc(len + 1);
  memcpy(config.row[at].chars, s, len);
  config.row[at].chars[len] = '\0';

  config.row[at].rsize = 0;
  config.row[at].render = NULL;
  config.row[at].hl = NULL;

  editorUpdateRow(&config.row[at]);

  config.numrows++;
  config.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

void editorDelRow(int at) {
  if (at < 0 || at >= config.numrows) return;
  editorFreeRow(&config.row[at]);
  memmove(&config.row[at], &config.row[at + 1],
          sizeof(erow) * (config.numrows - at - 1));
  config.dirty++;
  if (--config.numrows <= 0)
    editorInsertRow(at, "", 0);
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  config.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  config.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at > row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  config.dirty++;
}

/*** editor operations ***/
 
void editorInsertChar(int c) {
  if (config.cy == config.numrows) {
    editorInsertRow(config.numrows, "", 0);
  }
  editorRowInsertChar(&config.row[config.cy], config.cx++, c);
}

char editorDelChar() {
  
  if (config.cy == config.numrows) return 0;
  if (config.cx <= 0 && config.cy == 0) return 0;

  char deleted = config.row[config.cy].chars[config.cx - 1];

  erow *row = &config.row[config.cy];
  if (config.cx > 0){ 
    editorRowDelChar(row, config.cx-- - 1);
  } else {
    config.cx = config.row[config.cy - 1].size;
    editorRowAppendString(&config.row[config.cy - 1], row->chars, row->size);
    editorDelRow(config.cy--);
  }
  return deleted;
}

void editorInsertNewLine() {
  if (config.cx == 0) {
    editorInsertRow(config.cy, "", 0);
  } else {
    erow *row = &config.row[config.cy];
    editorInsertRow(config.cy + 1, &row->chars[config.cx], 
                    row->size - config.cx);
    row = &config.row[config.cy];
    row->size = config.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  config.cy++;
  config.cx = 0; 

}

/*** file i/o ***/

char *editorRowsToString(int *buflen){
  int totlen = 0;
  for (int i = 0; i < config.numrows; i++)
    totlen += config.row[i].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (int i = 0; i < config.numrows; i++){
    memcpy(p, config.row[i].chars, config.row[i].size);
    p += config.row[i].size;
    *p = '\n';
    p++;
  }

  return buf;

}

void editorOpen(char *filename) {
  free(config.filename);
  config.filename = strdup(filename); 

  FILE *fp = fopen(filename, "r");
  if (fp == NULL) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
      line[linelen - 1] == '\r'))
      linelen--;

    editorInsertRow(config.numrows, line, linelen);
  }

  free(line);
  fclose(fp);

  config.dirty = 0;

}

void editorSave() {
  if (config.filename == NULL){
    config.filename = editorPrompt("Save as: %s", 128, NULL);
    if (config.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(config.filename, O_RDWR | O_CREAT, 0644);

  if (fd == -1) free(buf);
  if (ftruncate(fd, len) == -1) close(fd);
  
  if (write(fd, buf, len) == len){
    close(fd);
    free(buf);
    editorSetStatusMessage("%d bytes written to disk", len);
    config.dirty = 0;
    return;
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));

}

/*** search ***/

void editorSearchCallback(char *query, int key) {

  static int last_match = -1;
  static int direction  =  1;

  static int saved_hl_line;
  static char *saved_hl = NULL;

  if (saved_hl) {
    memcpy(config.row[saved_hl_line].hl, saved_hl,
           config.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }

  switch (key) {
    case '\r':
    case '\x1b':
      last_match = -1;
      direction  =  1;
      return;
    case ARROW_RIGHT:
    case ARROW_DOWN:
      direction = 1;
      break;
    case ARROW_LEFT:
    case ARROW_UP:
      direction = -1;
      break;
    default:
      last_match = -1;
      direction  =  1;
      break;
  } 
  
  if (last_match == -1) direction = 1;
  int current = last_match;
  for (int i = 0; i < config.numrows; i++) {
    current += direction;
    
    if (current == -1) current = config.numrows - 1;
    else if (current == config.numrows) current = 0;

    erow *row = &config.row[current];

    char *match = strstr(row->render, query);
    if (match) {
      last_match = config.cy = current;
      config.cx = editorRowRxToCx(row, match - row->render);
      config.rowoff = config.numrows;

      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);

      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void editorSearch() {

  int saved_cx     = config.cx;
  int saved_cy     = config.cy;
  int saved_coloff = config.coloff;
  int saved_rowoff = config.rowoff;

  char *query = editorPrompt("Search: %s", 128, editorSearchCallback);

  if (query) {
    free(query);
  } else {
    config.cx     = saved_cx;
    config.cy     = saved_cy;
    config.coloff = saved_coloff;
    config.rowoff = saved_rowoff;
  }

}

/*** append buffer ***/

struct abuf {
  char *buf;
  int len;
};

#define ABUF_INIT {NULL, 0};

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->buf, ab->len + len);

  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->buf = new;
  ab->len += len;
}

void abFree(struct abuf *ab){
  free(ab->buf);
}

/*** output ***/

void editorScroll() {
  config.rx = 0;
  if (config.cy < config.numrows) {
    config.rx = editorRowCxToRx(&config.row[config.cy], config.cx);
  }
  if (config.cy < config.rowoff + SCROLL_PADDING) {
    config.rowoff = config.cy - SCROLL_PADDING;
    config.rowoff = MAX(config.rowoff, 0);
  }
  if (config.cy >= config.rowoff + config.screenrows - SCROLL_PADDING
      && config.numrows > config.screenrows) {
    config.rowoff = config.cy - config.screenrows + 1 + SCROLL_PADDING;
    config.rowoff = MIN(config.rowoff, config.numrows - config.screenrows);
  }
  if (config.rx < config.coloff + 1) {
    config.coloff = config.rx - 1;
  }
  if (config.rx >= config.coloff + config.screencols){
    config.coloff = config.rx - config.screencols + 1;
  }
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < config.screenrows; y++) {
    int filerow = y + config.rowoff;

    abAppend(ab, &config.linestart, 1);
    
    if (filerow < config.numrows) {
      int len = config.row[filerow].rsize - config.coloff;
      len = MAX(len, 0);
      if (len > config.screencols) len = config.screencols;
      
      char *c = &config.row[filerow].render[config.coloff];
      unsigned char *hl = &config.row[filerow].hl[config.coloff];
      int current_color = -1;
      if (len > 1){
        for (int i = 0; i < len; i++){
          if (hl[i] == HL_NORMAL) {
            if (current_color != -1){
              abAppend(ab, "\x1b[39m", 5);
              current_color = -1;
            }
            abAppend(ab, &c[i], 1);
          } else {
            int color = editorSyntaxToColor(hl[i]);
            if (color != current_color) {
              current_color = color; 
              char buf[16];
              int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
              abAppend(ab, buf, clen);
            }
            abAppend(ab, &c[i], 1);
          }
        }
        abAppend(ab, "\x1b[39m", 5);
      }
    }
    
    abAppend(ab, CLEAR_LINE_STRING, 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, INVERT_ESCAPE, 4);
  char status[80], rstatus[80];
  
  int len = snprintf(status, sizeof(status), " %.20s%s - %d lines", 
      config.filename ? config.filename : "<unnamed>", 
      config.dirty ? "*" : "", config.numrows);
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d ",
      config.cx, config.cy + 1);

  len = MIN(len, config.screencols);

  abAppend(ab, status, len);

  // fill remaining space
  while (len < config.screencols){
    if (config.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, RESET_ESCAPE, 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab){

  abAppend(ab, BOLD_ESCAPE, 4);

  abAppend(ab, CLEAR_LINE_STRING, 3);
  int msglen = strlen(config.statusmsg);
  msglen = MIN(msglen, config.screencols);
  if (msglen && time(NULL) - config.statusmsg_time < 5)
    abAppend(ab, config.statusmsg, msglen);

  abAppend(ab, RESET_ESCAPE, 3);
}

void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, RESET_MOUSE_POS_STRING, 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf,sizeof(buf),"\x1b[%d;%dH",
           (config.cy - config.rowoff)+1, (config.rx - config.coloff)+1);

  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.buf, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(config.statusmsg, sizeof(config.statusmsg), fmt, ap);
  va_end(ap);
  config.statusmsg_time = time(NULL);
}

/*** input ***/

char *editorPrompt(char *prompt, size_t maxlen, void (*callback)(char*, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while(1){
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    if (c == KEY_DEL || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      editorSetStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r' || buflen >= maxlen) {
      if (buflen != 0) {
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback) callback(buf, c);

  }
}

void editorMoveCursor(int key) {
  erow *row = (config.cy >= config.numrows) ? NULL : &config.row[config.cy]; 

  switch (key) {
    case ARROW_LEFT:
      config.cx--;
      if (config.cx < 0 && config.cy > 0) {
        // go to previous line
        config.cx = config.row[--config.cy].size;
      }
      config.cx = MAX(config.cx, 0);
      break;
    case ARROW_RIGHT:
      config.cx++;
      if (config.cx > row->size && config.cy < config.numrows - 1){
        // go to next line
        config.cy++;
        config.cx = 0;
      }
      config.cx = MIN(config.cx, row->size);
      break;
    case ARROW_UP:
      config.cy--;
      config.cy = MAX(config.cy, 0);;
      break;
    case ARROW_DOWN:
      config.cy++;
      config.cy = MIN(config.cy, config.numrows - 1);
      break;
  }
  
  row = (config.cy >= config.numrows) ? NULL : &config.row[config.cy];
  config.cx = MIN(config.cx, row->size);
}

void editorProcessKeypress() {
  int c = editorReadKey();
  char delc;
  char* promptbuffer;
  static int quit_times = QUIT_TIMES; 

  switch (c) {
    
    case '\r':
      editorInsertNewLine();
      break;

    case CTRL_KEY('q'):
      if (config.dirty && --quit_times > 0) {
        editorSetStatusMessage("WARNING! File has unsaved changes. "
                               "Press C-Q %d more time(s) to quit.", quit_times);
        return;
      }
      write(STDOUT_FILENO, CLEAR_SCREEN_STRING, 4);
      write(STDOUT_FILENO, RESET_MOUSE_POS_STRING, 3);
      exit(0);

    case CTRL_KEY('g'):
      promptbuffer = editorPrompt("go to line: %s", 16, NULL);
      config.cy = promptbuffer ? atoi(promptbuffer) - 1 : config.cy;
      config.cy = MIN(config.cy, config.numrows - 1);
      config.cx = MIN(config.cx, config.row[config.cy].size);
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    case ARROW_LEFT:
    case ARROW_RIGHT:
    case ARROW_UP:
    case ARROW_DOWN:
      editorMoveCursor(c);
      break; 

    case KEY_PAGE_UP:
      config.cy = config.rowoff;
      break;
    case KEY_PAGE_DOWN:
      config.cy = config.screenrows + config.rowoff - 1;
      break;
    case KEY_HOME:
    case CTRL_KEY('0'):
      config.cx = 0;
      break;
    case KEY_END:
    case CTRL_KEY('a'):
      config.cx = MAX(config.row[config.cy].rsize, 1);
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
      if (
        isCharOpen(delc = editorDelChar()) 
        && getCharUnderCursor() == getCloseBrace(delc)
      ){
        config.cx++;
        editorDelChar();
      }
      break;
    case KEY_DEL:
      config.cx++;
      editorDelChar();
      break;

    case CTRL_KEY('l'):
    case '\x1b':
      break;

    case CTRL_KEY('d'):
      editorDelRow(config.cy);
      config.cx = 0;
      break;

    case CTRL_KEY('f'):
      editorSearch();
      break;

    default:
      editorInsertChar(c);
      
      if (isCharOpen(c)){
        editorInsertChar(getCloseBrace(c));
        config.cx--;
      }

      break;
  }

  quit_times = QUIT_TIMES;

}

/*** init ***/

void initEditor() {
  config.cx = 0;
  config.cy = 0;
  config.rx = 1;
  config.rowoff = 0;
  config.coloff = 0;
  config.numrows = 0;
  config.row = NULL;
  config.filename = NULL;
  config.statusmsg[0] = '\0';
  config.statusmsg_time = 0;
  config.dirty = 0;
  config.linestart = '|';

  if (getWindowSize(&config.screenrows, &config.screencols) == -1)
    die("getWindowSize");

  config.screenrows -= 2;

}

int main(int argc, char *argv[]){

  enableRawMode();
  initEditor();

  if (argc >= 2) {
    editorOpen(argv[1]);
  } else {
    editorInsertRow(0, "", 0);
  }

  editorSetStatusMessage("PICO v" PICO_VERSION);

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
