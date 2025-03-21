#ifdef MINIVIM_LINUX
#   include <stdio.h>
// #include <sys.h>
#   include <stdlib.h>
#   include <errno.h>
#   include <string.h>
#   include <stdbool.h>
#   include <unistd.h>
#   include <fcntl.h>
#else
#   include <stdio.h>
#   include <sys.h>
#   include <stdlib.h>
#   include <errno.h>
#   include <string.h>
#endif

#define fd_t int
#define assert(expr) ((expr) ? assert_void() : assert_fail (#expr, __FILE__, __LINE__, __FUNCTION__))

#define MAX_CMD_SIZE 15
#define STATE_VIEW 0
#define STATE_CMD 1
#define STATE_EDIT 2

#define CTRL_KEY(x) ((x) & 0x1f)

#define CODE_SPECIAL 224
#define CODE_LEFT_ARROW 1001
#define CODE_UP_ARROW 1002
#define CODE_RIGHT_ARROW 1003
#define CODE_DOWN_ARROW 1004
#define CODE_BACKSPACE 8
#define CODE_DELETE 127
#define CODE_ENTER 10
#define CODE_ESC 27

#define COLOR_FG_BLACK 30
#define COLOR_FG_RED 31
#define COLOR_FG_GREEN 32
#define COLOR_FG_YELLOW 33
#define COLOR_FG_BLUE 34
#define COLOR_FG_MAGENTA 35
#define COLOR_FG_CYAN 36
#define COLOR_FG_WHITE 37
#define COLOR_FG_DEFAULT 39
#define COLOR_BG_BLACK 40
#define COLOR_BG_RED 41
#define COLOR_BG_GREEN 42
#define COLOR_BG_YELLOW 43
#define COLOR_BG_BLUE 44
#define COLOR_BG_MAGENTA 45
#define COLOR_BG_CYAN 46
#define COLOR_BG_WHITE 47
#define COLOR_BG_DEFAULT 49

#define CMD_FLAG_QUIT 0x1
#define CMD_FLAG_WRITE 0x02
#define CMD_FLAG_FORCE 0x04

#define ROW_FLAG_WRAPPED 0x1

#define WH_OFFSET 0

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

#define MAX_ROW_SIZE 1000

#ifdef MINIVIM_LINUX
#   include <termios.h>
void die(const char *s) {
  perror(s);
  exit(1);
}
struct termios orig_termios;
void disable_raw_mode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
}
void enable_row_mode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
  atexit(disable_raw_mode);
  struct termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}
#endif

/**
 * INFO:
 * 1.) when we insert a new row, we just keep editing the current row
 * 2.) when we delete the row, we set ROW_FLAG_WRAPPED and empty the row
 */

struct row_t {
    int index;
    int seek_start;
    int seek_end;
    size_t size;
    int flags;
    char buffer[MAX_ROW_SIZE];
    bool free;
};

struct store_t {
    int cursorx;
    int cursory;
    int pagex;
    int pagey;
    int prev_pagex;
    int prev_pagey;
    int prev_pagey_pos;
    int next_pagey_pos;
    int toprow;
    int prev_toprow;
    int next_toprow;
    size_t filepos;
    int child_row_current;
    int prev_child_row_current;
    int prev2_child_row_current;
};

struct showed_row_t {
    int length;
    int index;
    int num;
};

/**
 * graphic terminal
 */
static char *colors;
static char *terminal;
static char *colors2;
static char *terminal2;
static int g_rows;
static int g_cols;
static int g_size;
static char filepath[200];
static char app_version[] = "0.0.1";

static fd_t fd;
static int state;
static bool app_quit;

/**
 * buffer
 */
static size_t buffer_size;
static char *buffer;
static size_t buffer_cursor;

static size_t filepos;

static int child_row_current;
static int prev_child_row_current;
static int prev2_child_row_current;

static int cursorx;
static int cursory;

static int pagex;
static int pagey;

static int prev_pagex;
static int prev_pagey;

static int prev_pagey_pos;
static int next_pagey_pos;

static int toprow = 0;

static int prev_toprow = 0;
static int next_toprow = 0;

static size_t rows_max_size;
static size_t rows_size;
static size_t available_rows_count_max;
static size_t available_rows_count;

static struct row_t **rows;
static struct row_t *available_rows;
static struct showed_row_t *showed_rows;
static struct row_t **available_rows_queue;
static size_t available_rows_queue_size;
static size_t available_rows_queue_index;
static const char err_free_rows_not_exists[] = "Free rows not exists";
static const char err_row_buffer_overflow[] = "Row buffer overflow";

static fd_t w_fd = 0;

void current_file_save();

void assert_void () {
    /** void */
}

void assert_fail (const char *expr, const char *file, int line, const char *func) {
    printf("assertion failed: %s. %s:%d in %s\n", expr, file, line, func);
    exit(-1);
}

void savestore (struct store_t *n) {
    n->child_row_current = child_row_current;
    n->cursorx = cursorx;
    n->cursory = cursory;
    n->filepos = filepos;
    n->next_pagey_pos = next_pagey_pos;
    n->next_toprow = next_toprow;
    n->pagex = pagex;
    n->pagey = pagey;
    n->prev2_child_row_current = prev2_child_row_current;
    n->prev_child_row_current = prev_child_row_current;
    n->prev_pagex = prev_pagex;
    n->prev_pagey = prev_pagey;
    n->prev_pagey_pos = prev_pagey_pos;
    n->prev_toprow = prev_toprow;
    n->toprow = toprow;
}

void restore(struct store_t *n) {
    child_row_current = n->child_row_current;
    cursorx = n->cursorx;
    cursory = n->cursory;
    filepos = n->filepos;
    next_pagey_pos = n->next_pagey_pos;
    next_toprow = n->next_toprow ;
    pagex= n->pagex;
    pagey=n->pagey;
    prev2_child_row_current=n->prev2_child_row_current;
    prev_child_row_current=n->prev_child_row_current;
    prev_pagex=n->prev_pagex;
    prev_pagey=n->prev_pagey;
    prev_pagey_pos=n->prev_pagey_pos;
    prev_toprow=n->prev_toprow ;
    toprow=n->toprow ;
}

void reverse(char *s) {
   char *j;
   int i = strlen(s);

   strcpy(j,s);
   while (i-- >= 0)
      *(s++) = j[i];
   *s = '\0';
}

