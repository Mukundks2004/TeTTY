#include <curses.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define BOARD_HEIGHT 20
#define ARR_HEIGHT 40
#define BOARD_WIDTH 10
#define WIDTH 38 + 7 + 1 + BOARD_WIDTH * 2 + 1 + 9
#define HEIGHT BOARD_HEIGHT + 6
#define RIGHT_MARGIN 46

#define SPAWN_X 4
#define SPAWN_Y 19
#define SPAWN_ROT 0
#define FPS 60
#define DAS 5
#define CLEAR_GOAL 40
#define QUEUE_SZ 5
#define BAG_SZ 7
#define KEYS 10

#define LEFT 0
#define RIGHT 1
#define SD 2
#define HD 3
#define CCW 4
#define CW 5
#define FLIP 6
#define HOLD 7
#define RESET 8
#define QUIT 9

#define COLOR_ORANGE 8

enum Parser {
    CODE,
    MOD,
    STATE,
    END,
    INVALID,
};

enum InputMode {
    EXTKEYS,
    SCANCODES,
    NORM
};

// extended keyboard protocol keycodes
const uint32_t keys[KEYS] = {
    KEY_LEFT,  // Left  | ←
    KEY_RIGHT, // Right | →
    KEY_DOWN,  // SD    | ↓
    ' ',       // HD    | Space
    'a',       // CCW   | a
    's',       // CW    | s
    'd',       // 180   | d
    57441,     // Hold  | shift
    'r',       // Reset | r
    'q'        // Quit  | q
};

// scancodes
const uint8_t keys2[KEYS] = {
    0x4b, // Left  | ←
    0x4d, // Right | →
    0x50, // SD    | ↓
    0x39, // HD    | Space
    0x1e, // CCW   | a
    0x1f, // CW    | s
    0x20, // 180   | d
    0x2a, // Hold  | z
    0x13, // Reset | r
    0x10  // Quit  | q
};

typedef struct Piece {
    int8_t x;
    int8_t y;
    int8_t coords[4][2];
    uint8_t type;
    uint8_t rot;
} Piece;

