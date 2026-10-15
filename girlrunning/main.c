// Ported from Zariep-software/raylib-d-wasm-build

#include "raylib.h"
#include <stdbool.h>
#include <stddef.h>

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

#define MAX_SPIKES 128

#define ASSETS_PATH "assets/"
#define SPRITE_PATH ASSETS_PATH "Sprites/"
#define MUSIC_PATH ASSETS_PATH "Music/"
#define SFX_PATH ASSETS_PATH "sfx/"

#define RUN_FRAMES 4
#define JUMP_FRAMES 1
#define RUN_FPS 4.0f

#define BASE_WIDTH 1024
#define BASE_HEIGHT 600
#define BASE_GROUND_Y 350.0f
#define BASE_PLAYER_SIZE 160.0f
#define SPIKE_MULT 3.0f
#define BASE_SPIKE_SIZE (30.0f * SPIKE_MULT)

// Hitbox inset, grounded (running sprite)
#define PLAYER_HB_LEFT 50.0f
#define PLAYER_HB_RIGHT 50.0f
#define PLAYER_HB_TOP 1.0f
#define PLAYER_HB_BOTTOM 10.0f

// Hitbox inset, airborne (jump sprite)
#define PLAYER_HB_LEFT_JUMP 60.0f
#define PLAYER_HB_RIGHT_JUMP 60.0f
#define PLAYER_HB_TOP_JUMP 15.0f
#define PLAYER_HB_BOTTOM_JUMP 30.0f

#define SPIKE_HB_X (8.0f * SPIKE_MULT)
#define SPIKE_HB_Y (18.0f * SPIKE_MULT)
#define SPIKE_HB_W (14.0f * SPIKE_MULT)
#define SPIKE_HB_H (14.0f * SPIKE_MULT)

#define RESTART_DELAY 0.3f

#define EXPLOSION_SIZE_MULT 1.0f


#if defined(PLATFORM_WEB)
static bool WebAudioOk(void)
{
	return emscripten_run_script_int(
		"(function(){try{var C=window.AudioContext||window.webkitAudioContext;"
		"var c=new C({sampleRate:44100});c.close();return 1;}catch(e){return 0;}})()");
}
#endif

#if defined(PLATFORM_WEB)
static int softLeftDown, softRightDown, callDown;

EMSCRIPTEN_KEEPALIVE
void OnSoftKey(int id, int down)
{
    if (id == 0) softLeftDown = down;
    else if (id == 1) softRightDown = down;
    else if (id == 2) callDown = down;
}
#endif

static bool audioEnabled = true;


typedef struct Spike
{
	Rectangle drawRect;
	Rectangle hitbox;
} Spike;

typedef struct Assets
{
	Texture2D running;
	Texture2D jump;
	Texture2D spike;
	Texture2D explosion;
	Texture2D bgTex;
	Texture2D groundTex;
	Music background;
	Sound explosionSfx;
	Sound jumpSfx;
} Assets;

typedef struct Game
{
	float screenW;
	float screenH;
	float scale;
	float groundY;
	float playerSize;
	// Player
	Vector2 playerPos;
	float velocityY;
	bool grounded;
	float animTimer;
	int animFrame;
	// Obstacles
	Spike spikes[MAX_SPIKES];
	size_t spikeCount;
	float spawnTimer;
	float obstacleSpeed;
	bool dead;
	float deadTimer;
	Vector2 explosionPos; // center of the explosion
	// Scrolling
	float bgScroll;
	float groundScroll;
} Game;

// File-level state: the frame function takes no arguments (required for Emscripten)
static Game game;
static Assets assets;
static bool showHitboxes = false; // toggled with F3
static bool quitRequested = false;

static Rectangle PlayerHitbox(const Game *g)
{
	float s = g->scale;
	// Recomputed every frame from `grounded`, so grounded insets return on landing.
	float l = g->grounded ? PLAYER_HB_LEFT : PLAYER_HB_LEFT_JUMP;
	float r = g->grounded ? PLAYER_HB_RIGHT : PLAYER_HB_RIGHT_JUMP;
	float t = g->grounded ? PLAYER_HB_TOP : PLAYER_HB_TOP_JUMP;
	float b = g->grounded ? PLAYER_HB_BOTTOM : PLAYER_HB_BOTTOM_JUMP;
	Rectangle hb;
	hb.x = g->playerPos.x + l * s;
	hb.y = g->playerPos.y + t * s;
	hb.width = g->playerSize - (l + r) * s;
	hb.height = g->playerSize - (t + b) * s;
	return hb;
}

