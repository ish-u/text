/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

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

/*** init ***/

int main() {
  enableRawMode();

  while (1) {
    char c = '\0';
    // failing C Library function sets errno to some value to indicate failure
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {
      die("read");
    }
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }

    if (c == 'q')
      break;
  }
  return 0;
}