/* converts an integer into a string */
void itoa(int n, char *buffer, int base) {
   char *ptr = buffer;
   int lowbit;

   base >>= 1;
   do
   {
      lowbit = n & 1;
      n = (n >> 1) & 32767;
      *ptr = ((n % base) << 1) + lowbit;
      if (*ptr < 10)
         *ptr +='0';
      else
         *ptr +=55;
      ++ptr;
   }
   while (n /= base);
   *ptr = '\0';
   reverse (buffer);   /* reverse string */
}

int g_min (int a, int b) {
    return a < b ? a : b;
}

int g_max (int a, int b) {
    return a > b ? a : b;
}

size_t sn_g_cursor_at (char *buffer, size_t buffer_size, int y, int x) {
#ifdef MINIVIM_LINUX
    return snprintf(buffer, buffer_size, "\033[%d;%dH", y+WH_OFFSET+1, x+WH_OFFSET+1);
#else
    snprintf(buffer, buffer_size, "\033[%d;%dH", y+WH_OFFSET, x+WH_OFFSET); return strlen(buffer);
#endif
}

void g_cursor_at (int y, int x) {
    char buffer2[64];
    buffer2[sn_g_cursor_at(buffer2, sizeof (buffer2), y, x)] = '\0';
    printf(buffer2);
}

void g_cursor_at_pos (int pos) {
    int rows = pos / (g_cols);
    g_cursor_at (rows, pos - rows * g_cols);
}

void m_init () {
    buffer = NULL;
    buffer_size = 0;
    buffer_cursor = 0;

    cursorx = 0;
    cursory = 0;

    app_quit = false;
    state = STATE_VIEW;

    fd = -1;
    filepos = 0;

    g_rows = 0;
    g_cols = 0;
    g_size = 0;

    pagex = 0;
    pagey = 0;

    prev_pagex = pagex;
    prev_pagey = pagey;

    prev_pagey_pos = 0;
    next_pagey_pos = -1;

    rows = NULL;
    available_rows = NULL;
    rows_max_size = 0;
    rows_size = 0;
    available_rows_count = 0;
    w_fd = 0;

    toprow = 0;
    prev_toprow = 0;
    next_toprow = -1;
    child_row_current = -1;
    prev_child_row_current = -1;
    prev2_child_row_current = -1;
}

/**
 * graphic check
 */
void g_check () {
    g_rows = 37 - WH_OFFSET * 2;
    g_cols = 100 - WH_OFFSET * 2;

    g_size = g_rows * g_cols;
    buffer_size = g_max(g_size, 500);
}

/**
 * graphic init
 */
void g_init () {
#ifdef MINIVIM_LINUX
    enable_row_mode();
#endif
    memset (terminal, ' ', g_size);
    memset (terminal2, ' ', g_size);
    for (int i =0, j = 0; i < g_size; i++, j+=2) {
        colors[j] = (char)COLOR_BG_BLACK;
        colors[j+1] = (char)COLOR_FG_WHITE;

        colors2[j] = (char)COLOR_BG_DEFAULT;
        colors2[j+1] = (char)COLOR_FG_DEFAULT;
    }

    g_cursor_at (0,0);
}


/**
 * clear screen
 */
void g_clear () {
    printf("\033[2J");
}

/**
 * smart flush
 */
void g_flush (bool rst) {
    static char buffer2[10000];
    int buffer2_cursor = 0;
    int buffer2_size = sizeof (buffer2);

#ifdef MINIVIM_LINUX
    printf("\x1b[?25l");
    printf("\x1b[H");
#endif


    int prev_cursorx = cursorx;
    int prev_cursory = cursory;

    int cursor_pos = cursorx+cursory*g_cols;

    char prev_cursor_color[2] = {colors[cursor_pos*2], colors[cursor_pos*2+1]};

    if (!rst) {
        colors[cursor_pos*2] = COLOR_BG_WHITE;
        colors[cursor_pos*2+1] = COLOR_FG_BLACK;
    }

    for (int i = 0, j = 0; i < g_size; i++, j+=2) {
        int y = i / (g_cols);
        int x = i - y * g_cols;


        if ((colors[j] != colors2[j])||(colors[j+1]!=colors2[j+1])||terminal[i] != terminal2[i]) {
            buffer2_cursor += sn_g_cursor_at (buffer2 + buffer2_cursor, buffer2_size - buffer2_cursor, y, x);

            snprintf(buffer2 + buffer2_cursor, buffer2_size - buffer2_cursor, "\033[%d;%dm", (int)colors[j], (int)colors[j+1]); buffer2_cursor += strlen(buffer2 + buffer2_cursor);
            colors2[j]=colors[j];
            colors2[j+1]=colors[j+1];
            buffer2_cursor += sn_g_cursor_at (buffer2 + buffer2_cursor, buffer2_size - buffer2_cursor, y, x);
            //printf(terminal[i]);
            /** snprintf -> void (not int) why? */
            snprintf(buffer2 + buffer2_cursor, buffer2_size - buffer2_cursor, "%c", terminal[i]); buffer2_cursor += strlen(buffer2 + buffer2_cursor);
            terminal2[i] = terminal[i];

            if (buffer2_cursor > buffer2_size / 2) {
                printf(buffer2);
                buffer2_cursor = 0;
                buffer2[0] = '\0';
            }
        }
    }
#ifdef MINIVIM_LINUX
    fflush(stdout);
    fflush(stdin);
#endif

    printf(buffer2);

    colors[cursor_pos*2] = prev_cursor_color[0];
    colors[cursor_pos*2+1] = prev_cursor_color[1];

    cursorx = prev_cursorx;
    cursory = prev_cursory;
#ifdef MINIVIM_LINUX
    fflush(stdout);
    fflush(stdin);
#endif

    g_cursor_at (cursory, cursorx);
#ifdef MINIVIM_LINUX
    printf("\x1b[?25l");
    printf("\x1b[H");
#endif
}

/**
 * force flush screen
 */
void g_flush_force () {
    g_clear();
    g_flush(true);
}

void g_set_bg (size_t index, char color) {
    colors[index*2] = color;
}

void g_set_fg (size_t index, char color) {
    colors[index*2+1] = color;
}

void g_set_line (int row, char c, char color[2]) {
    for (int i = 0; i < g_cols; i++) {
        int tindex = row * g_cols + i;
        terminal[tindex] = ' ';
        g_set_bg(tindex, color[0]);
        g_set_fg(tindex, color[1]);
    }
}

void g_empty_line (int row) {
    char color[2] = { COLOR_BG_BLACK, COLOR_FG_WHITE };
    g_set_line (row, ' ', color);
}

