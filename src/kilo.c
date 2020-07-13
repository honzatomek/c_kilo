#include <termios.h>
#include <unistd.h>

void enableRawMode() {
    struct termios raw; /* from <termios.h> */

    tcgetattr(STDIN_FILENO, &raw);

    /* ECHO causes all keys to be printed to the terminal
     * bitflag defined as 00000000000000000000000000001000
     * turned using AND(flags, NOT(ECHO)) */
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
