/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/
#define TEXT_VERSION "0.0.1"
#define TEXT_TAB_STOP 8
#define TEXT_QUIT_TIMES 3
// All Ctrl + k operations results in 0x[ASCII_CODE_IN_HEX] & 0x1f
// Ctrl + Q = 0x17 => 0b01110001 & 0b00011111 = 0b00010001 = 0x17
#define CTRL_KEY(k) ((k)&0x1f)

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** data ***/
// Editor Row - Store the Line of Text
typedef struct erow {
  int size;     // Size of Line
  int rsize;    // size of content of render
  char *chars;  // Pointer to Character Data of Line
  char *render; // contains string for Characters to draw on screen
} erow;

struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row; // Array of erow where each erow stores a line read from a file
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termios;
};

struct editorConfig E;

/*** prototype ***/
void editorSetStatusMessage(const char *fmt, ...);

/*** terminal ***/
// To Handle Errors
void die(const char *s) {
  // Clearing the Screen and Repositioning Cursor on Exit
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

// To Disable Raw Mode during Exit
void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
    die("tcgetattr");
  }
}

// To Turn Off the Echo in Terminal
void enableRawMode() {
  // get the current attributes of terminal in struct 'orig_termios'
  if (tcgetattr(STDIN_FILENO, &E.orig_termios)) {
    die("tcsetattr");
  }

  // restoring original terminal atrributes on quit
  atexit(disableRawMode);

  // modifying the terminal attributes by hand
  // ICANON - program quits as soon as we type 'q'
  // ISIG - disbale Ctrl+C Ctrl+Z to quit when pressed
  // IXON - disable transmission pause/resume using Ctrl+S and Ctrl+Q
  // IEXTEN - disbale Ctrl+V and Ctrl+O in some systems
  // ICRNL - makes Ctrl+M print 13 and also make enter read as 13
  // OPOST - disbale \r \n
  // BRKINT, INPCK, ISTRIP, and CS8 - Other Miscll. flags for raw mode
  // Ctrl + V - Still Pasting Clipboard Contents
  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_iflag &= ~(ICRNL | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_lflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  // read timeout
  // c_cc = Control Characters
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  // setting the modified attributes to terminal
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Wait for an Key Press
int editorReadKey() {
  int nread;
  char c;
  // failing C Library function sets errno to some value to indicate failure
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  // If we read an escape chracter we *immediately*
  // read the next two letters after it and remap them to WASD
  // if they are valid arrow keys sequences
  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      // PageUp - <esc>[5~ , PageDown - <esc>[6~
      // Home Key - <esc>[1~, <esc>[7~, <esc>[H, or <esc>OH
      // End Key - <esc>[4~, <esc>[8~, <esc>[F, or <esc>OF
      // Delete Key - <esc>[3~
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

// get the cursor position for finding window size if ioctl fails
int getCursorPosition(int *rows, int *cols) {

  // For parsing relevant information from the result of n command
  char buf[32];
  unsigned int i = 0;

  // Ref : https://vt100.net/docs/vt100-ug/chapter3.html#DSR
  // using the n command we get the cursor position report that contains
  // the relevant information for us to find the Window Size
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  // Reading the result of the n command/escape_seq in buf
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[1] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;

  // printf("\r\n");
  // char c;
  // while (read(STDIN_FILENO, &c, 1) == 1) {
  //   if (iscntrl(c)) {
  //     printf("%d\r\n", c);
  //   } else {
  //     printf("%d ('%c')\r\n", c, c);
  //   }
  // }
  // editorReadKey();
  // return  0;
}

// get the rows and col of the terminal
int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  // TIOCGWINSZ - Terminal IOCtl(IOCtl - Input/Output Control) Get Window Size
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // C - Cursor Forward - move to right
    // B - Cursor Down - moves cursor down
    // We Supply the 999 as the argumet for these Escape Sequences
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** row operations ***/
// For moving tabs - Converts a e.chars index into a e.render index
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t') {
      rx *= (TEXT_TAB_STOP - 1) - (rx % TEXT_TAB_STOP);
    }
    rx++;
  }
  return rx;
}

