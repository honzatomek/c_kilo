// includes --------------------------------------------------------------- {{{1

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
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

struct editorConfig {                                                    // {{{2
    /* set up global struct to contain the editor state
     * e.g. width and height of terminal */
    int screenrows;
    int screencols;
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

int getCursorPosition(int *rows, int *cols) {                            // {{{2
    /* buffer variable to store [6n command output */
    char buf[32];
    unsigned int i = 0;

    /* send [6n command to query the terminal for cursor position
     * (n = device status report request, 6 = cursor position */
    /* returns an escape sequence to stdout: \x1b[24;80R or similar */
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        /* read chars into the prepared buffer */
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        /* stop on 'R' character */
        if (buf[i] == 'R') break;
        i++;
    }
    /* printf() function expects the last char of string to be null char */
    buf[i] = '\0';

    /* if buffer does not start with \x1b[ then return error (-1) */
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    /* sscanf() from <stdio.h>, we pass a pointer to the buffer, skipping the
     * \x1b[ characters and tell sscanf() to parse two integers in form %d;%d
     * and save the result into rows and cols variable */
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    editorReadKey();

    return 1;
}

int getWindowSize(int *rows, int *cols) {                                // {{{2
    /* from <sys/ioctl.h> */
    struct winsize ws;

    /* from <sys/ioctl.h>
     * on succes the ioctl() will place the terminal window size into struct
     * winsize struct, on failure returns -1
     * we also check the returned values for 0 as that is possible error */
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* if ioctl() cannot return terminal size, position the cursor at the
         * end: [999C moves cursor right by 999 columns (stops at screen edge)
         *      [999B moves cursor down by 999 row (stops at screen edge) */
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        /* on ioctl success save window size and return 0 */
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

// output ----------------------------------------------------------------- {{{1

void editorDrawRows() {                                                  // {{{2
    /* write tildes at the beginning of each line */
    int y;
    /* print tildes on each row of screen */
    for (y = 0; y < E.screenrows; y++) {
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

void initEditor() {
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {                                                             // {{{2
    /* simplified the main() function */
    enableRawMode();
    /* initialize all the fields inf the E struct */
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}

// vim: foldmethod=marker