// TODO: figure out better way to store this
// Defined by offset from the piece center
// 7 pieces, 4 rotations, 3 coordinate pairs
const int8_t pieces[BAG_SZ][4][4][2] = {
    // I
    {
        {{-1, 0}, {0, 0}, {1, 0}, {2, 0}},
        // []<>[][]
        {{0, -1}, {0, 0}, {0, 1}, {0, 2}},
        // []
        // <>
        // []
        // []
        {{-2, 0}, {-1, 0}, {0, 0}, {1, 0}},
        // [][]<>[]
        {{0, -2}, {0, -1}, {0, 0}, {0, 1}},
        // []
        // []
        // <>
        // []
    },
    // J
    {
        {{-1, -1}, {-1, 0}, {0, 0}, {1, 0}},
         // []
         // []<>[]
        {{0, -1}, {1, -1}, {0, 0}, {0, 1}},
         // [][]
         // <>
         // []
        {{-1, 0}, {0, 0}, {1, 0}, {1, 1}},
         // []<>[]
         //     []
        {{0, -1}, {0, 0}, {-1, 1}, {0, 1}},
         //   []
         //   <>
         // [][]
    },
    // L
    {
        {{1, -1}, {-1, 0}, {0, 0}, {1, 0}},
        //     []
        // []<>[]
        {{0, -1}, {0, 0}, {0, 1}, {1, 1}},
        // []
        // <>
        // [][]
        {{-1, 0}, {0, 0}, {1, 0}, {-1, 1}},
        // []<>[]
        // []
        {{-1, -1}, {0, -1}, {0, 0}, {0, 1}},
        // [][]
        //   <>
        //   []
    },
    // O
    {
        {{0, -1}, {1, -1}, {0, 0}, {1, 0}},
        // [][]
        // <>[]
        {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
        // <>[]
        // [][]
        {{-1, 0}, {0, 0}, {-1, 1}, {0, 1}},
        // []<>
        // [][]
        {{-1, -1}, {0, -1}, {-1, 0}, {0, 0}}
        // [][]
        // []<>
    },
    // S
    {
        {{0, -1}, {1, -1}, {-1, 0}, {0, 0}},
        //   [][]
        // []<>
        {{0, -1}, {0, 0}, {1, 0}, {1, 1}},
        // []
        // <>[]
        //   []
        {{0, 0}, {1, 0}, {-1, 1}, {0, 1}},
        //   <>[]
        // [][]
        {{-1, -1}, {-1, 0}, {0, 0}, {0, 1}}
        // []
        // []<>
        //   []
    },
    // T
    {
        {{0, -1}, {-1, 0},{0, 0},  {1, 0}},
        //   []
        // []<>[]
        {{0, -1}, {0, 0}, {1, 0}, {0, 1}},
        // []
        // <>[]
        // []
        {{-1, 0}, {0, 0}, {1, 0}, {0, 1}},
        // []<>[]
        //   []
        {{0, -1}, {-1, 0}, {0, 0}, {0, 1}}
        //   []
        // []<>
        //   []
    },
    // Z
    {
        {{-1, -1}, {0, -1}, {0, 0}, {1, 0}},
        // [][]
        //   <>[]
        {{1, -1}, {0, 0}, {1, 0}, {0, 1}},
        //   []
        // <>[]
        // []
        {{-1, 0}, {0, 0}, {0, 1}, {1, 1}},
        // []<>
        //   [][]
        {{0, -1}, {-1, 0}, {0, 0}, {-1, 1}}
        //   []
        // []<>
        // []
    },
};

// 3 offset 'classes', 4 rotation states, 5 x & y offsets
const int8_t offsets[3][4][5][2] = {
    // J, L, S, T, Z
    {
        // Spawn
        {{ 0, 0}, { 0, 0}, { 0, 0}, { 0, 0}, { 0, 0}},
        // CW
        {{ 0, 0}, { 1, 0}, { 1,-1}, { 0, 2}, { 1, 2}},
        // 180
        {{ 0, 0}, { 0, 0}, { 0, 0}, { 0, 0}, { 0, 0}},
        // CCW
        {{ 0, 0}, {-1, 0}, {-1,-1}, { 0, 2}, {-1, 2}},
    },
    // I
    {
        // Spawn
        {{ 0, 0}, {-1, 0}, { 2, 0}, {-1, 0}, { 2, 0}},
        // CW
        {{-1, 0}, { 0, 0}, { 0, 0}, { 0, 1}, { 0,-2}},
        // 180
        {{-1, 1}, { 1, 1}, {-2, 1}, { 1, 0}, {-2, 0}},
        // CCW
        {{ 0, 1}, { 0, 1}, { 0, 1}, { 0,-1}, { 0, 2}},
    },
    // O
    {
        // Spawn
        {{ 0, 0}},
        // CW
        {{ 0,-1}},
        // 180
        {{-1,-1}},
        // CCW
        {{-1, 0}},
    },
};

// 180 offset table
const int8_t offsets2[2][4][2][2] = {
    {
        // Spawn
        {{ 0, 0}, { 0, 1}},
        // CW
        {{ 0, 0}, { 1, 0}},
        // 180
        {{ 0, 0}, { 0, 0}},
        // CCW
        {{ 0, 0}, { 0, 0}}
    },
    {
        // Spawn
        {{ 1, 0}, { 1, 0}},
        // CW
        {{-1, 0}, { 0, 0}},
        // 180
        {{ 0, 1}, { 0, 0}},
        // CCW
        {{ 0, 1}, { 0, 1}},
    }
};

static const char *conspath[] = {
    "/proc/self/fd/0",
    "/dev/tty",
    "/dev/tty0",
    "/dev/vc/0",
    "/dev/systty",
    "/dev/console",
    NULL
};

int is_a_console(int fd) {
    char arg = 0;
    return (isatty(fd) && ioctl(fd, KDGKBTYPE, &arg) == 0 && ((arg == KB_101) || (arg == KB_84)));
}

int getfd(struct termios* old, struct termios* new) {
    int fd = 0;

    for (int i = 0; conspath[i]; i++) {
        fd = open(conspath[i], O_RDONLY | O_NOCTTY | O_NONBLOCK);
        if (is_a_console(fd))
            break;
        close(fd);
    }

    if (fd < 0) {
        fprintf(stderr, "no fd\n");
        return -1;
    }

    if (tcgetattr(fd, old) == -1 || tcgetattr(fd, new) == -1) {
        fprintf(stderr, "tcgetattr error\n");
        return -1;
    }

    new->c_lflag &= ~((tcflag_t)(ICANON | ECHO | ISIG));
    new->c_iflag     = 0;
    new->c_cc[VMIN]  = 0;
    new->c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSAFLUSH, new) == -1) {
        fprintf(stderr, "tcsetattr error\n");
        return -1;
    }

    if (ioctl(fd, KDSKBMODE, K_RAW)) {
        fprintf(stderr, "ioctl KDSKBMODE error\n");
        return -1;
    }

    return fd;
}

