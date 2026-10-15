#include "raylib.h"
#include "../common/raykai.h" // include after raylib.h

// Fallback window size (portrait KaiOS phone). On web the real size is detected.
#define DEFAULT_WIDTH 240
#define DEFAULT_HEIGHT 320

#define PADDLE_HEIGHT 12
#define BALL_RADIUS 6
#define BRICK_ROWS 4
#define BRICK_COLS 8
#define BRICK_WIDTH_MAX 32
#define BRICK_HEIGHT 12
#define BRICK_PADDING 4
#define BRICK_MARGIN 4

#define MAX_BULLETS 16
#define BULLET_W 3
#define BULLET_H 8
#define BULLET_SPEED 6.0f
#define SHOT_COOLDOWN 10

typedef struct
{
	Rectangle rect;
	bool active;
	Color color;
} Brick;

typedef struct
{
	Rectangle rect;
	float vx;
	bool active;
	Color color;
} Bullet;

// Game state
static int screenW = DEFAULT_WIDTH;
static int screenH = DEFAULT_HEIGHT;
static Rectangle paddle;
static float paddleSpeed = 4.0f;
static Vector2 ballPos;
static Vector2 ballVel;
static Brick bricks[BRICK_ROWS][BRICK_COLS];
static Color rowColors[BRICK_ROWS];
static Bullet bullets[MAX_BULLETS];
static int shotCooldown = 0;
static bool gameOver = false;
static bool gameWon = false;
static bool quitRequested = false;

// Lays out the paddle and bricks for the current screen size.
static void BuildLevel(void)
{
	float pw = screenW * 0.19f;
	if (pw < 40.0f) pw = 40.0f;
	paddle = (Rectangle){ (screenW - pw) / 2.0f, screenH - 30.0f, pw, PADDLE_HEIGHT };

	rowColors[0] = RED;
	rowColors[1] = ORANGE;
	rowColors[2] = YELLOW;
	rowColors[3] = GREEN;

	float brickW = (screenW - 2.0f * BRICK_MARGIN - (BRICK_COLS - 1) * BRICK_PADDING) / (float)BRICK_COLS;
	if (brickW > BRICK_WIDTH_MAX) brickW = BRICK_WIDTH_MAX;
	float totalW = BRICK_COLS * brickW + (BRICK_COLS - 1) * BRICK_PADDING;
	float startX = (screenW - totalW) / 2.0f;
	float startY = 40.0f;

	for (int r = 0; r < BRICK_ROWS; r++)
	{
		for (int c = 0; c < BRICK_COLS; c++)
		{
			bricks[r][c].rect = (Rectangle){
				startX + c * (brickW + BRICK_PADDING),
				startY + r * (BRICK_HEIGHT + BRICK_PADDING),
				brickW,
				BRICK_HEIGHT
			};
			bricks[r][c].active = true;
			bricks[r][c].color = rowColors[r];
		}
	}
}

static void ResetRound(void)
{
	ballPos = (Vector2){ screenW / 2.0f, screenH / 2.0f };
	ballVel = (Vector2){ 3.0f, -3.0f };
	paddle.x = (screenW - paddle.width) / 2.0f;
	gameOver = false;
	gameWon = false;
	shotCooldown = 0;

	for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;
	for (int r = 0; r < BRICK_ROWS; r++)
		for (int c = 0; c < BRICK_COLS; c++)
			bricks[r][c].active = true;
}

// vx = horizontal speed (pixels per frame); the bullet always travels upward.
static void Fire(float x, float vx, Color color)
{
	for (int i = 0; i < MAX_BULLETS; i++)
	{
		if (!bullets[i].active)
		{
			bullets[i].rect = (Rectangle){ x - BULLET_W / 2.0f, paddle.y - BULLET_H, BULLET_W, BULLET_H };
			bullets[i].vx = vx;
			bullets[i].color = color;
			bullets[i].active = true;
			return;
		}
	}
}

static void UpdateBullets(void)
{
	for (int i = 0; i < MAX_BULLETS; i++)
	{
		Bullet *b = &bullets[i];
		if (!b->active) continue;

		b->rect.x += b->vx;
		b->rect.y -= BULLET_SPEED;

		if (b->rect.y + b->rect.height < 0 || b->rect.x + b->rect.width < 0 || b->rect.x > screenW)
		{
			b->active = false;
			continue;
		}

		for (int r = 0; r < BRICK_ROWS && b->active; r++)
		{
			for (int c = 0; c < BRICK_COLS; c++)
			{
				if (bricks[r][c].active && CheckCollisionRecs(b->rect, bricks[r][c].rect))
				{
					bricks[r][c].active = false;
					b->active = false;
					break;
				}
			}
		}
	}
}

static void DrawHud(void)
{
	// Soft key indicators: light up while the key is held
	static const char *labels[RAYKAI_SOFT_COUNT] = { "SL", "SR", "CALL" };
	int x = 4;
	for (int i = 0; i < RAYKAI_SOFT_COUNT; i++)
	{
		bool down = RayKaiSoftKeyDown(i);
		int w = MeasureText(labels[i], 10) + 6;
		DrawRectangle(x, screenH - 14, w, 12, down ? DARKGREEN : LIGHTGRAY);
		DrawText(labels[i], x + 3, screenH - 13, 10, down ? RAYWHITE : DARKGRAY);
		x += w + 4;
	}

	// Last key reported by the browser (debug, empty on desktop)
	const char *last = RayKaiLastKey();
	if (last[0] != '\0')
		DrawText(last, 4, 4, 10, GRAY);
}