// For Rendering Special Characters
void editorUpdateRow(erow *row) {
  // Counting the number of tabs in the row
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t')
      tabs++;
  }

  free(row->render);
  row->render = malloc(row->size + tabs * (TEXT_TAB_STOP - 1) + 1);

  // Copying all the characters from row->chars to row->render
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % TEXT_TAB_STOP != 0) {
        row->render[idx++] = ' ';
      }
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
  // Reallocate(Resize Memory Block) to accomadate a new erow in E.row array
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  // index of the new element of E.row array
  int at = E.numrows;
  // Setting the Size of Line in a Row
  E.row[at].size = len;
  // Allocating Memory for Character of the Line in a Row
  E.row[at].chars = malloc(len + 1);
  // Copying Characters from line to Line in a Row
  memcpy(E.row[at].chars, s, len);
  // Adding the Ending chracter to the copied Characters
  E.row[at].chars[len] = '\0';

  // For Rendering Special Characters
  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  // Filling E.row.render using E.row.chars
  editorUpdateRow(&E.row[at]);

  // Incrementing the Row Count
  E.numrows++;

  E.dirty++;
}

// Insert a char in erow
void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) {
    at = row->size;
  }
  // allocating 1 byte for new character (1 for new char + 1 for NULL)
  row->chars = realloc(row->chars, row->size + 2);
  // moves characters from at to at+1 so we can insert new chracter
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  // inserting 'c'
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

// Delete char in erow
void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) {
    return;
  }

  // move the chracters from at+1 to at -> removing the character
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/*** editor operations ***/
void editorInsertChar(int c) {

  if (E.cy == E.numrows) {
    // inserting a new row because the editor is at the EOF on tilde line
    editorAppendRow("", 0);
  }
  // inserting a new character at cusror position (E.cx,E,cy)
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  // moving cursor forward after inserting the character
  E.cx++;
}

void editorDelChar() {
  // nothing to delete at EOF
  if (E.cy == E.numrows) {
    return;
  }

  // getting the current row
  erow *row = &E.row[E.cy];
  // get the row the cursor is on
  // and if there is character left to cursor delete it
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  }
}

/*** file i/o ***/
// Converting the erow struct array to string that can be written
// to a file
char *editorRowsToString(int *buflen) {
  // total length of the Buffer - Sum of all chars of erow
  int totlen = 0;

  // adding 1 to each erow for '\n'
  int j;
  for (j = 0; j < E.numrows; j++) {
    totlen += E.row[j].size + 1;
  }
  *buflen = totlen;

  // allocating the memory for the whole buffer
  char *buf = malloc(totlen);
  // looping through each row and adding '\n' to each row
  // and copying each row to the end of the buffer - 'buf'
  // (using *p as a temp pointer)
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    // copying j-th row chars to pointer 'p' (our temp buffer)
    memcpy(p, E.row[j].chars, E.row[j].size);
    // offseting the pointer to end of the copied characters
    p += E.row[j].size;
    // writing new line at the end
    *p = '\n';
    p++;
  }
  // returning the buffer
  return buf;
}

void editorOpen(char *filename) {
  // Storing File Name in editor config
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  // Reading the File Line-by-Line and Appending Each line as erow
  // to E.row array
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--;
    }
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave() {
  if (E.filename == NULL) {
    return;
  }

  // getting the buffer and it's length
  int len;
  char *buf = editorRowsToString(&len);

  // opening file
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    // setting the file size to the buffer length
    if (ftruncate(fd, len) != -1) {
      // writing the buffer to the file
      if (write(fd, buf, len) != -1) {
        // closing the file
        close(fd);
        // freeing the allocated memory
        free(buf);
        // Resetting Dirty buffer
        E.dirty = 0;
        // Setting the Status that the file is saved
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  // Setting the Status that the file didn't save correctly
  editorSetStatusMessage("Can't save ! I/O error : %s", strerror(errno));
}

/*** append buffer ***/
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0};

// append a string 's' to append-buffer 'abuf'
void abAppend(struct abuf *ab, const char *s, int len) {
  // allocate memory to hold the new string
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  // copy the string 's' after string original string
  memcpy(&new[ab->len], s, len);

  // assigning new string and len to 'abuf'
  ab->b = new;
  ab->len += len;
}

// to deallocate memory used by 'abuf'
void abFree(struct abuf *ab) { free(ab->b); }

/*** input ***/
// Handle Cursor Motion
void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0) {
      E.cx--;
    }
    // Move Cursor to end of above line
    // when pressing left arrow at the start of a line
    else if (E.cy > 0) {
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_RIGHT:
    if (row && E.cx < row->size) {
      E.cx++;
    }
    // Move Cursor to start of below line
    // when pressing right arrow at the end of a line
    else if (row && E.cx == row->size) {
      E.cy++;
      E.cx = 0;
    }
    break;
  case ARROW_UP:
    if (E.cy != 0) {
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows) {
      E.cy++;
    }
    break;
  }

  // Make the Cursor not move past the length of a row
  // when moving from large length line to small length line
  // (Snap Cursor to end of line)
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}
// Handle the KeyPress
void editorProcessKeypresses() {
  static int quit_times = TEXT_QUIT_TIMES;

  int c = editorReadKey();

  switch (c) {
  case '\r':
    // TODO
    break;
  case CTRL_KEY('q'):
    if (E.dirty && quit_times > 0) {
      editorSetStatusMessage("WARNING!! File has unsaved changes. "
                             "Press Ctrl-Q %d more times to quit.",
                             quit_times);
      quit_times--;
      return;
    }
    // Clearing the Screen and Repositioning Cursor on Exit
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  case CTRL_KEY('s'):
    editorSave();
    break;
  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    if (E.cy < E.numrows) {
      E.cx = E.row[E.cy].size;
    }
    break;
  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    // DEL_KEY deletes characters to the right of cursor
    if (c == DEL_KEY) {
      editorMoveCursor(ARROW_RIGHT);
    }
    editorDelChar();
    break;
  case PAGE_UP:
  case PAGE_DOWN: {
    // Scroll within Page
    if (c == PAGE_UP) {
      E.cy = E.rowoff;
    } else if (c == PAGE_DOWN) {
      E.cy = E.rowoff + E.screenrows - 1;
      if (E.cy > E.numrows) {
        E.cy = E.numrows;
      }
    }
  } break;
  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;

  case CTRL_KEY('l'):
  case '\x1b':
    break;
  default:
    editorInsertChar(c);
    break;
  }

  // Restting Quit Times if any other key then Ctrl-Q is pressed
  quit_times = TEXT_QUIT_TIMES;
}