void init_curses () {
    initscr();
    raw();
    curs_set(0);
    start_color();
    noecho();
    use_default_colors();
    nodelay(stdscr, 1);

    init_pair(1,  COLOR_CYAN,    COLOR_CYAN);
    init_pair(2,  COLOR_BLUE,    COLOR_BLUE);
    init_pair(3,  COLOR_WHITE,   COLOR_WHITE);
    init_pair(4,  COLOR_YELLOW,  COLOR_YELLOW);
    init_pair(5,  COLOR_GREEN,   COLOR_GREEN);
    init_pair(6,  COLOR_MAGENTA, COLOR_MAGENTA);
    init_pair(7,  COLOR_RED,     COLOR_RED);
    init_pair(8,  COLOR_WHITE,   -1);
    init_pair(9,  COLOR_BLUE,    COLOR_WHITE);
    init_pair(10, COLOR_WHITE,   COLOR_BLUE);
    init_pair(11, COLOR_BLUE,    -1);
    init_pair(12, COLOR_BLACK,   COLOR_CYAN);
    init_pair(13, COLOR_BLACK,   COLOR_BLUE);
    init_pair(14, COLOR_BLACK,   COLOR_WHITE);
    init_pair(15, COLOR_BLACK,   COLOR_YELLOW);
    init_pair(16, COLOR_BLACK,   COLOR_GREEN);
    init_pair(17, COLOR_BLACK,   COLOR_MAGENTA);
    init_pair(18, COLOR_BLACK,   COLOR_RED);

    // Make orange if supported
    if (COLORS > 8) {
        init_color(COLOR_ORANGE, 816, 529, 439);
        init_pair(3,  COLOR_ORANGE,  COLOR_ORANGE);
    }
}

time_t get_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}

int8_t check_collide(int8_t board[ARR_HEIGHT][BOARD_WIDTH], int8_t x, int8_t y, int8_t type, int8_t rot) {
    for (int i = 0; i < 4; i++) {
        int minoY = y - pieces[type][rot][i][1];
        int minoX = x + pieces[type][rot][i][0];
        if (minoY >= ARR_HEIGHT
          || minoX >= BOARD_WIDTH
          || minoX < 0
          || minoY < 0
              || board[minoY][minoX])
            return 1;
    }
    return 0;
}

void move_piece(int8_t board[ARR_HEIGHT][BOARD_WIDTH], Piece *p, int8_t h, int8_t amount) {
    int8_t collision = 0;
    int8_t last_x = p->x;
    int8_t last_y = p->y;
    int8_t step = (amount < 0) ? -1 : 1;

    for (int8_t i = step; i != amount + step; i += step) {
        int8_t x = p->x + (h ? i : 0);
        int8_t y = p->y + (h ? 0 : i);

        collision = check_collide(board, x, y, p->type, p->rot);

        if (!collision) {
            last_x = x;
            last_y = y;
        } else
            break;
    }

    p->x = last_x;
    p->y = last_y;

    for (int8_t i = 0; i < 4; i++) {
        p->coords[i][0] = p->x + pieces[p->type][p->rot][i][0];
        p->coords[i][1] = p->y - pieces[p->type][p->rot][i][1];
    }
}

