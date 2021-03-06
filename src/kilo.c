// includes --------------------------------------------------------------- {{{1

/* define feature test macros (getline() function complied on compilation)
 * also for better portability
 * the header files use these macros to decide what features to expose */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

// defines ---------------------------------------------------------------- {{{1

#define KILO_VERSION "0.0.1"
/* set tab stop as a constant */
#define KILO_TAB_STOP 8

/* CTRL_KEY macro does a bitwise AND of character with the value 00011111 in
 * binary = sets the upper 3 bits of character to 0 (Ctrl key strips bits 5 and 6
 * bitwise of any char that is pressed with Ctrl and sends that, bit numbering
 * starts from 0)
 * also to switch case for characters it is possible to toggle bit 5 */
#define CTRL_KEY(k) ((k) & 0x1f)

/* crete key constants for further use
 * use numbers out of normal character range - e.g. 1000+ */
enum editorKey {                                                         // {{{2
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_RIGHT,
    ARROW_LEFT,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

// data ------------------------------------------------------------------- {{{1

/* editor row - line of text as a pointer to a dynamically allocated character
 * data and a length
 * the typedef lets us refer to the type as `erow` insted of `struct erow` */
typedef struct erow {                                                    // {{{2
    /* size of chars */
    int size;
    /* size of render */
    int rsize;
    /* actual line characters */
    char *chars;
    /* rendered line characters */
    char *render;
} erow;

struct editorConfig {                                                    // {{{2
    /* store the cursor position, cx = horizontal (left to right, zero based),
     * cy = vertical (top to bottom, zero based)*/
    int cx, cy;
    /* row offset for scrolling */
    int rowoff;
    /* column offset for scrolling */
    int coloff;
    /* set up global struct to contain the editor state
     * e.g. width and height of terminal */
    int screenrows;
    int screencols;
    /* for now the editor can only display one line of text
     * -> numrows = {0 / 1} */
    int numrows;
    /* make row a dynamically allocated array of erow structs -> make _row_
     * a pointer */
    erow *row;
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

int editorReadKey() {                                                    // {{{2
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

    /* if we read an escape character */
    if (c == '\x1b') {
        /* escape sequence buffer, 3 bytes long to process also other escape
         * sequences apart from arrow keys */
        char seq[3];

        /* automatically read tow more bytes into seq buffer, if the read()
         * function times out, assume the user pressed <esc> and return
         * that */
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        /* \x1b[1~ = Home */
                        case '1': return HOME_KEY;
                        /* \x1b[2~ = End */
                        case '2': return END_KEY;
                        /* \x1b[3~ = Del */
                        case '3': return DEL_KEY;
                        /* \x1b[5~ = PageUp */
                        case '5': return PAGE_UP;
                        /* \x1b[6~ = PageDown */
                        case '6': return PAGE_DOWN;
                        /* \x1b[7~ = Home */
                        case '7': return HOME_KEY;
                        /* \x1b[8~ = End */
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    /* \x1b[A = up arrow */
                    case 'A': return ARROW_UP;
                    /* \x1b[B = down arrow */
                    case 'B': return ARROW_DOWN;
                    /* \x1b[C = right arrow */
                    case 'C': return ARROW_RIGHT;
                    /* \x1b[D = left arrow */
                    case 'D': return ARROW_LEFT;
                    /* \x1b[H = Home */
                    case 'H': return HOME_KEY;
                    /* \x1b[F = End */
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                /* \x1bOH = Home */
                case 'H': return HOME_KEY;
                /* \x1bOF = End */
                case 'F': return END_KEY;
            }
        }

        /* if it is an escape sequence that is not yet recognised, return just
         * escape */
        return '\x1b';
    } else {
        /* return read character */
        return c;
    }
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

// row operations --------------------------------------------------------- {{{1

void editorUpdateRow(erow *row) {                                        // {{{2
    /* rendering tab characters as 8 spaces */
    int tabs = 0;
    int j;
    /* count the number of chars in line */
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    /* free memory allocated for render array */
    free(row->render);
    /* allocate memory for the line, tabs are 8 spaces (1 for character and add 7 */
    row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);