void show_message (const char *msg, size_t len) {
    g_empty_line (g_rows - 1);
    memcpy(terminal + (g_rows - 1) * g_cols, msg, g_min(len, g_cols));
}

int rows_bin_search (int index) {
    int l = 0;
    int r = rows_size;

    while (l + 1 < r) {
            int mid = (r + l) / 2;
            if (rows[mid]->index > index) {
                    r = mid;
            }
            else {
                    l = mid;
            }
    }

    while (l > 0 && rows[l - 1]->index == index) {
        l--;
    }

    return l;
}

int rows_find (int prow) {
    int findex = rows_bin_search (prow);

    if (findex >= 0 && findex < rows_size && rows[findex]->index == prow) {
        return findex;
    }

    return -1;
}

int rows_insert_n (struct row_t *row, int npos) {
    /** array insert (can be replaced with r/b tree) */
    int prow = row->index;
    /** approximate position determination */
    int findex = rows_bin_search (prow);

    if (findex < rows_size && rows[findex]->index < row->index) {
            findex++;
    }

    while (findex < rows_size && rows[findex]->index == row->index && npos--) {
            findex++;
    }

    if (rows_size == rows_max_size) {
        return -1;
    }

    memmove((rows) + (1 + findex), (rows) + findex, (rows_size - findex) * sizeof (*rows));

    rows[findex] = row;
    rows_size++;

    assert((!findex || (findex > 0 && rows[findex - 1]->index <= rows[findex]->index)));
    assert(((findex + 1 >= rows_size) || (rows[findex+1]->index >= rows[findex]->index)));

    return findex;
}
int rows_insert (struct row_t *row) {
    return rows_insert_n(row, -1);
}

void clear_content_editor () {
    char color[2] = {COLOR_BG_BLACK, COLOR_FG_WHITE};
    for (int i = 0; i < g_rows - 1; i++) {
        g_set_line (i, ' ', color);
    }
}

void detect_previous_frame () {
    int c_row = 0;
    int c_col = 0;
    int m_rows = g_rows - 1 + 1;
    int left = filepos;
    int step = 100;
    int ltoprow = 0;
    int lpagey_pos = 0;
    int prev_ltoprow = -1;
    bool skiprow = false;
    int local_child_row_current = prev_child_row_current;

    buffer_cursor = 0;

    if (pagey == 0) {
        prev_pagey_pos = 0;
        prev_toprow = 0;
        return;
    }

    prev_pagey_pos = left;
    int shift = g_min(left, step);

    while ((c_row < m_rows) || skiprow) {
        if (ltoprow != prev_ltoprow) {
            prev_ltoprow = ltoprow;

            if (c_row < m_rows) {
                int array_row_index = rows_find(toprow-ltoprow /** c_row + pagey * g_rows **/);
                if (array_row_index != -1) {
                    if (!(rows[array_row_index]->flags & ROW_FLAG_WRAPPED)) {
                        int lltoprow = toprow-ltoprow;
                        int maxsize = rows_size;
                        bool ex = local_child_row_current != -1;
                        if (ex) {
                            maxsize = g_min(local_child_row_current, maxsize);
                            local_child_row_current = -1;
                        }
                        int ind;
                        for (ind = array_row_index; ind < maxsize && rows[ind]->index == lltoprow; ind++) {
                            c_row++;

                            /** check */
                            if (c_row >= m_rows) {
                                break;
                            }
                        }
                        // calculate prev2_child_row_current
                        if (c_row >= m_rows) {
                            int cnt = c_row;
                            for (; ind < maxsize && rows[ind]->index == lltoprow; ind++) {
                                cnt++;
                            }
                            prev2_child_row_current = cnt - c_row + array_row_index;
                            prev_pagey_pos = rows[array_row_index]->seek_start;
                            // if (!ex) {
                            //     prev2_child_row_current--;
                            // }
                        }
                    }
                    ltoprow++;
                    skiprow = true;
                    continue;
                }
            }
        }

        int st = g_min(shift, buffer_size - buffer_cursor);

        left -= st;
        assert((left>=0));
        /** setup */
        lseek (fd, left, SEEK_SET);


        int rhs =0;
        while (rhs < st) {
            rhs += g_min(read (fd, buffer, st-rhs), st-rhs);
            assert ((rhs>=0&&"read failed"));

            if (!rhs) {
                break;
            }
        }

        if (!rhs) {
            break;
        }

        shift = g_min(left, step);

        for (int i = rhs - 1; i >= 0; i--) {
            if (buffer[i] == '\n') {
                if (skiprow) {
                    skiprow=false;
                }
                else {
                    prev_pagey_pos = g_max(left + i + 1, 0);
                    c_row++;
                    ltoprow++;
                    if (rows_find(toprow-ltoprow) != -1) {
                        left += (i);
                        break;
                    }
                }
                /** check */
                if (c_row >= m_rows) {
                    break;
                }
            }
        }

        lseek (fd, left, SEEK_SET);
    }
    if (toprow == 0) {
        prev_toprow = 0;
        prev_pagey_pos = 0;
    }
    else {
        prev_toprow = toprow - ltoprow + 1;
        assert((prev_toprow >= 0));
    }
    buffer_cursor = 0;
}