void spin_piece(int8_t board[ARR_HEIGHT][BOARD_WIDTH], Piece *p, int8_t spin) {
    // 0 = cw
    // 1 = 180
    // 2 = ccw
    int8_t init_rot = p->rot;
    int8_t class = 0;
    if (p->type == 0) class = 1;
    if (p->type == 3) class = 2;
    p->rot = (p->rot + spin + 1) % 4;

    int8_t collision = 0;
    for (int8_t i = 0; i < 5; i++) {
        int8_t x = p->x + (offsets[class][init_rot][i][0] - offsets[class][p->rot][i][0]);
        int8_t y = p->y + (offsets[class][init_rot][i][1] - offsets[class][p->rot][i][1]);

        if (class != 2 && spin == 1) {
            x = p->x + (offsets2[class][init_rot][i][0] - offsets2[class][p->rot][i][0]);
            y = p->y + (offsets2[class][init_rot][i][1] - offsets2[class][p->rot][i][1]);
            if (i > 2)
                break;
        }

        collision = check_collide(board, x, y, p->type, p->rot);

        if (!collision) {
            p->x = x;
            p->y = y;
            break;
        }
    }

    if (collision) {
        p->rot = init_rot;
        return;
    }

    for (int8_t i = 0; i < 4; i++) {
        p->coords[i][0] = p->x + pieces[p->type][p->rot][i][0];
        p->coords[i][1] = p->y - pieces[p->type][p->rot][i][1];
    }

}

void draw_gui(int8_t x, int8_t y) {
    for (int8_t i = BOARD_HEIGHT - 1; i >= 0; i--) {
        mvprintw(y + i, x, "█");
        mvprintw(y + i, x + 1 + BOARD_WIDTH * 2, "█");
    }
    for (int8_t i = 0; i < BOARD_WIDTH + 1; i++)
        mvprintw(y + BOARD_HEIGHT, x + i * 2, "▀▀");
    refresh();
}

void draw_piece(WINDOW *w, int8_t x, int8_t y, int8_t type, int8_t rot, int8_t ghost) {
    for (int8_t i = 0; i < 4; i++) {
        wattron(w, COLOR_PAIR(ghost ? 8 : (type + 1)));
        mvwprintw(w,
                  y + pieces[type][rot][i][1],
                  2 * (x + pieces[type][rot][i][0]),
                  "[]"
        );
        wattroff(w, COLOR_PAIR(ghost ? 8 : (type + 1)));
    }
}

void draw_board(WINDOW *w, int8_t board[ARR_HEIGHT][BOARD_WIDTH], Piece *p, int8_t line) {
    werase(w);

    int8_t orig_y = p->y;
    move_piece(board, p, 0, -p->y);
    int8_t ghost_y = p->y;
    p->y = orig_y;

    for (int8_t i = 0; i < BOARD_HEIGHT; i++) {
        for (int8_t j = 0; j < BOARD_WIDTH; j++) {
            if (board[i][j]) {
                wattron(w, COLOR_PAIR(board[i][j]));
                mvwprintw(w, BOARD_HEIGHT - 1 - i, 2 * j, "[]");
                wattroff(w, COLOR_PAIR(board[i][j]));
            } else if (i == line) {
                mvwprintw(w, BOARD_HEIGHT - 1 - i, 2 * j, "__");
            }
        }
    }

    draw_piece(w, p->x, BOARD_HEIGHT - 1 - ghost_y, p->type, p->rot, 1);
    draw_piece(w, p->x, BOARD_HEIGHT - 1 - p->y, p->type, p->rot, 0);
    wrefresh(w);
}

void draw_queue(WINDOW *w, int8_t queue[], int8_t queue_pos) {
    werase(w);
    for (int8_t i = 0; i < QUEUE_SZ; i++) {
        draw_piece(w, 1, 2 + 3 * i, queue[queue_pos], 0, 0);
        queue_pos = (queue_pos + 1) % BAG_SZ;
    }
    wrefresh(w);
}

void draw_hold(WINDOW *w, int8_t p, int8_t held) {
    werase(w);
    if (p != -1) {
        draw_piece(w, 1, 1, p, 0, held);
    }
    wrefresh(w);
}

