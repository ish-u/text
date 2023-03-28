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
struct termios orig_termios;

/*** terminal ***/
// To Handle Errors
void die(const char *s) {
  perror(s);
  exit(1);
}

// To Disable Raw Mode during Exit
void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
    die("tcgetattr");
  }
}

// To Turn Off the Echo in Terminal
void enableRawMode() {
  // get the current attributes of terminal in struct 'orig_termios'
  if (tcgetattr(STDIN_FILENO, &orig_termios)) {
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
  struct termios raw = orig_termios;
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
    exit(0);
    break;
  }
}

/*** init ***/
int main() {
  enableRawMode();

  while (1) {
    editorProcessKeypresses();
  }
  return 0;
}