void fetch_current_frame_with_row (struct row_t *frow) {
    int c_row = 0;
    int c_col = 0;
    int m_rows = g_rows - 1;
    int eof = false;
    int buffer_current = 0;
    int ltoprow = 0;
    int while_prev_ltoprow = -1;
    bool last_row = true;
    buffer_cursor = 0;

    if (!frow) {
        /** clear screen */
        clear_content_editor ();
    }

    if (prev_pagey != pagey) {
        if (prev_pagey > pagey) {
            next_pagey_pos = filepos;
            next_toprow = toprow;


            if (prev_pagey_pos == -1 || prev_toprow == -1) {
                detect_previous_frame ();
            }

            filepos = prev_pagey_pos;
            toprow = prev_toprow;
            child_row_current = prev2_child_row_current;

            prev_pagey_pos = -1;
            prev_toprow = -1;
            prev2_child_row_current = -1;
            prev_child_row_current = -1;
        }
        else if (next_pagey_pos != -1) {
            prev_pagey_pos = filepos;
            prev_toprow = toprow;
            prev2_child_row_current = prev_child_row_current;

            filepos = next_pagey_pos;
            toprow = next_toprow;

            next_pagey_pos = -1;
            prev_child_row_current = -1;
        }
    }
    else {
        child_row_current = prev_child_row_current;
        prev_child_row_current = -1;
    }

    int c_filepos = filepos;
    lseek (fd, c_filepos, SEEK_SET);

    int max_size = lseek (fd, 0, SEEK_END);
    int left = max_size - c_filepos;
    assert((left >= 0));
    int nline = -1;

    int skipcols = pagex * g_cols;
    bool skiprow = false;
    int line_start_pos = c_filepos;
    int line_size = 0;
    bool edited_exists = true;

    lseek (fd, c_filepos, SEEK_SET);

    while ((c_row < m_rows || skiprow) && (edited_exists || !eof || buffer_current != buffer_cursor)) {
        nline = -1;

        if (while_prev_ltoprow != ltoprow) {
            while_prev_ltoprow = ltoprow;
            edited_exists = true;

            if (c_row < m_rows && (!frow || (frow && frow->index != ltoprow + toprow))) {
                int array_row_index = rows_find(ltoprow + toprow /** c_row + pagey * g_rows **/);
                if (array_row_index != -1) {
                    if (!(rows[array_row_index]->flags & ROW_FLAG_WRAPPED)) {
                        int lltoprow = rows[array_row_index]->index;
                        int lindex = 0;
                        if ((c_row == 0)) {
                            /** first row */
                            prev_child_row_current = child_row_current;
                            if (child_row_current != -1) {
                                lindex = child_row_current;
                            }
                            else {
                                lindex = array_row_index;
                            }
                        }
                        else {
                            lindex = array_row_index;
                        }
                        for (; lindex < rows_size && rows[lindex]->index == lltoprow && c_row < m_rows; lindex++) {
                            /** exists */
                            struct row_t *row = rows[lindex];

                            int s = row->size;
                            int off = g_min(s, pagex * g_cols);

                            s -= off;

                            memcpy (terminal + g_cols * c_row, row->buffer + off, g_min(s, g_cols));
                            showed_rows[c_row].length = s;
                            showed_rows[c_row].index = lltoprow;
                            showed_rows[c_row].num = lindex - array_row_index;

                            c_row++;

                            c_col = 0;
                        }
                        bool t = !(lindex < rows_size && rows[lindex]->index == lltoprow);

                        if (c_row < m_rows || t) {
                            if (!left && child_row_current != -1) {
                                c_filepos = rows[array_row_index]->seek_start;
                                left = max_size - c_filepos + 1;
                                line_start_pos = c_filepos;
                                line_size=0;
                                assert((left >= 0));
                            }
                            if (c_row == m_rows && t) {
                                child_row_current = -1;
                            }
                        }
                        else {
                            child_row_current = lindex;
                            int a = 0;
                            next_toprow = toprow + ltoprow;
                            next_pagey_pos = c_filepos;
                            continue;
                        }
                    }
                    else {
                        /** row not exists **/
                    }

                    skiprow = true;
                    last_row = false;
                }
            }
        }
        else {
            edited_exists = false;
        }

        int st = g_min(buffer_size - buffer_cursor, left);
        assert((st>=0&&"BUG"));
        int rhs = 0;
        if (st) {
            rhs = read (fd, buffer + buffer_cursor, st);
            rhs = g_min(rhs, st);
            assert ((rhs >= 0 && "read failed"));
            if (rhs==0) {
                /** eof */
                eof = true;
                if (!frow) {
                    next_pagey_pos = -1;
                }
            }
            left -= rhs;
            assert((left >= 0));
            c_filepos += rhs;
            assert((c_filepos<=max_size&&"position > max_size"));
            lseek(fd, c_filepos, SEEK_SET);
            buffer_cursor += rhs;
        }

        if (!left) {
            eof=true;
            if (!frow) {
                next_pagey_pos = -1;
            }
        }

        for (int i = buffer_current; i < buffer_cursor; i++) {
            if (buffer[i] == '\n') {
                /** new row */
                nline = i+1;
                break;
            }
        }

        if (nline!=-1) {
            bool nskiprow = false;
            line_size += (nline-buffer_current);
            if (skiprow) {
                ltoprow++;
                nskiprow = true;
                skiprow = false;
            }
            else {
                if (frow) {
                    if (frow->index == ltoprow + toprow/**pagey * g_rows + c_row **/) {
                        int s = nline-buffer_current-1;
                        memcpy (frow->buffer + frow->size, buffer+buffer_current, s);
                        frow->size += s;

                        frow->seek_start = line_start_pos;
                        frow->seek_end = frow->seek_start + frow->size;
                    }
                    c_row ++;
                    ltoprow++;
                }
                else {
                    int avaialable = g_cols - c_col;
                    int off = g_min(skipcols, nline-buffer_current-1);
                    buffer_current+=off;
                    skipcols -= off;
                    int s = g_min(avaialable, nline-buffer_current-1);
                    memcpy (terminal + g_cols * c_row + c_col, buffer+buffer_current, s);
                    /* set row length */
                    showed_rows[c_row].length = c_col + s;
                    showed_rows[c_row].num = 0;
                    showed_rows[c_row].index = ltoprow + toprow;
                    c_row ++;
                    ltoprow++;
                    last_row = false;
                }

                if (c_row == m_rows) {
                    child_row_current=-1;
                }
            }

            line_start_pos += line_size;
            line_size=0;

            c_col = 0;

            if (!frow) {
                next_toprow = toprow + ltoprow;
                next_pagey_pos = c_filepos - buffer_cursor + nline;
            }

            buffer_current = nline;
            skipcols = pagex * g_cols;

            if (buffer_current>=buffer_cursor) {
                if (!nskiprow && eof) {
                    last_row=true;
                }
                buffer_current = 0;
                buffer_cursor = 0;
            }

            if (frow && frow->index == /**pagey * g_rows + c_row**/ ltoprow + toprow - 1) {
                goto end;
            }

            continue;
        }

        line_size += (buffer_cursor - buffer_current);

        if (frow) {
            if (frow->index == ltoprow+toprow/* pagey * g_rows + c_row */) {
                int s = buffer_cursor-buffer_current;
                memcpy (frow->buffer + frow->size, buffer+buffer_current, s);
                frow->size += s;
            }
            last_row = true;
        }
        else if (skiprow) {

        }
        else {
            int avaialable = g_cols - c_col;
            int off = g_min(skipcols, buffer_cursor-buffer_current);
            buffer_current+=off;
            skipcols -= off;
            int s = g_min(avaialable, buffer_cursor-buffer_current);
            memcpy (terminal + g_cols * c_row + c_col, buffer+buffer_current, s);
            c_col += s;
            last_row = true;
        }


        buffer_cursor = 0;
        buffer_current = 0;
    }

    if (last_row) {
        if (frow) {
            frow->seek_start = line_start_pos;
            frow->seek_end = frow->seek_start + frow->size;
        }
        else {
            /** last rows */
            showed_rows[c_row].length = c_col;
            showed_rows[c_row].num = 0;
            showed_rows[c_row].index = ltoprow + toprow;

        }
        c_row++;
        ltoprow++;
    }

    if (!frow) {
        /** empty rows */
        for (; c_row < g_rows - 1; c_row++) {
            showed_rows[c_row].length = 0;
            showed_rows[c_row].index = -1;
            showed_rows[c_row].num = 0;

            next_pagey_pos = -1;
            next_toprow = 0;
        }

        prev_pagey = pagey;
        prev_pagex = pagex;
    }

    end: buffer_cursor = 0;
}