/*** output ***/
// For Scrolling
void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}
//
// Add Rows to Standard Output
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      // Show the Welcome Message - Only show when open without a file
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "Text Editor -- version %s", TEXT_VERSION);
        if (welcomelen > E.screencols)
          welcomelen = E.screencols;
        // Centering the Message
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--)
          abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencols)
        len = E.screencols;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }
    // EraseCurrent Line -[0k => 0 to erase part of line after the cursor
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

// Draw Status Line
void editorDrawStatusBar(struct abuf *ab) {
  // <esc>[7m - To Switch to Inverted Colours
  // m command - https://vt100.net/docs/vt100-ug/chapter3.html#SGR
  abAppend(ab, "\x1b[7m", 4);

  // Creating the Status Text and finding it's length
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[No Name]", E.numrows,
                     E.dirty ? "(modified)" : "");
  // right status
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

  if (len > E.screencols)
    len = E.screencols;

  // Appending Statusto the buffer
  abAppend(ab, status, len);
  while (len < E.screencols) {
    // Appending rstatus to right of status bar
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }

  // Resting Inverted Colors
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

// Draw Message Bar
void editorDrawMessageBar(struct abuf *ab) {
  // Clearing the text after cursor
  abAppend(ab, "\x1b[K", 3);
  // Appending the Message
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) {
    msglen = E.screencols;
  }
  if (msglen && time(NULL) - E.statusmsg_time < 5) {
    abAppend(ab, E.statusmsg, msglen);
  }
}

// Clear/Refresh Screen + Reposition Cursor
void editorRefreshScreen() {
  // Ref : https://vt100.net/docs/vt100-ug/chapter3.html#CUP
  // \x1b - Escape Character - 27
  // [2J - Reamingin 3 Bytes
  // Escape Squence Starts with - '\x1b['
  // J - Erases the Terminal, 2 is the Argument for this Escape Sequence
  // H - Cursor Position - takes 2 Arguments => RowNo. and ColNo. => Default
  // h - Set Mode
  // l - Reset Mode
  // 1;1 Arguments are seperated by ; <esc>[2J - Clear Whole Screen <esc>[1J -
  // Clear Screen Upto Cursor <esc>[0J - Clear Screen From Cursor till end

  // for Scrolling
  editorScroll();

  struct abuf ab = ABUF_INIT;

  // Hide Cursor
  abAppend(&ab, "\x1b[?25l", 6);
  // Repositioning the Cursor
  abAppend(&ab, "\x1b[H", 3);

  // Drawing Rows
  editorDrawRows(&ab);
  // Drawing Status Bar
  editorDrawStatusBar(&ab);
  // Drawing Message Bar
  editorDrawMessageBar(&ab);

  // Cursor Motion
  char buf[32];
  // E.rowoff & E.coloff sets the cursor offsets that allow to scroll
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
           (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  // Show Cursor
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDIN_FILENO, ab.b, ab.len);
  abFree(&ab);
}

// Write Status Message
// '...' three dots as arguments
// -https://en.wikipedia.org/wiki/Variadic_function
void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** init ***/
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = 0;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("HELP : Ctrl-S = save | Ctrl-Q = quit");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypresses();
  }
  return 0;
}