    int idx;
    /* for now just copy the contents of actual line to render array */
    for (j = 0; j < row->size; j++) {
        /* render tabs */
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            /* pad with spaces until tabstop = column number divisible by 8 */
            while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    /* end the array with nullchar */
    row->render[idx] = '\0';
    /* render size = number of characters */
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {                              // {{{2
    /* have to tell realloc() how many bytes to allocate */
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    /* set _at_ to th index of the new row */
    int at = E.numrows;
    /* set size field to the length of the row */
    E.row[at].size = len;
    /* from <stdlib.h>
     * allocate enough memory for the row */
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    /* initialize render array */
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    /* set numrows + 1 */
    E.numrows++;
}

// file i/o --------------------------------------------------------------- {{{1

void editorOpen(char *filename) {                                        // {{{2
    /* FILE, fopen() and getline() come from <stdio.h>
     * editorOpen() takes filename as an argument and uses fopen() to open
     * the file for reading */
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    /* get line and linelen from getline() instead of hardcoded values
     * getline() is useful for reading lines from a file if we do not know
     * how much memory to allocate for it
     * first we pass it a null _line_ pointer and a _linecap_ (line capacity)
     * of 0. getline() reads a line into memory and sets _line_ to point to
     * the memory, also sets _linecap_ to know how much memory was allocated.
     * its return value is the length of the line read or -1 if at the EOF.
     * later we'll feed _line_ and _linecap_ back into getline() and it will
     * try to reuse the memory already allocated as long the _linecap_ is
     * big enough */
    /* while loop to read the whole file, the while loop works because
     * getline() returns -1 at EOF */
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        /* strip newline and carriage return chars from the end of the line */
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}

// append buffer ---------------------------------------------------------- {{{1

/* struct for append buffer to print whole screen at once */
struct abuf {                                                            // {{{2
    char *b;
    int len;
};

/* constructor for abuf type */
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {                 // {{{2
    /* realloc() from <stdlib.h>
     * first make sure to have enough memory - realloc returns block of
     * memory the size of current string + the size of string we are appending
     * realloc() either extends the current memory block or will take care of
     * freeing the current memory block and allocating a new one */
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    /* copy string s to the end of the current string in the buffer */
    memcpy(&new[ab->len], s, len);
    /* update abuf pointer and length */
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {                                           // {{{2
    /* free() from <stdlib.h>
     * destructor that deallocates the buffer dynamic memory */
    free(ab->b);
}

// output ----------------------------------------------------------------- {{{1

void editorScroll() {                                                    // {{{2
    /* if the cursor is above the visible window scroll to the cursor
     * position */
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    /* if the cursor is below visible window, scroll accordingly */
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    /* if the cursor is left of the visible window scroll to the cursor
     * position */
    if (E.cx < E.coloff) {
        E.coloff = E.cx;
    }
    /* if the cursor is right of the visible window, scroll accordingly */
    if (E.cx >= E.coloff + E.screencols) {
        E.coloff = E.cx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab) {                                   // {{{2
    /* write tildes at the beginning of each line */
    int y;
    for (y = 0; y < E.screenrows; y++) {
        /* actual file row */
        int filerow = y + E.rowoff;
        /* check wheter we are drawing a row that is part of the text buffer
         * or a row that comes after */
        if (filerow >= E.numrows) {
            /* display the welcome message only if no file was supplied */
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                /* snpfintf() form <stdio.h>, used to interpolate kilo version
                 * into the welcome message */
                int welcomelen = snprintf(welcome, sizeof(welcome),
                        "Kilo editor -- version %s", KILO_VERSION);
                /* truncate the welcome message if it does not fit to screen */
                if (welcomelen > E.screencols) welcomelen = E.screencols;
                /* center the welcome message */
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                /* fill the space up to string with space characters */
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else {
                /* print tildes on each row of screen */
               abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            /* in case the user scrolled horzontally past the end of line
             * in that case set len to 0 so that nothing is displayed */
            if (len < 0) len = 0;
            /* truncate the rendered line if it goes beyond the screen */
            if (len > E.screencols) len = E.screencols;
            /* simply write out the chars fields of the erow */
            /* use E.coloff as an index to the character display */
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }

        /* clear line before repainting
         * [0K = clear line from cursor right (default)
         * [1K = clear line up to cursor
         * [2K = clear whole line */
        abAppend(ab, "\x1b[K", 3);
        /* do not print carriage return on last line of screen */
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {                                             // {{{2
    /* call scrolling function before each refresh */
    editorScroll();

    /* initialise buffer ab */
    struct abuf ab = ABUF_INIT;
    /* escape sequence to hide the cursor */
    abAppend(&ab, "\x1b[?25l", 6);
    /* from <unistd.h>, write 4 bytes to standard output
     * \x1b is an escape character (27, <esc>), the other 3 bytes are [2J
     * control characters:
     * [0J = clear screen from cursor down
     * [1J = clear screen up to cursor
     * [2J = clear whole screen */
    /* escape sequence 3 bytes long
     * control characters for positioning the cursor:
     * [12;40H - positions the cursor to the middle of screen on 80x24 terminal
     * [row;columnH, the indexes are 1 based, default is [1;1H = [H */
    /* append to buffer */
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    /* position the cursor */
    char buf[32];
    /* use snprintf() to inject cursor position into string in buf variable */
    /* subtract __rowoff__ from __cy__ to get correct behavior when scrollup */
    /* subtract __coloff__ from __cx__ to get correct behavior when scrolling */
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                              (E.cx - E.coloff) + 1);
    /* strlen() is form <string.h> */
    abAppend(&ab, buf, strlen(buf));

    /* escape sequence to show the cursor */
    abAppend(&ab, "\x1b[?25h", 6);

    /* write the buffer contents to standard output */
    write(STDOUT_FILENO, ab.b, ab.len);
    /* free the buffer memory */
    abFree(&ab);
}

// input ------------------------------------------------------------------ {{{1

void editorMoveCursor(int key) {                                         // {{{2
    /* check if the cursor is on the actual line. if so, the row will point
     * to the erow the cursor is on */
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    /* use arrows for movement */
    switch (key) {
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            /* allow cursor to advance past the bottom of the screen */
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
        case ARROW_RIGHT:
            /* allow user to scroll past the right edge of the screen */
            /* while limiting the movement to the length of the line + 1 */
            if (row && E.cx < row->size) {
                E.cx++;
            /* left arrow at the end of line goes to the start of next line */
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            /* allow user to move to the end of line above if at the beginning
             * of a line
             * make sure we are not at the first line */
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
    }

    /* fix vertical movement - snap to the length of line
     * set row again as E.cy will be pointing to a different line */
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    /* get length of row the cursor is on, consider NULL line to be of 0 length */
    int rowlen = row ? row->size : 0;
    /* if the cursor is to the right of the line end, set it to the end
     * of the current line */
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editorProcessKeypress() {                                           // {{{2
    /* wait for keypress and handle it */
    int c = editorReadKey();

    switch (c) {
        /* check whether pressed key = 'q' with bits 5-7 stripped off */
        case CTRL_KEY('q'):
            /* clear the screen and reposition the cursor at the start of screen */
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        /* check for movement keys */
        case HOME_KEY:
            /* set cursor to the beginning of line */
            E.cx = 0;
            break;
        case END_KEY:
            /* set cursor to the end of line */
            E.cx = E.screencols - 1;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            /* block of code in braces to declare the times variable
             * the up and down arrow keys are simulated to move to the bottom
             * or top of the screen
             * this way of implementation is for further use with scrolling */
            {
                int times = E.screenrows;
                while (times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_RIGHT:
        case ARROW_LEFT:
            editorMoveCursor(c);
            break;
    }
}

// init ------------------------------------------------------------------- {{{1

void initEditor() {                                                      // {{{2
    /* initialise the cursor position to the top left of screen */
    E.cx = 0;
    E.cy = 0;
    /* initialise row offset to 0 - scrolled to the top as default */
    E.rowoff = 0;
    /* initialise column offset to 0 - scrolled to the left as default */
    E.coloff = 0;
    /* initialise number of rows as 0 */
    E.numrows = 0;
    /* initialize E.row pointer to be NULL */
    E.row = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {                                       // {{{2
    /* simplified the main() function */
    enableRawMode();
    /* initialize all the fields inf the E struct */
    initEditor();
    /* editorOpen() will be for opening and reading a file from disk
     * if filename is supplied to kilo then open it, otherwise continue with
     * empty file */
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}

// vim: foldmethod=marker