void fetch_current_frame () {
    fetch_current_frame_with_row(NULL);
}

bool input_move (int c) {
    bool refetch = false;
    bool rr = false;
    switch (c)
    {
    case CODE_LEFT_ARROW:
    case 'a':
        if (cursorx) {
            cursorx--;
        }
        else {
            if (pagex) {
                pagex--;
                cursorx = g_cols - 1;
                refetch = true;
            }
        }
        break;
    case CODE_RIGHT_ARROW:
    case 'd':
        if (cursorx+1 < g_cols) {
            cursorx++;
        }
        else {
            pagex++;
            cursorx = 0;
            refetch = true;
        }
        break;
    case 'w':
    case CODE_UP_ARROW:

    rr = true;
        if (cursory) {
            cursory--;
        }
        else {
            if (pagey) {
                cursory = g_rows - 1 - 1;
                pagey--;
                refetch = true;
            }
        }
        break;
    case 's':
    case CODE_DOWN_ARROW:
    rr = true;
        if (cursory+1 < g_rows - 1) {
            cursory++;
        }
        else if (next_pagey_pos >= 0) {
            cursory = 0;
            pagey++;
            refetch = true;
        }
        break;
    default:
        return false;
    }

    if (refetch) {
        fetch_current_frame ();
        cursorx = g_min(cursorx, showed_rows[cursory].length);
        g_cursor_at(cursory, cursorx);
    }
    else {
        cursorx = g_min(cursorx, showed_rows[cursory].length);
        g_cursor_at(cursory, cursorx);
    }

    if (rr) {
        snprintf (buffer, buffer_size, "index: %d num: %d size: %d", showed_rows[cursory].index, showed_rows[cursory].num, showed_rows[cursory].length);
        show_message (buffer, strlen(buffer));
    }

    return true;
}

void apply_command (int flg) {
    if ((flg & CMD_FLAG_WRITE)) {
        /** write */
        current_file_save ();
    }
    if ((flg & CMD_FLAG_QUIT)) {
        if (rows_size > 0) {
            if ((flg & CMD_FLAG_FORCE)) {
                app_quit = true;
            }
        }
        else {
            app_quit = true;
        }
    }
}

void input_state_cmd (int c) {
    if (c == CODE_BACKSPACE || c == CODE_DELETE) {
        /** pop */
        if (buffer_cursor) {
            terminal[g_cols*(g_rows-1)+(--buffer_cursor)] = ' ';
        }

        if (buffer_cursor==0) {
            state = STATE_VIEW;
        }

        for (int i = buffer_cursor; i < g_cols; i++) {
            terminal[g_cols*(g_rows-1)+i] = ' ';
        }

        return;
    }

    if (!(c >= 32 && c <= 126)) {
        if (c == CODE_ENTER || c == '\r') {
            /** apply command */
            int cmd_flg = 0;
            for (int i = 1; i < buffer_cursor; i++) {
                switch (buffer[i])
                {
                case '!':
                    cmd_flg |= CMD_FLAG_FORCE;
                    break;
                case 'w':
                    cmd_flg |= CMD_FLAG_WRITE;
                    break;
                case 'q':
                    cmd_flg |= CMD_FLAG_QUIT;
                    break;
                default:
                    break;
                }
            }
            apply_command (cmd_flg);
            /** disable command mode */
            buffer_cursor = 0;
            state = STATE_VIEW;
            g_empty_line(g_rows-1);
        }
        else if (c == CODE_ESC) {
            /** disable command mode */
            buffer_cursor = 0;
            state = STATE_VIEW;
            g_empty_line(g_rows-1);
        }
        else {
            /** invalid symbol */
        }
        return;
    }

    /** state edit */
    if (buffer_cursor >= MAX_CMD_SIZE) {
        /** no space left */
        return;
    }

    terminal[g_cols*(g_rows-1)+buffer_cursor] = (char)c;
    buffer[buffer_cursor++] = (char)c;
    for (int i = buffer_cursor; i < g_cols; i++) {
        terminal[g_cols*(g_rows-1)+i] = ' ';
    }
}

void input_state_view (int c) {
    /** state view */
    switch (c)
    {
    case ':': {
        /* enter command */
        state = STATE_CMD;
        input_state_cmd (c);
        break;
    }
    case 'i': {
        /* edit mode */
        state = STATE_EDIT;
        break;
    }
    default:
        if (input_move (c)) {}
        break;
    }
}

struct row_t *get_available_row_storage () {
    if (available_rows_queue_index < available_rows_queue_size) {
        struct row_t *p = available_rows_queue[available_rows_queue_index++];

        p->size = 0;
        p->seek_start = 0;
        p->seek_end = 0;
        p->flags = 0;
        p->index = 0;
        p->free = false;
        return p;
    }

    return NULL;
}

void fetch_row (struct row_t *row) {
    fetch_current_frame_with_row (row);
    row->buffer[row->size] = '\0';
    if (row->size == 0) {
        int a =0;
    }
}

int get_row_storage_with_state (int row, bool *inserted) {
    if (inserted) {
        *inserted = false;
    }

    int index = rows_find (row);
    if (index != -1) {
        return index;
    }

    struct row_t *p = get_available_row_storage();
    if (p) {
        p->index = row;
        index = rows_insert (p);
        if (index == -1) {
            /** no left space */
            return -1;
        }

        fetch_row (p);

        if (inserted) {
            *inserted = true;
        }
    }
    return index;
}

