#include <ctype.h>
#include <stdio.h>
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
    /* IXON turns off sending XON and XOFF (Ctrl-S and Ctrl-Q)
     * ICRNL from <termios.h>, turns Ctrl-M + Enter are read as 13 instead of 10
     * (terminal is translation \r to \n) */
    raw.c_iflag &= ~(ICRNL | IXON);
    /* OPOST - from <termios.h>, turn off output processing (e.g. translating
     * \n to \r\n) */
    raw.c_oflag &= ~(OPOST);
    /* ECHO causes all keys to be printed to the terminal
     * bitflag defined as 00000000000000000000000000001000
     * turned off using AND(flags, NOT(ECHO))
     * ICANON sets canonical mode on and off
     * ISIG turn off SIGINT and SIGSTP (Ctrl-C and Ctrl-Z)
     * IEXTEN from <termios.h> turns off Ctrl-V and Ctrl-O */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /* TCSAFLUSH: when to apply flag change - waits for pending output to be
     * written to the terminal, discards any unread input */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
        /* test whether char is control character <ctype.h> */
        if (iscntrl(c)) {
            /* from <stdio.h>
             * %d means format byte as decimal number
             * %c means to write the byte directly, as a character */
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
    }

    return 0;
}