static void LoadAssets(Assets *a)
{
	a->running = LoadTexture(SPRITE_PATH "running.png");
	a->jump = LoadTexture(SPRITE_PATH "jump.png");
	a->spike = LoadTexture(SPRITE_PATH "spike.png");
	a->explosion = LoadTexture(SPRITE_PATH "explosion.png");

	// Tiling textures (optional: flat colors are used if the files are missing)
	a->bgTex = LoadTexture(SPRITE_PATH "background.png");
	a->groundTex = LoadTexture(SPRITE_PATH "ground.png");
	if (a->bgTex.id != 0) SetTextureWrap(a->bgTex, TEXTURE_WRAP_REPEAT);
	if (a->groundTex.id != 0) SetTextureWrap(a->groundTex, TEXTURE_WRAP_REPEAT);

	a->background = LoadMusicStream(MUSIC_PATH "background.ogg");
	a->background.looping = true;
	SetMusicVolume(a->background, 0.5f);

	a->explosionSfx = LoadSound(SFX_PATH "explosion.ogg");
	a->jumpSfx = LoadSound(SFX_PATH "jjump.ogg");
}

static void UnloadAssets(Assets *a)
{
	UnloadTexture(a->running);
	UnloadTexture(a->jump);
	UnloadTexture(a->spike);
	UnloadTexture(a->explosion);
	UnloadTexture(a->bgTex);
	UnloadTexture(a->groundTex);
	UnloadMusicStream(a->background);
	UnloadSound(a->explosionSfx);
	UnloadSound(a->jumpSfx);
}

static void InitGame(Game *g)
{
	g->screenW = (float)GetScreenWidth();
	g->screenH = (float)GetScreenHeight();
	g->scale = g->screenH / BASE_HEIGHT;
	g->groundY = BASE_GROUND_Y * g->scale;
	g->playerSize = BASE_PLAYER_SIZE * g->scale;

	g->playerPos = (Vector2){ 150 * g->scale, g->groundY - g->playerSize };
	g->velocityY = 0;
	g->grounded = true;
	g->animTimer = 0;
	g->animFrame = 0;
	g->spawnTimer = 0;
	g->obstacleSpeed = 600 * g->scale;
	g->spikeCount = 0;
	g->dead = false;
	g->deadTimer = 0;
	g->explosionPos = (Vector2){ 0, 0 };
	g->bgScroll = 0;
	g->groundScroll = 0;
}

static void RestartRun(Game *g)
{
	InitGame(g);
	StopMusicStream(assets.background);
	PlayMusicStream(assets.background);
}

static bool JumpPressed(void)
{
	bool pressed = false;
	int key;
	while ((key = GetKeyPressed()) != 0)
	{
		if (key != KEY_F3) pressed = true;
	}
	return pressed
		|| IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
		|| IsGestureDetected(GESTURE_TAP);
}

static void UpdatePlayer(Game *g, float dt)
{
	if (JumpPressed() && g->grounded)
	{
		g->velocityY = -500 * g->scale;
		g->grounded = false;
		PlaySound(assets.jumpSfx);
	}
	g->velocityY += 1200 * g->scale * dt;
	g->playerPos.y += g->velocityY * dt;
	if (g->playerPos.y >= g->groundY - g->playerSize)
	{
		g->playerPos.y = g->groundY - g->playerSize;
		g->velocityY = 0;
		g->grounded = true;
	}

	// Run animation
	if (g->grounded)
	{
		g->animTimer += dt;
		float frameTime = 1.0f / RUN_FPS;
		while (g->animTimer >= frameTime)
		{
			g->animTimer -= frameTime;
			g->animFrame = (g->animFrame + 1) % RUN_FRAMES;
		}
	}
	else
	{
		g->animTimer = 0;
		g->animFrame = 0;
	}
}