int get_row_storage (int row) {
    return get_row_storage_with_state(row, NULL);
}

void ret_row_storage (struct row_t *row) {
    row->free = true;
    row->seek_end = 0;
    row->seek_start = 0;
    row->flags = 0;
    row->seek_end =0;
    row->seek_start = 0;
    row->index = 0;
    row->size = 0;
    available_rows_queue[--available_rows_queue_index] = row;
}

void current_file_save () {
    char buff[200];
    int index = 0;
#ifdef MINIVIM_FILESYSTEM
    do {
        assert((index < 10 && "failed to save file"));
        snprintf(buff, sizeof (buff), "%s.%d.tmp", filepath, index++);
    }
    while (((w_fd = creat (buff, 0755)) <= 0));
    close(w_fd);

    w_fd = open(buff, O_WRONLY|O_CREAT|O_TRUNC, 0755);
#ifdef MINIVIM_LINUX
    if (w_fd < 0) {
        int err = errno;
        printf("errno: %d\n", err);
    }
#endif
    assert((w_fd > 0));

    size_t r_filepos = 0;
    size_t w_filepos = 0;
    buffer_cursor = 0;
    int filesize = lseek(fd, 0, SEEK_END);
    lseek (fd, r_filepos, SEEK_SET);
    index = 0;

    while (true)
    {
        int s = buffer_size;
        if (index < rows_size) {
            s = g_min(s,  rows[index]->seek_start - r_filepos);
            if (s==0) {
                bool parent = true;
                int seek_end = rows[index]->seek_end;
                int row_index = rows[index]->index;

                if (!(rows[index]->flags & ROW_FLAG_WRAPPED)) {
                    do {
                        if (!parent) {
                            assert((write (w_fd, "\n", sizeof ("\n") - 1) >= 0 && "write failed"));
                        }
                        int cursor = 0;
                        while (cursor < rows[index]->size) {
                            int wrhs = write (w_fd, rows[index]->buffer + cursor, rows[index]->size - cursor);
                            wrhs = g_min(wrhs, rows[index]->size - cursor);
                            cursor += wrhs;
                            assert((wrhs >= 0 && "Write failed"));
                        }
                        index++;
                        parent=false;
                    }
                    while (index < rows_size && row_index == rows[index]->index);

                    if (rows_size > index && rows[index]->seek_start == rows[index-1]->seek_end + 1 && (rows[index]->flags & ROW_FLAG_WRAPPED)) {
                        while (index < rows_size && (rows[index]->flags & ROW_FLAG_WRAPPED)) {
                            seek_end=rows[index]->seek_end;
                            index++;
                        }
                    }
                }
                else {
                    /** row was removed */
                    index++;
                    seek_end++;
                }

                r_filepos = g_min(seek_end, filesize);
                lseek (fd, r_filepos, SEEK_SET);


                continue;
            }
        }
        int rhs = read (fd, buffer, s);
        // bug
        rhs = g_min(rhs, s);
        assert((rhs >= 0));
        if (rhs == 0) {
            break;
        }
        r_filepos += rhs;
        lseek (fd, r_filepos, SEEK_SET);

        int cursor = 0;
        while (cursor < rhs) {
            int wrhs = write (w_fd, buffer + cursor, rhs - cursor);
            wrhs = g_min(wrhs, rhs - cursor);
            assert((wrhs >= 0 && "Write failed"));
            cursor += wrhs;
        }
    }

    for (int i = rows_size - 1; i >= 0; i--) {
        ret_row_storage(rows[i]);
    }

    rows_size = 0;

    close (fd);
    close (w_fd);

    pagex = 0;
    pagey = 0;
    cursorx = 0;
    cursory = 0;
    filepos = 0;
    buffer_cursor = 0;
    w_fd = 0;
    fd = 0;
    toprow = 0;
    prev_child_row_current = -1;
    prev2_child_row_current = -1;
    child_row_current = -1;
    next_toprow = -1;
    prev_toprow = -1;
    next_pagey_pos = -1;
    prev_pagey_pos = -1;

    assert((rename(buff, filepath) == 0 && "rename failed"));
    fd = open (filepath, O_RDONLY|O_CREAT, 0755);

    fetch_current_frame ();
#endif
}

