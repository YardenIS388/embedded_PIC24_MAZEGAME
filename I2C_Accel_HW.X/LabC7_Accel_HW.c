#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "System/system.h"
#include "System/delay.h"
#include "oledDriver/oledC.h"
#include "oledDriver/oledC_colors.h"
#include "oledDriver/oledC_shapes.h"
#include "i2cDriver/i2c1_driver.h"
#include "Accel_i2c.h"
#include <string.h>

// Constants
#define POT_MAX_VALUE 1023
#define MENU_OPTIONS 4
#define BUTTON_PORT PORTA
#define BUTTON_PIN 11
#define DEBOUNCE_TIME 50
#define TILT_THRESHOLD 2
#define JUMP 2
#define MAX_WALLS 20
#define L1TIME 1000
#define L2TIME 900
#define L3TIME 800
#define L4TIME 700

// Data Types
typedef enum {
    GAME_ON,
    GAME_OFF
} GameState;

typedef struct {
    int direction; // 0 horizontal, 1 vertical
    int x;
    int y;
    int length;
} Wall;

typedef struct {
    Wall walls[40];
    int x;
    int y;
    int num_walls;
    int end_x;
    int end_y;
} Maze;

typedef struct {
    int x;
    int y;
    int radius;
} Ball;

typedef enum {
    TILT_NONE,
    TILT_UP,
    TILT_DOWN,
    TILT_LEFT,
    TILT_RIGHT,
} TiltDirection;

// Function Prototypes
void InitializeSystem(void);
void StopWithError(char *message);
void DelayMilliseconds(int ms);
Wall CreateWall(int direction, int x, int y, int length);
void DrawWall(const Wall *wall, uint16_t color);
void DrawWalls(const Maze *maze, uint16_t color);
void InitializeMaze(Maze *maze, int maze_number);
Ball CreateBall(int x, int y, int radius);
int CheckWallCollision(const Ball *ball, const Wall *wall);
int CheckMazeCollision(const Ball *ball, const Maze *maze);
int IsWallBelow(const Ball *ball, const Maze *maze);
void DrawBall(const Ball *ball, uint16_t color);
TiltDirection DetectTilt(int x, int y, int z);
int DisplayMazePickMenu(void);
int DisplayLevelPickMenu(void);
int IsButtonPressed(void);
void DisplayLevelOption(int option);
void DisplayMenuOption(int option);
void DisplayWinScreen(void);
void DisplayLoseScreen(void);

int main(void) {
    InitializeSystem();

    int selected_maze;
    int selected_level;
    int is_win;

    while (1) {
        oledC_clearScreen();
        selected_maze = DisplayMazePickMenu();
        selected_level = DisplayLevelPickMenu();
        is_win = PlayMazeGame(selected_level, selected_maze);

        switch (is_win) {
            case 0:
                DisplayLoseScreen();
                continue;
            case 1:
                DisplayWinScreen();
                continue;
            case -1:
                continue;
        }
    }

    return 1;
}

void InitializeSystem(void) {
    // Initialize IO Direction
    TRISA |= (1 << BUTTON_PIN) | (1 << 12);
    TRISA &= ~((1 << 8) | (1 << 9));
    TRISB |= (1 << 12);
    ANSB |= (1 << 12);

    // Initialize A/D Circuit
    AD1CON1 = 0x00;
    AD1CON1bits.SSRC = 0;
    AD1CON1bits.FORM = 0;
    AD1CON1bits.MODE12 = 0;
    AD1CON1bits.ADON = 1;

    AD1CON2 = 0;
    AD1CON3 = 0x00;
    AD1CON3bits.ADCS = 0xFF;
    AD1CON3bits.SAMC = 0x10;
}

void StopWithError(char *message) {
    oledC_DrawString(0, 20, 2, 2, (uint8_t *)message, OLEDC_COLOR_DARKRED);

    while (1)
        ;
}

void DelayMilliseconds(int ms) {
    volatile int i, j;
    for (i = 0; i < ms; i++) {
        for (j = 0; j < 2000; j++) {
            // Do nothing, just loop
        }
    }
}

// Maze Functions

Wall CreateWall(int direction, int x, int y, int length) {
    Wall wall;
    wall.direction = direction;
    wall.x = x;
    wall.y = y;
    wall.length = length;
    return wall;
}

void DrawWall(const Wall *wall, uint16_t color) {
    int startY, endY;

    if (wall->direction == 0) { // Horizontal wall
        for (int x = wall->x; x < (wall->x + wall->length); x++) {
            oledC_DrawPoint(x, wall->y, color);
        }
    } else { // Vertical wall
        startY = (wall->y < 0) ? 0 : wall->y;
        endY = (wall->y + wall->length > 90) ? 90 : wall->y + wall->length;

        for (int y = startY; y < endY; y++) {
            oledC_DrawPoint(wall->x, y, color);
        }
    }
}

