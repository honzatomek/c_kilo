// includes --------------------------------------------------------------- {{{1

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// defines ---------------------------------------------------------------- {{{1

/* CTRL_KEY macro does a bitwise AND of character with the value 00011111 in
 * binary = sets the upper 3 bits of character to 0 (Ctrl key strips bits 5 and 6
 * bitwise of any char that is pressed with Ctrl and sends that, bit numbering
 * starts from 0)
 * also to switch case for characters it is possible to toggle bit 5 */
#define CTRL_KEY(k) ((k) & 0x1f)

// data ------------------------------------------------------------------- {{{1

struct editorConfig {
    /* set up global struct to contain the editor state
     * e.g. width and height of terminal */
    struct termios orig_termios;
};

/* global variable storing editor configuration */
struct editorConfig E;

// terminal --------------------------------------------------------------- {{{1

void die(const char *s) {                                                // {{{2
    /* clear the screen and reposition the cursor at the start of screen */
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    /* from <stdio.h> - prints error message based on global variable errno */
    perror(s);
    /* from <stdlib.h> - exit the program with exit status 1 (non-zero value = failure) */
    exit(1);
}

void disableRawMode() {                                                  // {{{2
    /* reset the terminal attributes after program exit,
     * test the tcsetattr for error
     * use settings from stored global struct */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {                                                   // {{{2
    /* backup terminal attributes, test the tcgetattr for error
     * store the attributes in global struct */
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    /* from <stdlib.h>, used to register disableRawMode() function to be called
     * automatically when the program exits, either by returning from main()
     * or calling exit() function */
    atexit(disableRawMode);

    struct termios raw = E.orig_termios; /* from <termios.h> */
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
     * written to the terminal, discards any unread input
     * test tcsetattr for error */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

char editorReadKey() {                                                   // {{{2
    /* wait for one keypress and return it
     * low level terminal interaction */
    int nread;
    char c;
    /* read 1 character from standard input */
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        /* test read for error, errno and EAGAIN come from <errno.h>,
         * we don't treat EAGAIN as error for portability to Cygwin
         * (it returns -1 with EAGAIN if read() times out */
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

// output ----------------------------------------------------------------- {{{1

void editorDrawRows() {                                                  // {{{2
    /* write tildes at the beginning of each line */
    int y;
    /* assume 24 row high terminal */
    for (y = 0; y < 24; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen() {                                             // {{{2
    /* from <unistd.h>, write 4 bytes to standard output
     * \x1b is an escape character (27, <esc>), the other 3 bytes are [2J
     * control characters:
     * [0J = clear screen from cursor down
     * [1J = clear screen up to cursor
     * [2J = clear whole screen */
    write(STDOUT_FILENO, "\x1b[2J", 4);
    /* escaoe sequence 3 bytes long
     * control characters for positioning the cursor:
     * [12;40H - positions the cursor to the middle of screen on 80x24 terminal
     * [row;columnH, the indexes are 1 based, default is [1;1H = [H */
    write(STDOUT_FILENO, "\x1b[H", 3);

    editorDrawRows();

    /* repostion the cursor at beginning after drawing rows */
    write(STDOUT_FILENO, "\x1b[H", 3);
}

// input ------------------------------------------------------------------ {{{1

void editorProcessKeypress() {                                           // {{{2
    /* wait for keypress and handle it */
    char c = editorReadKey();

    switch (c) {
        /* check whether pressed key = 'q' with bits 5-7 stripped off */
        case CTRL_KEY('q'):
            /* clear the screen and reposition the cursor at the start of screen */
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

// init ------------------------------------------------------------------- {{{1

int main() {                                                             // {{{2
    /* simplified the main() function */
    enableRawMode();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}

// vim: foldmethod=marker