void input_state_edit (int c) {
    const char *err_msg = NULL;
    int prow = showed_rows[cursory].index;
    int pcol = pagex*g_cols + cursorx;

    bool refresh = false;
    struct row_t *row;

    /** state edit */
    if (c == CODE_ESC) {
        /** disable edit mode */
        state = STATE_VIEW;
        buffer_cursor = 0;
    }

    else if (prow >= 0) {

        if (c == CODE_DELETE || c == CODE_BACKSPACE) {
            int row_index = get_row_storage(prow);
            if (row_index!=-1) {

                assert(rows[row_index]->index == prow);

                int num = showed_rows[cursory].num;
                row_index += num;
                row=rows[row_index];
                int index = cursorx + pagex * g_cols;
                if (index) {
                    memmove(row->buffer + index - 1, row->buffer + index, row->size - index);
                    row->size --;

                    refresh = true;
                    showed_rows[cursory].length = g_min(g_cols, row->size);

                    input_move (CODE_LEFT_ARROW);
                }
                else {
                    bool res2 = row->index > 0;
                    bool res1 = num > 0 || (res2 && (rows_size > row_index + 1 && rows[row_index + 1]->index == row->index));

                    int xpos = g_cols - 1;
                    int top_row_index = -1;

                    if (res1||res2) {
                        if (row_index - 1 >= 0 && rows[row_index-1]->index == row->index) {
                            // child
                            top_row_index = row_index - 1;
                        }
                        else if (row->index > 0) {
                            // top row (top index)
                            int ri = row->index - 1;
                            bool inserted = false;
                            do {
                                struct store_t store;
                                bool store1 = cursory == 0;
                                if (store1) {
                                    /* on another page */
                                    savestore(&store);
                                    pagex=0;
                                    pagey--;
                                }

                                top_row_index = get_row_storage_with_state(ri, &inserted);

                                if (store1) {
                                    restore(&store);
                                }

                                if (top_row_index == -1) {
                                    err_msg = err_free_rows_not_exists;
                                    goto err;
                                }
                            }
                            while ((rows[top_row_index]->flags & ROW_FLAG_WRAPPED) && (--ri) >= 0);
                            if (ri < 0) {
                                assert((ri >= 0 && "failed to find (unreachable, because we can't delete the first row)"));
                            }
                            // grab last child
                            while (top_row_index + 1 < rows_size && !(rows[top_row_index+1]->flags & ROW_FLAG_WRAPPED) && rows[top_row_index+1]->index == rows[top_row_index]->index) {
                                top_row_index++;
                            }
                            if (inserted && top_row_index <= row_index) {
                                row_index++;
                            }
                        }
                        else {
                            assert((top_row_index != -1 && "bug: failed to find top row."));
                        }

                        struct row_t *ltoprow = rows[top_row_index];
                        int s = g_min(ltoprow->size + row->size, MAX_ROW_SIZE) - ltoprow->size; /** buffer overflow */
                        memcpy (ltoprow->buffer + ltoprow->size, row->buffer, s);
                        xpos = g_min(ltoprow->size, xpos);
                        ltoprow->size += s;
                        row->size = 0;
                    }

                    /** delete row */
                    if (res1) {
                        /** row is a child or has a child  */

                        bool flg = true;

                        if (row->seek_start>=0 || row->seek_end>=0) {
                            /** the row is the parent */
                            if (row_index + 1 < rows_size && rows[row_index+1]->index == row->index) {
                                rows[row_index+1]->seek_start = row->seek_start;
                                rows[row_index+1]->seek_end = row->seek_end;
                            }
                            else {
                                flg = false;
                            }
                        }

                        if (flg) {
                            if (rows_size > row_index + 1) {
                                memmove ((rows) + row_index, rows + (row_index + 1), sizeof (*rows) *(rows_size - (row_index + 1)));
                            }
                            rows_size--;

                            ret_row_storage(row);
                            row = NULL;
                        }
                        else {
                            row->flags |= ROW_FLAG_WRAPPED;
                            row->size = 0;
                        }

                        fetch_current_frame();

                        cursorx = xpos;
                        input_move(CODE_UP_ARROW);
                    }
                    else if (res2) {
                        /** no childs */
                        row->flags |= ROW_FLAG_WRAPPED;
                        row->size = 0;

                        fetch_current_frame();
                        cursorx = xpos;
                        input_move(CODE_UP_ARROW);
                    }
                }
            }
        }
        else if (c >= 32 && c <= 126) {
            int row_index = get_row_storage(prow);
            if (row_index!=-1) {
                assert(rows[row_index]->index == prow);
                // snprintf (buffer, buffer_size, "row_index: %d num: %d rows_size: %d", row_index, showed_rows[cursory].num, rows_size);
                // show_message (buffer, strlen(buffer));
                row_index += showed_rows[cursory].num;
                assert((row_index < rows_size));
                row=rows[row_index];
                int index = cursorx + pagex * g_cols;
                memmove(row->buffer + index + 1, row->buffer + index, row->size - index);
                row->buffer[index] = c;
                row->size ++;

                refresh = true;
                showed_rows[cursory].length = g_min(g_cols, g_max(0, row->size - g_cols * pagex));

                input_move (CODE_RIGHT_ARROW);
            }
            else {
                err_msg = err_free_rows_not_exists;
                goto  err;
            }
        }
        else if (c == CODE_ENTER || c == '\r') {
            /** new line */
            int row_index = get_row_storage(prow);
            if (row_index!=-1) {
                int num = showed_rows[cursory].num;
                row_index = row_index + num;
                assert(rows[row_index]->index == prow);
                assert(((row_index) < rows_size));
                struct row_t *parent = rows[row_index];

                row = get_available_row_storage();
                if (!row) {
                    /** free rows not exists right now */
                    err_msg = err_free_rows_not_exists;
                    goto err;
                }
                row->index = parent->index;
                row->flags = 0;
                row->size = 0;
                row->seek_end = -1;
                row->seek_start = -1;

                int new_row_index;

                if (rows_size > row_index + 1 && (rows[row_index+1]->flags & ROW_FLAG_WRAPPED)) {
                    new_row_index = row_index + 1;
                    rows[new_row_index]->flags = rows[new_row_index]->flags ^ ROW_FLAG_WRAPPED;
                }
                else {
                    new_row_index = rows_insert_n(row, num+1);
                }

                row=rows[new_row_index];

                // for (int trow = cursory + 1; trow < g_rows && showed_rows[trow].index == row->index; trow++) {
                //     showed_rows[trow].num++;
                // }

                // for (int trow = g_rows - 3; trow > cursory; trow--) {
                //     //memmove(terminal + (trow+1) * g_cols, terminal + (trow) * g_cols, g_cols);
                //     //memcpy (showed_rows + trow + 1, showed_rows + trow, sizeof (struct showed_row_t));
                // }


                int x = cursorx+pagex*g_cols;
                int s = g_max(0, parent->size - x);
                memcpy (row->buffer, parent->buffer + x, s);
                // memset (terminal + g_cols * cursory + cursorx, ' ', g_cols - cursorx);

                row->size = s;
                parent->size -= s;

                // showed_rows[cursory].length = g_max(0, showed_rows[cursory].length - s);
                // if (cursory + 1 != g_rows - 1) {
                //     showed_rows[cursory + 1].index = row->index;
                //     showed_rows[cursory + 1].length = g_min(g_cols, g_max(0, row->size - g_cols * pagex));
                //     showed_rows[cursory + 1].num = num;
                // }
                fetch_current_frame();


                pagex = 0;
                cursorx = 0;

                input_move (CODE_DOWN_ARROW);
                refresh = true;

                fetch_current_frame();
            }
        }

        err: if (err_msg) {
            show_message(err_msg, strlen(err_msg));
            refresh = false;
            fetch_current_frame();
        }
    }

    if (refresh) {
        int s = row->size;
        int off = g_min(s, pagex * g_cols);

        s -= off;
        s = g_min(s, g_cols);

        memcpy (terminal + g_cols * cursory, row->buffer + off, s);
        memset (terminal + g_cols * cursory + s, ' ', g_cols - s);
    }
}

void input_event_switch (int c) {
    switch (state)
    {
    case STATE_VIEW:
        input_state_view (c);
        break;
    case STATE_EDIT:
        input_state_edit (c);
        break;
    case STATE_CMD:
        input_state_cmd (c);
        break;
    default:
        break;
    }
}

void input_event (int c) {
    if (c == '\t') {
        for (int i = 0; i < 4; i++) {
            input_event_switch (' ');
        }
        return;
    }
    input_event_switch (c);
}

