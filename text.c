#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// terminal attributes
struct termios orig_termios;

// To Disable Raw Mode during Exit
void disableRawMode(){
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// To Turn Off the Echo in Terminal
void enableRawMode(){
  // get the current attributes of terminal in struct 'orig_termios'
  tcgetattr(STDIN_FILENO, &orig_termios);

  // restoring original terminal atrributes on quit
  atexit(disableRawMode);

  // modifying the terminal attributes by hand
  // performing bit-wise NOT on ECHO and then bit-wise AND with c_lflag 
  // and then assigning it to c_lflag
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);

  // setting the modified attributes to terminal
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}



int main() {
  enableRawMode();

  char c;
  while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
  return 0;
}