void draw_keys(WINDOW *w, int8_t inputs[]) {
    werase(w);

    wattron(w, COLOR_PAIR(11));
    mvwprintw(w, 0, 5, "▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄");
    mvwprintw(w, 2, 5, "▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀");
    mvwprintw(w, 4, 23, "▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄");
    mvwprintw(w, 6, 23, "▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀");
    mvwprintw(w, 2, 0, "▄▄▄▄▄");
    mvwprintw(w, 4, 0, "▀▀▀▀▀");
    mvwprintw(w, 4, 15, "▄▄▄▄▄");
    mvwprintw(w, 6, 15, "▀▀▀▀▀");
    wattroff(w, COLOR_PAIR(11));

    wattron(w, COLOR_PAIR(10));
    mvwprintw(w, 1, 5, "  (    )    /  ");
    mvwprintw(w, 3, 0, "  ↕  ");
    mvwprintw(w, 5, 15, "  ▼  ");
    mvwprintw(w, 5, 23, "  ←    ↓    →  ");
    wattroff(w, COLOR_PAIR(10));

    wattron(w, COLOR_PAIR(8));
    if (inputs[LEFT]) {
        mvwprintw(w, 4, 23, "▄▄▄▄▄");
        mvwprintw(w, 6, 23, "▀▀▀▀▀");
    }
    if (inputs[RIGHT]) {
        mvwprintw(w, 4, 33, "▄▄▄▄▄");
        mvwprintw(w, 6, 33, "▀▀▀▀▀");
    }
    if (inputs[SD]) {
        mvwprintw(w, 4, 28, "▄▄▄▄▄");
        mvwprintw(w, 6, 28, "▀▀▀▀▀");
    }
    if (inputs[HD]) {
        mvwprintw(w, 4, 15, "▄▄▄▄▄");
        mvwprintw(w, 6, 15, "▀▀▀▀▀");
    }
    if (inputs[CCW]) {
        mvwprintw(w, 0, 5, "▄▄▄▄▄");
        mvwprintw(w, 2, 5, "▀▀▀▀▀");
    }
    if (inputs[CW]) {
        mvwprintw(w, 0, 10, "▄▄▄▄▄");
        mvwprintw(w, 2, 10, "▀▀▀▀▀");
    }
    if (inputs[FLIP]) {
        mvwprintw(w, 0, 15, "▄▄▄▄▄");
        mvwprintw(w, 2, 15, "▀▀▀▀▀");
    }
    if (inputs[HOLD]) {
        mvwprintw(w, 2, 0, "▄▄▄▄▄");
        mvwprintw(w, 4, 0, "▀▀▀▀▀");
    }
    wattroff(w, COLOR_PAIR(8));

    wattron(w, COLOR_PAIR(9));
    if (inputs[LEFT]) {
        mvwprintw(w, 5, 23, "  ←  ");
    }
    if (inputs[RIGHT]) {
        mvwprintw(w, 5, 33, "  →  ");
    }
    if (inputs[SD]) {
        mvwprintw(w, 5, 28, "  ↓  ");
    }
    if (inputs[HD]) {
        mvwprintw(w, 5, 15, "  ▼  ");
    }
    if (inputs[CCW]) {
        mvwprintw(w, 1, 5, "  (  ");
    }
    if (inputs[CW]) {
        mvwprintw(w, 1, 10, "  )  ");
    }
    if (inputs[FLIP]) {
        mvwprintw(w, 1, 15, "  /  ");
    }
    if (inputs[HOLD]) {
        mvwprintw(w, 3, 0, "  ↕  ");
    }
    wattroff(w, COLOR_PAIR(9));

    wrefresh(w);
}

void draw_stats(WINDOW *w, int time, int pieces, int keys, int holds) {
    werase(w);

    int min = time / 60000;
    int sec = (time / 1000) % 60;
    int csec = (time / 10) % 100;

    if (min)
        mvwprintw(w, 0, 0, "%6s %d:%02d.%d", "Time", min, sec, csec);
    else
        mvwprintw(w, 0, 0, "%6s %d.%d", "Time", sec, csec);

    mvwprintw(w, 1, 0, "%6s %.2f", "PPS", pieces ? pieces / ((float) time / 1000) : 0);
    mvwprintw(w, 2, 0, "%6s %.2f", "KPP", pieces ? (float) keys / pieces : 0);
    mvwprintw(w, 3, 0, "%6s %d", "Hold", holds);
    mvwprintw(w, 4, 0, "%6s %d", "#", pieces);
    wrefresh(w);
}