void DrawWalls(const Maze *maze, uint16_t color) {
    for (int i = 0; i < maze->num_walls; i++) {
        Wall tempWall = maze->walls[i];

        if ((tempWall.direction == 0 && (tempWall.y + tempWall.length) < 0) ||
            (tempWall.direction == 0 && tempWall.y > 192) ||
            (tempWall.direction == 1 && (tempWall.y + tempWall.length) < 0) ||
            (tempWall.direction == 1 && tempWall.y > 192)) {
            continue;
        }

        DrawWall(&tempWall, color);
    }
}

void InitializeMaze(Maze *maze, int maze_number) {
    int directions[MAX_WALLS], x_values[MAX_WALLS], y_values[MAX_WALLS], lengths[MAX_WALLS];
    int end_position[2] = {50, 64};

    const int maze_count = 4;
    const int direction_size = sizeof(directions);

    int *direction_lists[] = {direction1, direction2, direction3, direction4};
    int *x_lists[] = {x1, x2, x3, x4};
    int *y_lists[] = {y1, y2, y3, y4};
    int *length_lists[] = {len1, len2, len3, len4};

    if (maze_number >= 0 && maze_number < maze_count) {
        memcpy(directions, direction_lists[maze_number], direction_size);
        memcpy(x_values, x_lists[maze_number], direction_size);
        memcpy(y_values, y_lists[maze_number], direction_size);
        memcpy(lengths, length_lists[maze_number], direction_size);
    } else {
        StopWithError("Invalid maze number");
    }

    maze->num_walls = MAX_WALLS;
    maze->x = 0;
    maze->y = 0;
    maze->end_x = end_position[0];
    maze->end_y = end_position[1];

    for (int i = 0; i < maze->num_walls; i++) {
        Wall wall = CreateWall(directions[i], x_values[i], y_values[i], lengths[i]);
        maze->walls[i] = wall;
    }
}

// Ball Functions

Ball CreateBall(int x, int y, int radius) {
    Ball ball;
    ball.x = x;
    ball.y = y;
    ball.radius = radius;
    return ball;
}

int CheckWallCollision(const Ball *ball, const Wall *wall) {
    int wallStartX, wallEndX, wallStartY, wallEndY;

    if (wall->direction == 0) { // Horizontal wall
        wallStartX = wall->x;
        wallEndX = wall->x + wall->length;
        wallStartY = wallEndY = wall->y;
    } else { // Vertical wall
        wallStartX = wallEndX = wall->x;
        wallStartY = wall->y;
        wallEndY = wall->y + wall->length;
    }

    // Calculate distances
    int dx = ball->x - (wallStartX + wallEndX) / 2;
    int dy = ball->y - (wallStartY + wallEndY) / 2;

    int width = (wall->direction == 0) ? wall->length : 1;
    int height = (wall->direction == 1) ? wall->length : 1;

    int hx = width / 2;
    int hy = height / 2;

    int crossWidth = hx * dy;
    int crossHeight = hy * dx;

    // Collision detection logic
    if (abs(dx) <= hx && abs(dy) <= hy) {
        if (crossWidth > crossHeight) {
            if (crossWidth > -crossHeight) {
                return (wall->direction == 0) ? TILT_DOWN : TILT_LEFT;
            } else {
                return (wall->direction == 0) ? TILT_LEFT : TILT_UP;
            }
        } else {
            if (crossWidth > -crossHeight) {
                return (wall->direction == 0) ? TILT_RIGHT : TILT_DOWN;
            } else {
                return (wall->direction == 0) ? TILT_UP : TILT_RIGHT;
            }
        }
    }

    return TILT_NONE;
}

int CheckMazeCollision(const Ball *ball, const Maze *maze) {
    for (int i = 0; i < maze->num_walls; i++) {
        if (CheckWallCollision(ball, &maze->walls[i]) != TILT_NONE) {
            return 1;
        }
    }

    return 0;
}

int IsWallBelow(const Ball *ball, const Maze *maze) {
    for (int i = 0; i < maze->num_walls; i++) {
        if (ball->y + ball->radius >= maze->walls[i].y && ball->y <= maze->walls[i].y + maze->walls[i].length) {
            return 1;
        }
    }

    return 0;
}

// Drawing Functions

void DrawBall(const Ball *ball, uint16_t color) {
    oledC_DrawCircle(ball->x, ball->y, ball->radius, color);
}

TiltDirection DetectTilt(int x, int y, int z) {
    if (x < -TILT_THRESHOLD) {
        return TILT_LEFT;
    } else if (x > TILT_THRESHOLD) {
        return TILT_RIGHT;
    } else if (y < -TILT_THRESHOLD) {
        return TILT_UP;
    } else if (y > TILT_THRESHOLD) {
        return TILT_DOWN;
    } else {
        return TILT_NONE;
    }
}

// Menu Functions