static void AddSpike(Game *g, float x)
{
	if (g->spikeCount >= MAX_SPIKES) return;
	float sz = BASE_SPIKE_SIZE * g->scale;
	Spike *s = &g->spikes[g->spikeCount++];
	s->drawRect = (Rectangle){ x, g->groundY - sz, sz, sz };
	s->hitbox = (Rectangle){ x + SPIKE_HB_X * g->scale, g->groundY - SPIKE_HB_Y * g->scale,
		SPIKE_HB_W * g->scale, SPIKE_HB_H * g->scale };
}

static void SpawnSpikes(Game *g, float dt)
{
	g->spawnTimer += dt;
	if (g->spawnTimer < 2) return;
	g->spawnTimer = 0;
	int count = GetRandomValue(1, 3);
	for (int i = 0; i < count; i++)
	{
		// Spawn just past the right edge of the screen for this run
		AddSpike(g, g->screenW + (1 + i * 35 * SPIKE_MULT) * g->scale);
	}
}

static void UpdateSpikes(Game *g, float dt)
{
	for (size_t i = 0; i < g->spikeCount; i++)
	{
		Spike *s = &g->spikes[i];
		s->drawRect.x -= g->obstacleSpeed * dt;
		s->hitbox.x -= g->obstacleSpeed * dt;
	}
	size_t i = 0;
	while (i < g->spikeCount)
	{
		if (g->spikes[i].drawRect.x < -50 * SPIKE_MULT * g->scale)
		{
			g->spikes[i] = g->spikes[g->spikeCount - 1];
			g->spikeCount--;
		}
		else i++;
	}
}

static void CheckCollisions(Game *g)
{
	Rectangle playerBox = PlayerHitbox(g);
	for (size_t i = 0; i < g->spikeCount; i++)
	{
		if (CheckCollisionRecs(playerBox, g->spikes[i].hitbox))
		{
			// Explosion appears where the spike was
			Rectangle d = g->spikes[i].drawRect;
			g->explosionPos = (Vector2){ d.x + d.width / 2, d.y + d.height / 2 };

			// Remove the spike
			g->spikes[i] = g->spikes[g->spikeCount - 1];
			g->spikeCount--;

			// Player disappears too
			g->dead = true;
			g->deadTimer = 0;
			StopMusicStream(assets.background);
			PlaySound(assets.explosionSfx);
			return;
		}
	}
}

static void UpdateGame(Game *g, float dt)
{
	if (g->dead)
	{
		g->deadTimer += dt;
		if (g->deadTimer >= RESTART_DELAY && JumpPressed())
			RestartRun(g);
		return;
	}
	g->groundScroll += g->obstacleSpeed * dt;
	g->bgScroll += g->obstacleSpeed * 0.25f * dt; // parallax
	UpdatePlayer(g, dt);
	SpawnSpikes(g, dt);
	UpdateSpikes(g, dt);
	CheckCollisions(g);
}

static void DrawPlayer(const Game *g)
{
	Rectangle dest = { g->playerPos.x, g->playerPos.y, g->playerSize, g->playerSize };

	Texture2D tex = g->grounded ? assets.running : assets.jump;
	int frames = g->grounded ? RUN_FRAMES : JUMP_FRAMES;
	int frame = g->grounded ? g->animFrame : 0;

	// Fallback
	if (tex.id == 0)
	{
		DrawRectangleRec(dest, WHITE);
		return;
	}

	float fw = (float)tex.width / frames;
	Rectangle src = { fw * frame, 0, fw, (float)tex.height };
	DrawTexturePro(tex, src, dest, (Vector2){ 0, 0 }, 0, WHITE);
}

static void DrawSpike(const Spike *s)
{
	if (assets.spike.id != 0)
	{
		Rectangle src = { 0, 0, (float)assets.spike.width, (float)assets.spike.height };
		DrawTexturePro(assets.spike, src, s->drawRect, (Vector2){ 0, 0 }, 0, WHITE);
	}
	else
	{
		// Fallback
		DrawTriangle(
			(Vector2){ s->drawRect.x, s->drawRect.y + s->drawRect.height },
			(Vector2){ s->drawRect.x + s->drawRect.width, s->drawRect.y + s->drawRect.height },
			(Vector2){ s->drawRect.x + s->drawRect.width / 2, s->drawRect.y },
			WHITE);
	}
}

