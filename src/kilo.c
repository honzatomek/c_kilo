#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode() {
    /* reset the terminal attributes after program exit */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    /* backup terminal attributes */
    tcgetattr(STDIN_FILENO, &orig_termios);
    /* from <stdlib.h>, used to register disableRawMode() function to be called
     * automatically when the program exits, either by returning from main()
     * or calling exit() function */
    atexit(disableRawMode);

    struct termios raw = orig_termios; /* from <termios.h> */
    /* ECHO causes all keys to be printed to the terminal
     * bitflag defined as 00000000000000000000000000001000
     * turned off using AND(flags, NOT(ECHO)) */
    raw.c_lflag &= ~(ECHO);

    /* TCSAFLUSH: when to apply flag change - waits for pending output to be
     * written to the terminal, discards any unread input */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
    return 0;
}