int DisplayMazePickMenu(void) {
    int option = 0;
    int buttonPressed = 0;

    oledC_DrawString(2, 2, 2, 2, (uint8_t *)"Select Maze:", OLEDC_COLOR_WHITE);
    oledC_DrawString(4, 30, 2, 2, (uint8_t *)"1. Maze 1", OLEDC_COLOR_WHITE);
    oledC_DrawString(4, 50, 2, 2, (uint8_t *)"2. Maze 2", OLEDC_COLOR_WHITE);
    oledC_DrawString(4, 70, 2, 2, (uint8_t *)"3. Maze 3", OLEDC_COLOR_WHITE);
    oledC_DrawString(4, 90, 2, 2, (uint8_t *)"4. Maze 4", OLEDC_COLOR_WHITE);

    while (!buttonPressed) {
        buttonPressed = IsButtonPressed();

        if (buttonPressed) {
            option = (option % MENU_OPTIONS) + 1;
            DisplayMenuOption(option);
        }
    }

    return option;
}

int DisplayLevelPickMenu(void) {
    int option = 0;
    int buttonPressed = 0;

    oledC_DrawString(2, 2, 2, 2, (uint8_t *)"Select Level:", OLEDC_COLOR_WHITE);
    oledC_DrawString(4, 30, 2, 2, (uint8_t *)"1. Level 1", OLEDC_COLOR_WHITE);
    oledC_DrawString(4, 50, 2, 2, (uint8_t *)"2. Level 2", OLEDC_COLOR_WHITE);
    oledC_DrawString(4, 70, 2, 2, (uint8_t *)"3. Level 3", OLEDC_COLOR_WHITE);
    oledC_DrawString(4, 90, 2, 2, (uint8_t *)"4. Level 4", OLEDC_COLOR_WHITE);

    while (!buttonPressed) {
        buttonPressed = IsButtonPressed();

        if (buttonPressed) {
            option = (option % MENU_OPTIONS) + 1;
            DisplayLevelOption(option);
        }
    }

    return option;
}

int IsButtonPressed(void) {
    if (!(BUTTON_PORT & (1 << BUTTON_PIN))) {
        DelayMilliseconds(DEBOUNCE_TIME);
        if (!(BUTTON_PORT & (1 << BUTTON_PIN))) {
            while (!(BUTTON_PORT & (1 << BUTTON_PIN)))
                ;
            return 1;
        }
    }
    return 0;
}

void DisplayMenuOption(int option) {
    char optionString[3];
    snprintf(optionString, sizeof(optionString), "%d", option);
    oledC_DrawString(100, 20, 2, 2, (uint8_t *)optionString, OLEDC_COLOR_BLACK);
    DelayMilliseconds(200);
    oledC_DrawString(100, 20, 2, 2, (uint8_t *)optionString, OLEDC_COLOR_WHITE);
}

void DisplayLevelOption(int option) {
    char optionString[3];
    snprintf(optionString, sizeof(optionString), "%d", option);
    oledC_DrawString(100, 40, 2, 2, (uint8_t *)optionString, OLEDC_COLOR_BLACK);
    DelayMilliseconds(200);
    oledC_DrawString(100, 40, 2, 2, (uint8_t *)optionString, OLEDC_COLOR_WHITE);
}

// Main Function

int main(void) {
    // Initialize OLED display, accelerometer, and buttons
    oledC_Init();
    accel_Init();
    button_Init();

    // Main game loop
    while (1) {
        int selectedMaze = DisplayMazePickMenu();
        int selectedLevel = DisplayLevelPickMenu();

        Maze maze;
        InitializeMaze(&maze, selectedMaze);

        int ballRadius = BALL_RADIUS;
        Ball ball = CreateBall(maze.x + maze.end_x, maze.y + maze.end_y, ballRadius);

        int tiltX, tiltY, tiltZ;
        accel_Read(&tiltX, &tiltY, &tiltZ);

        TiltDirection currentTilt = DetectTilt(tiltX, tiltY, tiltZ);
        TiltDirection previousTilt = currentTilt;

        while (1) {
            accel_Read(&tiltX, &tiltY, &tiltZ);
            previousTilt = currentTilt;
            currentTilt = DetectTilt(tiltX, tiltY, tiltZ);

            if (currentTilt != previousTilt) {
                ball.x += tiltDeltas[currentTilt].x;
                ball.y += tiltDeltas[currentTilt].y;

                if (ball.x < maze.x) {
                    ball.x = maze.x;
                }
                if (ball.x > maze.x + MAZE_WIDTH) {
                    ball.x = maze.x + MAZE_WIDTH;
                }
                if (ball.y < maze.y) {
                    ball.y = maze.y;
                }
                if (ball.y > maze.y + MAZE_HEIGHT) {
                    ball.y = maze.y + MAZE_HEIGHT;
                }

                DrawBall(&ball, OLEDC_COLOR_WHITE);
                DelayMilliseconds(100);

                if (IsWallBelow(&ball, &maze) || CheckMazeCollision(&ball, &maze)) {
                    oledC_DrawString(20, 110, 2, 2, (uint8_t *)"Game Over", OLEDC_COLOR_WHITE);
                    DelayMilliseconds(1000);
                    break;
                }

                DrawBall(&ball, OLEDC_COLOR_BLACK);
            }
        }
    }

    return 0;
}

