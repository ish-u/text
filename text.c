/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
// All Ctrl + k operations results in 0x[ASCII_CODE_IN_HEX] & 0x1f
// Ctrl + Q = 0x17 => 0b01110001 & 0b00011111 = 0b00010001 = 0x17
#define CTRL_KEY(k) ((k)&0x1f)

/*** data ***/
struct editorConfig {
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
void editorDrawRows() {
  int y;
  for (y = 0; y < 24; y++) {
    write(STDOUT_FILENO, "~\r\n", 3);
  }
}

// Clear/Refresh Screen + Reposition Cursor
void editorRefreshScreen() {
  // Ref : https://vt100.net/docs/vt100-ug/chapter3.html#CUP
  // \x1b - Escape Character - 27
  // [2J - Reamingin 3 Bytes
  // Escape Squence Starts with - '\x1b['
  // J - Erases the Terminal, 2 is the Argument for this Escape Sequence
  // H - Cursor Position - takes 2 Arguments => RowNo. and ColNo. => Default 1;1
  // Arguments are seperated by ;
  // <esc>[2J - Clear Whole Screen
  // <esc>[1J - Clear Screen Upto Cursor
  // <esc>[0J - Clear Screen From Cursor till end

  // Clearing the Screen - 4byte Long
  write(STDOUT_FILENO, "\x1b[2J", 4);
  // Repositioning the Cursor - 3byte Long
  write(STDOUT_FILENO, "\x1b[H", 3);

  // Drawing Rows
  editorDrawRows();
  write(STDOUT_FILENO, "\x1b[H", 3);
}


/*** init ***/
int main() {
  enableRawMode();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypresses();
  }
  return 0;
}