static void DrawExplosion(const Game *g)
{
	float size = BASE_SPIKE_SIZE * EXPLOSION_SIZE_MULT * g->scale;
	Rectangle dest = { g->explosionPos.x - size / 2, g->explosionPos.y - size / 2, size, size };

	if (assets.explosion.id != 0)
	{
		Rectangle src = { 0, 0, (float)assets.explosion.width, (float)assets.explosion.height };
		DrawTexturePro(assets.explosion, src, dest, (Vector2){ 0, 0 }, 0, WHITE);
	}
	else
	{
		DrawCircleV(g->explosionPos, size / 2, ORANGE);
	}
}

static void DrawCenteredText(const char *text, float centerX, float y, int size, Color color)
{
	int w = MeasureText(text, size);
	DrawText(text, (int)(centerX - w / 2), (int)y, size, color);
}

static void DrawTiled(Texture2D tex, Rectangle dest, float tileScale, float scroll)
{
	Rectangle src = { scroll / tileScale, 0, dest.width / tileScale, dest.height / tileScale };
	DrawTexturePro(tex, src, dest, (Vector2){ 0, 0 }, 0, WHITE);
}

static void DrawGame(const Game *g)
{
	Color bg = { 0x2A, 0x2A, 0x2A, 255 };
	float sw = (float)GetScreenWidth();
	float sh = (float)GetScreenHeight();

	ClearBackground(bg);

	// Background
	if (assets.bgTex.id != 0)
		DrawTiled(assets.bgTex, (Rectangle){ 0, 0, sw, sh }, sh / assets.bgTex.height, g->bgScroll);

	// Ground
	Rectangle groundRect = { 0, g->groundY, sw, sh - g->groundY };
	if (assets.groundTex.id != 0)
		DrawTiled(assets.groundTex, groundRect, g->scale, g->groundScroll);
	else
		DrawRectangleRec(groundRect, GRAY);

	for (size_t i = 0; i < g->spikeCount; i++)
	{
		DrawSpike(&g->spikes[i]);
		if (showHitboxes)
			DrawRectangleLinesEx(g->spikes[i].hitbox, 2, RED);
	}

	if (!g->dead)
	{
		DrawPlayer(g);
		if (showHitboxes)
			DrawRectangleLinesEx(PlayerHitbox(g), 2, GREEN);
	}
	else
	{
		DrawExplosion(g);

		float cx = g->screenW / 2;
		DrawCenteredText("GAME OVER", cx, g->screenH * 0.17f, (int)(40 * g->scale), RED);
		DrawCenteredText("Press any key to restart", cx, g->screenH * 0.17f + 55 * g->scale,
			(int)(20 * g->scale), WHITE);
	}
}

static void UpdateDrawFrame(void)
{
	if (IsKeyPressed(KEY_ESCAPE)) quitRequested = true;
	if (IsKeyPressed(KEY_F3)) showHitboxes = !showHitboxes;

	UpdateMusicStream(assets.background);

	float dt = GetFrameTime();
	UpdateGame(&game, dt);

	BeginDrawing();
	DrawGame(&game);
	EndDrawing();

#if defined(PLATFORM_WEB)
	if (quitRequested) emscripten_cancel_main_loop();
#endif
}

static void DetectScreenSize(int *w, int *h)
{
	*w = BASE_WIDTH;
	*h = BASE_HEIGHT;
#if defined(PLATFORM_WEB)
	int iw = EM_ASM_INT({ return window.innerWidth | 0; });
	int ih = EM_ASM_INT({ return window.innerHeight | 0; });
	if (iw >= 120 && ih >= 120)
	{
		*w = iw;
		*h = ih;
	}
#endif
}

int main(void)
{
	SetTraceLogLevel(LOG_DEBUG);
	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	int winW, winH;
	DetectScreenSize(&winW, &winH);
	InitWindow(winW, winH, "Infinite Jumper");

	#if defined(PLATFORM_WEB)
    audioEnabled = WebAudioOk();
	#endif
    if (audioEnabled) InitAudioDevice();

	LoadAssets(&assets);
	InitGame(&game);
	PlayMusicStream(assets.background);

#if defined(PLATFORM_WEB)
	// 0 = requestAnimationFrame, 1 = simulate infinite loop.
	// Do NOT call SetTargetFPS() on web.
	emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
	SetTargetFPS(60);
	while (!WindowShouldClose() && !quitRequested) UpdateDrawFrame();
#endif

	UnloadAssets(&assets);
	CloseAudioDevice();
	CloseWindow();
	return 0;
}