void lock_piece(int8_t board[ARR_HEIGHT][BOARD_WIDTH], Piece *p) {
    for (int8_t i = 0; i < 4; i++)
        board[p->coords[i][1]][p->coords[i][0]] = p->type + 1;
}

void gen_piece(Piece *p, int8_t type) {
    p->type = type;
    p->rot = SPAWN_ROT;
    p->x = SPAWN_X;
    p->y = SPAWN_Y;
    for (int8_t i = 0; i < 4; i++) {
        p->coords[i][0] = p->x + pieces[type][0][i][0];
        p->coords[i][1] = p->y - pieces[type][0][i][1];
    }
}

int8_t queue_pop(Piece *p, int8_t queue[], int8_t queue_pos) {
    gen_piece(p, queue[queue_pos]);

    int8_t rand = random() % (BAG_SZ - queue_pos);
    int8_t used[BAG_SZ] = {0};
    int8_t bag[BAG_SZ];
    int8_t bag_pos = 0;

    for (int8_t i = 0; i < queue_pos; i++)
        used[queue[i]] = 1;

    for (int8_t i = 0; i < BAG_SZ; i++)
        if (!used[i])
            bag[bag_pos++] = i;

    queue[queue_pos] = bag[rand];
    return (queue_pos + 1) % BAG_SZ;

}

void queue_init (int8_t queue[]) {
    int8_t bag[BAG_SZ];
    for (int8_t i = 0; i < BAG_SZ; i++)
        bag[i] = i;

    for (int8_t i = BAG_SZ - 1; i > 0; i--) {
        int8_t rand = random() % (i + 1);
        queue[BAG_SZ - 1 - i] = bag[rand];
        bag[rand] = bag[i];
    }

    queue[BAG_SZ - 1] = bag[0];
}

int8_t clear_lines(int8_t board[ARR_HEIGHT][BOARD_WIDTH]) {
    int8_t cleared = 0;
    for (int8_t i = 0; i < ARR_HEIGHT; i++) {
        int8_t clear = 1;
        for (int8_t j = 0; j < BOARD_WIDTH; j++) {
            if (!board[i][j]) {
                clear = 0;
                break;
            }
        }
        if (clear) {
            cleared++;
            continue;
        } if (cleared) {
            for (int8_t j = 0; j < BOARD_WIDTH; j++) {
                board[i-cleared][j] = board[i][j];
                if (i >= ARR_HEIGHT - cleared)
                    board[i][j] = 0;
                board[ARR_HEIGHT-1][j] = 0;
            }
        }
    }
    return cleared;
}

void wash_board(int8_t board[ARR_HEIGHT][BOARD_WIDTH]) {
    for (int8_t i = 0; i < BOARD_HEIGHT; i++) {
        for (int8_t j = 0; j < BOARD_WIDTH; j++) {
            if (board[i][j]) {
                board[i][j] = 11;
            }
        }
    }
}

void get_inputs(enum InputMode mode, int fd, int8_t inputs[]) {
    if (mode == EXTKEYS) {
        char c;
        enum Parser state = INVALID;
        int key = 0;
        int8_t pressed = 0;
        while ((c = getch()) != ERR) {
            if (c == 27) {
                key = 0;
                pressed = 0;
                state = CODE;
                continue;
            }

            switch (state) {
                case CODE:
                    if ('0' <= c && c <= '9') {
                        key *= 10;
                        key += (c - '0');
                    } else if (c == ';')
                        state++;
                    else if (c != '[')
                        state = INVALID;
                    break;
                case MOD:
                    if (c == ':') state++;
                    break;
                case STATE:
                    pressed = (c == '1');
                    state++;
                    break;
                case END:
                    if (key == 1 && 'A' <= c && c <= 'D') {
                        switch (c) {
                            case 'A':
                                key = KEY_UP;
                                break;
                            case 'B':
                                key = KEY_DOWN;
                                break;
                            case 'C':
                                key = KEY_RIGHT;
                                break;
                            case 'D':
                                key = KEY_LEFT;
                                break;
                        }
                    }
                    for (int8_t i = 0; i < KEYS; i++) {
                        if (keys[i] == key) {
                            inputs[i] = pressed;
                        }
                    }
                    state++;
                    break;
                case INVALID:
                    break;
            }
        }
    // Reading input directly
    } else if (mode == SCANCODES) {
        unsigned char buf[32];
        ssize_t n = read(fd, buf, sizeof(buf));

        for (ssize_t i = 0; i < n; i++) {
            for (int8_t j = 0; j < KEYS; j++) {
                if (keys2[j] == buf[i]) {
                    inputs[j] = 1;
                    continue;
                }
                if ((keys2[j] | 0x80) == buf[i])
                    inputs[j] = 0;
            }
        }
    }
}

