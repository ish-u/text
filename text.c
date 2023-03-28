/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
// All Ctrl + k operations results in 0x[ASCII_CODE_IN_HEX] & 0x1f
// Ctrl + Q = 0x17 => 0b01110001 & 0b00011111 = 0b00010001 = 0x17
#define CTRL_KEY(k) ((k)&0x1f)

/*** data ***/
struct editorConfig {
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

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
char editorReadKey() {
  int nread;
  char c;
  // failing C Library function sets errno to some value to indicate failure
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  return c;
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
// Handle the KeyPress
void editorProcessKeypresses() {
  char c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    // Clearing the Screen and Repositioning Cursor on Exit
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  }
}

/*** output ***/
// Add Rows to Standard Output
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    abAppend(ab, "~", 1);

    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
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
  // 1;1 Arguments are seperated by ; <esc>[2J - Clear Whole Screen <esc>[1J -
  // Clear Screen Upto Cursor <esc>[0J - Clear Screen From Cursor till end

  struct abuf ab = ABUF_INIT;
  
  // Clearing the Screen - 4byte Long
  abAppend(&ab, "\x1b[2J", 4);
  // Repositioning the Cursor - 3byte Long
  abAppend(&ab, "\x1b[H", 3);

  // Drawing Rows
  editorDrawRows(&ab);
  abAppend(&ab, "\x1b[H", 3);

  write(STDIN_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** init ***/
void initEditor() {
  if (getWindowSize(&E.screenrows, &E.screenrows) == -1)
    die("getWindowSize");
}

int main() {
  enableRawMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypresses();
  }
  return 0;
}
