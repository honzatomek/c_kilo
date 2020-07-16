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
     * (terminal is translation \r to \n)
     * + additional flags to turn on terminal RAW mode
     * BRKINT - if break condition occures, it will cause SIGINT to be sent to the program
     * INPCK - enables parity checking (off for modern terminals)
     * ISTRIP - causes 8th bit of each input byte to be stripped (set to 0) */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* OPOST - from <termios.h>, turn off output processing (e.g. translating
     * \n to \r\n) */
    raw.c_oflag &= ~(OPOST);
    /* additional flags to turn on terminal RAW mode
     * CS8 - a bit mask to set Character Size to 8 bits per byte */
    raw.c_cflag |= (CS8);
    /* ECHO causes all keys to be printed to the terminal
     * bitflag defined as 00000000000000000000000000001000
     * turned off using AND(flags, NOT(ECHO))
     * ICANON sets canonical mode on and off
     * ISIG turn off SIGINT and SIGSTP (Ctrl-C and Ctrl-Z)
     * IEXTEN from <termios.h> turns off Ctrl-V and Ctrl-O */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /* control characters field <termios.h>
     * VMIN - minimum number of bytes of input needed before read() can return
     * VTIME - maximum amount of time to wait before read() returns (1 = 0.1s)
     * if read() times out the return value is 0, otherwise number of bytes read */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    /* TCSAFLUSH: when to apply flag change - waits for pending output to be
     * written to the terminal, discards any unread input */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    while (1) {
        char c = '\0';
        read(STDIN_FILENO, &c, 1);
        /* test whether char is control character <ctype.h> */
        if (iscntrl(c)) {
            /* from <stdio.h>
             * %d means format byte as decimal number
             * %c means to write the byte directly, as a character */
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == 'q') break;
    }

    return 0;
}