int8_t game(enum InputMode input_mode, int fd) {
    if (COLS < WIDTH || LINES < HEIGHT) {
        return 2;
    }

    // center board
    int offset_x = (COLS - BOARD_WIDTH * 2) / 2 - RIGHT_MARGIN;
    int offset_y = (LINES - HEIGHT) / 2 - 6;

    if (offset_x < 0)
        offset_x = 0;

    if (offset_y < 0)
        offset_y = 0;

    WINDOW *board_win = newwin(BOARD_HEIGHT, BOARD_WIDTH * 2, offset_y, offset_x + RIGHT_MARGIN);
    WINDOW *queue_win = newwin(15, 4 * 2, offset_y, offset_x + RIGHT_MARGIN + BOARD_WIDTH * 2 + 2);
    WINDOW *hold_win = newwin(2, 4 * 2, offset_y + 1, offset_x + 36);
    WINDOW *key_win = newwin(7, 38, offset_y + 3, offset_x);
    WINDOW *stat_win = newwin(5, 14, offset_y + BOARD_HEIGHT + 1, offset_x + RIGHT_MARGIN + 3);

    Piece *curr = malloc(sizeof(Piece));
    int8_t board[ARR_HEIGHT][BOARD_WIDTH];
    for (int8_t i = 0; i < ARR_HEIGHT; i++)
        for (int8_t j = 0; j < BOARD_WIDTH; j++)
            board[i][j] = 0;

    int8_t hold = -1;
    int8_t hold_used = 0;
    int8_t queue[BAG_SZ];
    queue_init(queue);
    int8_t queue_pos = 0;
    int8_t inputs[KEYS] = {0};
    int8_t last_inputs[KEYS] = {0};

    float grav = 0.02;
    float grav_c = 0;
    int8_t ldas_c = 0;
    int8_t rdas_c = 0;

    int pieces = 0;
    int holds = 0;
    int keys = 0;
    int cleared = 0;

    mvprintw(offset_y + 11, offset_x + 53, "READY");
    draw_gui(offset_x + 45, offset_y);

    draw_queue(queue_win, queue, queue_pos);
    draw_hold(hold_win, hold, hold_used);
    draw_keys(key_win, inputs);
    draw_stats(stat_win, 0, 0, 0, 0);

    usleep(500000);
    mvprintw(offset_y + 11, offset_x + 53, " GO! ");
    refresh();
    usleep(500000);

    time_t start_time = get_ms();
    time_t game_time;

    queue_pos = queue_pop(curr, queue, 0);

    // Game Loop
    while (1) {
        game_time = get_ms();
        for (int8_t i = 0; i < KEYS; i++)
            last_inputs[i] = inputs[i];
        get_inputs(input_mode, fd, inputs);

        for (int8_t i = 0; i < 8; i++) {
            keys += inputs[i] && !last_inputs[i];
        }

        if (inputs[RESET] || inputs[QUIT])
            break;
        if (inputs[HD] && !last_inputs[HD]) {
            move_piece(board, curr, 0, -curr->y);
            lock_piece(board, curr);
            queue_pos = queue_pop(curr, queue, queue_pos);
            cleared += clear_lines(board);
            hold_used = 0;
            grav_c = 0;
            pieces++;
            if (cleared >= CLEAR_GOAL)
                break;
        }

        if (inputs[LEFT] && rdas_c != DAS - 1) {
            ldas_c++;
        } else if (!inputs[LEFT] && ldas_c)
            ldas_c = 0;

        if (inputs[RIGHT] && ldas_c != DAS - 1) {
            rdas_c++;
        } else if (!inputs[RIGHT] && rdas_c)
            rdas_c = 0;

        if (ldas_c > DAS && (rdas_c == 0 || rdas_c > ldas_c))
            move_piece(board, curr, 1, -BOARD_WIDTH);
        if (rdas_c > DAS && (ldas_c == 0 || ldas_c > rdas_c))
            move_piece(board, curr, 1, BOARD_WIDTH);

        if (inputs[LEFT] && !last_inputs[LEFT])
            move_piece(board, curr, 1, -1);
        if (inputs[RIGHT] && !last_inputs[RIGHT])
            move_piece(board, curr, 1, 1);

        if (inputs[SD])
            move_piece(board, curr, 0, -curr->y);
        if (inputs[CCW] && !last_inputs[CCW])
            spin_piece(board, curr, 2);
        if (inputs[CW] && !last_inputs[CW])
            spin_piece(board, curr, 0);
        if (inputs[FLIP] && !last_inputs[FLIP])
            spin_piece(board, curr, 1);
        if (inputs[HOLD] && !last_inputs[HOLD]) {
            if (hold == -1) {
                hold = curr->type;
                queue_pos = queue_pop(curr, queue, queue_pos);
                holds++;
            } else if (!hold_used) {
                int8_t tmp = hold;
                hold = curr->type;
                gen_piece(curr, tmp);
                holds++;
            }
            hold_used = 1;
            grav_c = 0;
        }

        // Updates
        draw_board(board_win, board, curr, CLEAR_GOAL - cleared);
        draw_queue(queue_win, queue, queue_pos);
        draw_hold(hold_win, hold, hold_used);
        draw_keys(key_win, inputs);
        draw_stats(stat_win, game_time - start_time, pieces, keys, holds);

        // Gravity Movement
        grav_c += grav;
        move_piece(board, curr, 0, (int) -grav_c);
        grav_c = grav_c - (int) grav_c;

        usleep(1000000 / FPS - (get_ms() - game_time));
    }

    // Post game screen
    if (cleared >= CLEAR_GOAL) {
        wash_board(board);
        draw_board(board_win, board, curr, 21);
        draw_stats(stat_win, game_time - start_time, pieces, keys, holds);
        while (1) {
            get_inputs(input_mode, fd, inputs);
            if (inputs[RESET] || inputs[QUIT])
                break;
            draw_keys(key_win, inputs);
            usleep(1000000 / FPS);
        }
    }

    free(curr);
    delwin(board_win);
    delwin(queue_win);
    delwin(hold_win);
    delwin(key_win);
    clear();

    return inputs[QUIT];
}