void clean_up () {
    for (int i = 0; i < g_rows; i++) {
        g_empty_line(i);
    }
    g_flush(true);
    g_cursor_at(0,0);

    if (fd>=0) {
        close(fd);
        fd=-1;
    }
}

void cdie () {
    clean_up();
    exit(-1);
}

void quit() {
    clean_up();
    exit(0);
}

void init_params (int argc, const char *argv[]) {
#define OPTION_HELP 1
#define OPTION_VERSION 2
    int option = 0;

    for (int i = 1; i < argc; i++) {
        int flg = option == 0 ? 0 : 3;
        size_t size = strlen(argv[i]);

        for (int j = 0; j < size; j++) {
            if (flg==2||flg==3) {
                /** get full option name (2) or full option value (3) (-vhello -> name: v, value: hello) */
                memcpy (buffer, (&(argv[i][0]))+j, size - j);
                buffer_cursor = size - j;
                break;
            }

            if (flg==4) {
                /** without value */
                printf("there are exists option without value\n");
                exit(-1);
            }

            if (argv[i][j] == '-') {
                flg++;
            }
            else if (flg==0) {
                /** default value */
                assert((size <= sizeof (filepath) && "default option buffer overflow"));
                memcpy ((char *)&filepath, argv[i], size);
                filepath[size]='\0';
                break;
            }
            else {
                switch (argv[i][j])
                {
                    case 'h':
                        option = OPTION_HELP;
                        /** without value */
                        flg = 4;
                    break;
                    case 'v':
                        option = OPTION_VERSION;
                        flg = 4;
                    break;
                    default:
                        printf ("undefined option: -%c\n", argv[i][j]);
                        exit(-1);
                }
            }
        }

        if (flg == 2) {
            /** need to grab full option value if needed */
            buffer[buffer_cursor]='\0';
            if (strcmp ((const char *)buffer, "help")==0) {
                option = OPTION_HELP;
                flg = 4;
            }
            else if (strcmp ((const char *)buffer, "version")==0) {
                option = OPTION_VERSION;
                flg = 4;
            }
            else {
                printf("invalid option: %s (%d)\n", (const char *)buffer, buffer_cursor);
                exit(-1);

            }
        }

        if (flg == 3) {
            /** name -> value */
            continue;
        }

        if (flg == 4) {
            /** name -> null */
            switch (option)
            {
                case OPTION_HELP:
                    printf("Usage: minivim [options...] <filepath>\n");
                    printf("-h, --help                        Get help for commands\n");
                    printf("-v, --version                     Show version number and quit\n");
                    printf("\nAuthor: Xiadnoring | 2025. Developed for Mountain Tech OS\n");
                    exit(0);
                break;
                case OPTION_VERSION:
                    printf("Version: %s\n", app_version);
                    exit(0);
                break;
            }
            option = 0;
            continue;
        }
    }
#undef OPTION_VERSION
#undef OPTION_HELP

    if (!strlen((const char *)(&filepath))) {
        printf("no input file\n");
        exit(-1);
    }

    fd = open ((const char *)(&filepath), O_RDWR);
    if (fd < 0) {
        fd = creat ((const char *)(&filepath), 0755);
        if (fd < 0) {
            printf ("failed to open the file %s. fd: %d\n", (const char *)(&filepath), (int)fd);
            exit(-1);
        }
    }
}

#ifdef MINIVIM_LINUX
void refresh_screen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
}
#endif


#ifdef MINIVIM_LINUX
int read_code () {
    int nread;
    char c;

    int prevX = cursorx;
    int prevY = cursory;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
            if (seq[2] == '~') {
            switch (seq[1]) {
                case '1': return HOME_KEY;
                case '3': return DEL_KEY;
                case '4': return END_KEY;
                case '5': return PAGE_UP;
                case '6': return PAGE_DOWN;
                case '7': return HOME_KEY;
                case '8': return END_KEY;
            }
            }
        } else {
            switch (seq[1]) {
            case 'A': return ARROW_UP;
            case 'B': return ARROW_DOWN;
            case 'C': return ARROW_RIGHT;
            case 'D': return ARROW_LEFT;
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }
        } else if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
        }
        }
        return '\x1b';
    } else {
        return c;
    }
}
#else
int read_code () {
    int c = getchar ();
    assert (c <= 127);
    return c;
}
#endif

int main(int argc, char const *argv[]) {
#ifdef MINIVIM_LINUX
    static char tbuffer[8192];
    setvbuf(stdout, tbuffer, _IOFBF, sizeof(tbuffer));
#endif

    m_init();

    /* get graphic size*/
    g_check();

    buffer_size = g_max(37*100, 500);
    static char buffer_[37*100];
    static char terminal_[37*100];
    static char terminal2_[37*100];
    static char colors_[37*100*2];
    static char colors2_[37*100*2];
    /** we can edit only [n] rows */
    static struct row_t available_rows2[1000];
    static struct row_t *available_rows_queue2[1000];
    available_rows_count = sizeof (available_rows2) / sizeof (struct row_t);
    available_rows_count_max = available_rows_count;
    available_rows_queue_index = 0;
    available_rows_queue_size = available_rows_count;
    /** we can handle only 'rows_count' rows */
    rows_max_size = 1000;
    rows_size = 0;
    static struct row_t *rows2[1000];
    static struct showed_row_t showed_rows2[37];

    rows = (struct row_t **)&rows2;
    available_rows_queue = (struct row_t **)&available_rows_queue2;
    available_rows = (struct row_t *)&available_rows2;
    buffer = (char*)&buffer_;
    terminal = (char*)&terminal_;
    terminal2 = (char*)&terminal2_;
    colors = (char*)&colors_;
    colors2 = (char*)&colors2_;
    showed_rows = (struct showed_row_t *)&showed_rows2;

    /** fill **/
    for (int i = 0; i < available_rows_count; i++) {
        available_rows_queue2[i] = &available_rows2[i];
    }
    memset(available_rows, '\0', available_rows_count * sizeof (struct row_t));
    memset(showed_rows, '\0', g_rows * sizeof (struct showed_row_t));
    memset(rows, '\0', rows_max_size * sizeof (struct row_t*));

    init_params (argc, argv);

    /** init graphic */
    g_init();
    g_flush_force();

    fetch_current_frame();

    int cnt = 0;

    while (!app_quit) {
        /** main loop */
        g_flush(false);
        /** main loop */

        int c = read_code();
        input_event (c);
    }

    /** clean up */
    clean_up();

    return 0;
}