static void UpdateDrawFrame(void)
{
	RayKaiPollInput();

	if (IsKeyPressed(KEY_ESCAPE)) quitRequested = true;

	if (!gameOver && !gameWon)
	{
		// Paddle Movement
		if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) paddle.x -= paddleSpeed;
		if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) paddle.x += paddleSpeed;

		if (paddle.x < 0) paddle.x = 0;
		if (paddle.x > screenW - paddle.width) paddle.x = screenW - paddle.width;

		if (shotCooldown > 0) shotCooldown--;
		if ((IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_UP)) && shotCooldown == 0)
		{
			Fire(paddle.x + paddle.width / 2.0f, 0.0f, DARKBLUE);
			shotCooldown = SHOT_COOLDOWN;
		}
		if (RayKaiSoftKeyPressed(RAYKAI_SOFT_LEFT))
			Fire(paddle.x + 3.0f, -1.5f, PURPLE);
		if (RayKaiSoftKeyPressed(RAYKAI_SOFT_RIGHT))
			Fire(paddle.x + paddle.width - 3.0f, 1.5f, BROWN);
		if (RayKaiSoftKeyPressed(RAYKAI_SOFT_CALL))
		{
			float cx = paddle.x + paddle.width / 2.0f;
			Fire(cx, -2.0f, MAGENTA);
			Fire(cx, 0.0f, MAGENTA);
			Fire(cx, 2.0f, MAGENTA);
		}

		UpdateBullets();

		// Ball Movement
		ballPos.x += ballVel.x;
		ballPos.y += ballVel.y;

		// Wall Collisions
		if (ballPos.x - BALL_RADIUS < 0)
		{
			ballPos.x = BALL_RADIUS; ballVel.x *= -1;
		}
		if (ballPos.x + BALL_RADIUS > screenW)
		{
			ballPos.x = screenW - BALL_RADIUS; ballVel.x *= -1;
		}
		if (ballPos.y - BALL_RADIUS < 0)
		{
			ballPos.y = BALL_RADIUS; ballVel.y *= -1;
		}

		// Bottom Collision (Game Over)
		if (ballPos.y + BALL_RADIUS > screenH)
		{
			gameOver = true;
		}

		// Paddle Collision
		Rectangle ballRect = { ballPos.x - BALL_RADIUS, ballPos.y - BALL_RADIUS, BALL_RADIUS * 2, BALL_RADIUS * 2 };
		if (CheckCollisionRecs(ballRect, paddle) && ballVel.y > 0)
		{
			ballVel.y *= -1;
			float hitFactor = (ballPos.x - (paddle.x + paddle.width / 2.0f)) / (paddle.width / 2.0f);
			ballVel.x = hitFactor * 4.0f;
		}

		// Brick Collisions
		bool remainingBricks = false;
		for (int r = 0; r < BRICK_ROWS; r++)
		{
			for (int c = 0; c < BRICK_COLS; c++)
			{
				if (bricks[r][c].active)
				{
					remainingBricks = true;
					if (CheckCollisionCircleRec(ballPos, BALL_RADIUS, bricks[r][c].rect))
					{
						bricks[r][c].active = false;
						ballVel.y *= -1;
					}
				}
			}
		}

		if (!remainingBricks)
		{
			gameWon = true;
		}
	}
	else
	{
		if (IsKeyPressed(KEY_ENTER))
			ResetRound();
	}

	BeginDrawing();
	ClearBackground(RAYWHITE);

	if (!gameOver && !gameWon)
	{
		DrawRectangleRec(paddle, DARKGRAY);
		DrawCircleV(ballPos, BALL_RADIUS, MAROON);

		for (int i = 0; i < MAX_BULLETS; i++)
		{
			if (bullets[i].active)
				DrawRectangleRec(bullets[i].rect, bullets[i].color);
		}

		for (int r = 0; r < BRICK_ROWS; r++)
		{
			for (int c = 0; c < BRICK_COLS; c++)
			{
				if (bricks[r][c].active)
				{
					DrawRectangleRec(bricks[r][c].rect, bricks[r][c].color);
					DrawRectangleLines((int)bricks[r][c].rect.x, (int)bricks[r][c].rect.y,
						(int)bricks[r][c].rect.width, (int)bricks[r][c].rect.height, Fade(BLACK, 0.2f));
				}
			}
		}
	}
	else if (gameOver)
	{
		DrawText("GAME OVER", screenW / 2 - MeasureText("GAME OVER", 24) / 2, screenH / 2 - 24, 24, RED);
		DrawText("Press ENTER to Restart", screenW / 2 - MeasureText("Press ENTER to Restart", 10) / 2, screenH / 2 + 8, 10, DARKGRAY);
	}
	else if (gameWon)
	{
		DrawText("YOU WIN!", screenW / 2 - MeasureText("YOU WIN!", 24) / 2, screenH / 2 - 24, 24, GREEN);
		DrawText("Press ENTER to Restart", screenW / 2 - MeasureText("Press ENTER to Restart", 10) / 2, screenH / 2 + 8, 10, DARKGRAY);
	}

	DrawHud();

	EndDrawing();
}

int main(void)
{
	RayKaiInitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, "Simple Arkanoid in Raylib");
	screenW = GetScreenWidth();
	screenH = GetScreenHeight();

	BuildLevel();
	ResetRound();

	RayKaiRunMainLoop(UpdateDrawFrame, 60, &quitRequested);

	CloseWindow();
	return 0;
}