int main(int argc, char **argv) {
    srandom(time(NULL));
    setlocale(LC_ALL, "");

    struct termios old;
    struct termios new;
    int fd = -1;
    enum InputMode mode = EXTKEYS;

    init_curses();

    // Setup extkeys
    if (mode == EXTKEYS) {
        fprintf(stderr, "\e[>11u");
        fprintf(stderr, "\e[?u");
        char *response = "\e[?11u";

        nodelay(stdscr, 0);
        timeout(100);
        char c = getch();
        nodelay(stdscr, 1);

        for (int8_t i = 0; i < 6; i++) {
            if (c != response[i]) {
                mode = SCANCODES;
            }
            c = getch();
        }
    }

    // Setup fd to read from console
    if (mode == SCANCODES) {
        fd = getfd(&old, &new);
        if (fd < 0) {
            mode = NORM;
        }
    }

    // Main loop
    int8_t status = 0;
    while (!(status = game(mode, fd)) && mode != NORM); // ignore norm for now

    // Cleanup extkeys
    if (mode == EXTKEYS)
        fprintf(stderr, "\e[<u");

    // Cleanup 
    if (mode == SCANCODES) {
        if (ioctl(fd, KDSKBMODE, K_UNICODE)) {
            fprintf(stderr, "ioctl KDSKBMODE error\n");
            close(fd);
            return 1;
        }
        if (tcsetattr(fd, 0, &old) == -1) {
            fprintf(stderr, "tcsetattr error\n");
            close(fd);
            return 1;
        }
        close(fd);
    }

    endwin();

    if (status == 2) {
        fprintf(stderr, "Screen dimensions smaller than %dx%d\n", WIDTH, HEIGHT);
    }

    return 0;
}
