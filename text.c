#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// terminal attributes
struct termios orig_termios;

// To Disable Raw Mode during Exit
void disableRawMode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

// To Turn Off the Echo in Terminal
void enableRawMode() {
  // get the current attributes of terminal in struct 'orig_termios'
  tcgetattr(STDIN_FILENO, &orig_termios);

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

  // setting the modified attributes to terminal
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
  enableRawMode();

  char c;
  while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
  }
  return 0;
}
