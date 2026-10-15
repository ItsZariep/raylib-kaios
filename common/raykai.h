/*
	raykai.h - single-header helpers for running raylib games on KaiOS (Firefox 48 / B2G)

	What it gives you
	- Main loop        RayKaiRunMainLoop()       works on web (no blocking loop) and desktop
	- screen size      RayKaiInitWindow()        window = browser viewport, no letterboxing
	- Audio            RayKaiInitAudioDevice()   probes Web Audio, skips audio if KaiOS rejects it
	- Soft keys        RayKaiPollInput() + RayKaiSoftKey*()   SoftLeft / SoftRight / Call

	Usage
	#include "raylib.h"
	#include "raykai.h" // include after raylib.h

	static bool quit = false;
	static void Frame(void)
	{
		RayKaiPollInput();                       // call once at the start of every frame
		if (IsKeyPressed(KEY_ESCAPE)) quit = true;
		if (RayKaiSoftKeyPressed(RAYKAI_SOFT_LEFT)) { ... }
		BeginDrawing(); ... EndDrawing();
	}

	int main(void)
	{
		RayKaiInitWindow(240, 320, "My game");   // fallback size used on desktop
		RayKaiInitAudioDevice();
		// load assets; guard audio calls with if (RayKaiAudioEnabled())
		RayKaiRunMainLoop(Frame, 60, &quit);     // returns when the game quits (desktop)
		if (RayKaiAudioEnabled()) CloseAudioDevice();
		CloseWindow();
		return 0;
	}

	Required HTML shell (emcc --shell-file): the audio shim and the soft-key recorder below.
	Put both in the shell. The soft-key script goes after the Module script.

	<script>  // KaiOS AudioContext shim (B2G's constructor expects an AudioChannel string)
	(function () {
		var Orig = window.AudioContext || window.webkitAudioContext;
		if (!Orig) return;
		try { new Orig({ sampleRate: 44100 }).close(); return; } catch (e) {}
		function Patched() { return new Orig(); }
		Patched.prototype = Orig.prototype;
		window.AudioContext = Patched;
		window.webkitAudioContext = Patched;
	})();
	</script>

	<script>  // soft key recorder
	(function () {
		window.softKeys = [0, 0, 0];
		window.softPressed = [0, 0, 0];
		window.keyCount = 0;
		window.lastKey = '';
		var map = { SoftLeft: 0, SoftRight: 1, Call: 2 };
		document.addEventListener('keydown', function (e) {
			window.keyCount++;
			window.lastKey = e.key + '/' + e.keyCode;
			var id = map[e.key];
			if (id === undefined) return;
			e.preventDefault();
			if (!window.softKeys[id]) window.softPressed[id] = 1;
			window.softKeys[id] = 1;
		}, true);
		document.addEventListener('keyup', function (e) {
			var id = map[e.key];
			if (id === undefined) return;
			e.preventDefault();
			window.softKeys[id] = 0;
		}, true);
	})();
	</script>

	And a canvas that fills the screen:
	canvas { position:absolute; left:0; top:0; width:100%; height:100%; object-fit:contain; }

	Configuration (define before including this header)
	RAYKAI_MIN_SIZE                   smallest viewport size accepted by detection (default 120)
	RAYKAI_DESKTOP_KEY_SOFT_LEFT      desktop stand-ins for the soft keys (default KEY_Z)
	RAYKAI_DESKTOP_KEY_SOFT_RIGHT     (default KEY_X)
	RAYKAI_DESKTOP_KEY_CALL           (default KEY_C)
*/

	#ifndef RAYKAI_H
	#define RAYKAI_H

	#include <stdbool.h>
	#include <string.h>

	#if defined(PLATFORM_WEB)
		#include <emscripten/emscripten.h>
	#endif

	#ifndef RAYKAI_MIN_SIZE
		#define RAYKAI_MIN_SIZE 120
	#endif
	#ifndef RAYKAI_DESKTOP_KEY_SOFT_LEFT
		#define RAYKAI_DESKTOP_KEY_SOFT_LEFT KEY_Z
	#endif
	#ifndef RAYKAI_DESKTOP_KEY_SOFT_RIGHT
		#define RAYKAI_DESKTOP_KEY_SOFT_RIGHT KEY_X
	#endif
	#ifndef RAYKAI_DESKTOP_KEY_CALL
		#define RAYKAI_DESKTOP_KEY_CALL KEY_C
	#endif

	// Soft key ids (must match the ids in the shell script)
	#define RAYKAI_SOFT_LEFT  0
	#define RAYKAI_SOFT_RIGHT 1
	#define RAYKAI_SOFT_CALL  2
	#define RAYKAI_SOFT_COUNT 3

	// Internal state (single translation unit; include this header from one .c file)
	static bool raykai_audioEnabled = true;
	static bool raykai_softDown[RAYKAI_SOFT_COUNT];
	static bool raykai_softPressed[RAYKAI_SOFT_COUNT];
	static char raykai_lastKey[32] = "";
	static int raykai_lastKeyCount = 0;
	static void (*raykai_frameFn)(void) = 0;
	static const bool *raykai_quitFlag = 0;

	// Screen size //

	// Writes the browser viewport size (CSS pixels) to *w / *h on web, or the given
	// defaults on desktop or if the viewport is implausibly small.
	static inline void RayKaiDetectScreenSize(int defaultW, int defaultH, int *w, int *h)
	{
		*w = defaultW;
		*h = defaultH;
	#if defined(PLATFORM_WEB)
		int iw = EM_ASM_INT({ return window.innerWidth | 0; });
		int ih = EM_ASM_INT({ return window.innerHeight | 0; });
		if (iw >= RAYKAI_MIN_SIZE && ih >= RAYKAI_MIN_SIZE)
		{
			*w = iw;
			*h = ih;
		}
	#endif
	}

	// InitWindow() with the detected size. Read the result back with
	// GetScreenWidth() / GetScreenHeight() and lay your game out from those.
	static inline void RayKaiInitWindow(int defaultW, int defaultH, const char *title)
	{
		int w, h;
		RayKaiDetectScreenSize(defaultW, defaultH, &w, &h);
		InitWindow(w, h, title);
	}

	// Audio //

	// True if a Web Audio context can be created. Always true on desktop.
	static inline bool RayKaiWebAudioOk(void)
	{
	#if defined(PLATFORM_WEB)
		return emscripten_run_script_int(
			"(function(){try{var C=window.AudioContext||window.webkitAudioContext;"
			"var c=new C({sampleRate:44100});c.close();return 1;}catch(e){return 0;}})()") != 0;
	#else
		return true;
	#endif
	}

	// InitAudioDevice() only if the platform can do audio. Returns whether audio is enabled.
	// With the shell's AudioContext shim installed this is true on KaiOS as well.
	static inline bool RayKaiInitAudioDevice(void)
	{
		raykai_audioEnabled = RayKaiWebAudioOk();
		if (raykai_audioEnabled) InitAudioDevice();
		return raykai_audioEnabled;
	}

	// Guard every Load*/Unload*/Play*/Stop*/Update*/Setaudio call with this.
	static inline bool RayKaiAudioEnabled(void)
	{
		return raykai_audioEnabled;
	}

	// Soft keys (SoftLeft / SoftRight / Call) //

	// Call once at the start of every frame. Reads the state recorded by the shell
	// (web) or the desktop stand-in keys (Z / X / C by default).
	static inline void RayKaiPollInput(void)
	{
	#if defined(PLATFORM_WEB)
		for (int i = 0; i < RAYKAI_SOFT_COUNT; i++)
		{
			raykai_softDown[i] = EM_ASM_INT({ return (window.softKeys && window.softKeys[$0]) ? 1 : 0; }, i) != 0;
			// "pressed" is latched by the shell and cleared on read, so a quick tap
			// between two frames is never missed.
			raykai_softPressed[i] = EM_ASM_INT({
				var v = (window.softPressed && window.softPressed[$0]) ? 1 : 0;
				if (window.softPressed) window.softPressed[$0] = 0;
				return v;
			}, i) != 0;
		}

		// Remember the last key the browser reported ("key/keyCode"), for debugging.
		int n = EM_ASM_INT({ return window.keyCount | 0; });
		if (n != raykai_lastKeyCount)
		{
			raykai_lastKeyCount = n;
			const char *s = emscripten_run_script_string("window.lastKey || ''");
			strncpy(raykai_lastKey, s ? s : "", sizeof(raykai_lastKey) - 1);
			raykai_lastKey[sizeof(raykai_lastKey) - 1] = '\0';
		}
	#else
		static const int keys[RAYKAI_SOFT_COUNT] = {
			RAYKAI_DESKTOP_KEY_SOFT_LEFT, RAYKAI_DESKTOP_KEY_SOFT_RIGHT, RAYKAI_DESKTOP_KEY_CALL
		};
		for (int i = 0; i < RAYKAI_SOFT_COUNT; i++)
		{
			raykai_softDown[i] = IsKeyDown(keys[i]);
			raykai_softPressed[i] = IsKeyPressed(keys[i]);
		}
	#endif
	}

	// True while the soft key is held (id = RAYKAI_SOFT_LEFT / _RIGHT / _CALL).
	static inline bool RayKaiSoftKeyDown(int id)
	{
		return (id >= 0 && id < RAYKAI_SOFT_COUNT) ? raykai_softDown[id] : false;
	}

	// True on the frame the soft key went down.
	static inline bool RayKaiSoftKeyPressed(int id)
	{
		return (id >= 0 && id < RAYKAI_SOFT_COUNT) ? raykai_softPressed[id] : false;
	}

	// Last key reported by the browser as "key/keyCode" (e.g. "SoftLeft/0"). Empty on desktop.
	// Draw it on screen to see what a device actually sends.
	static inline const char *RayKaiLastKey(void)
	{
		return raykai_lastKey;
	}

	// Main loop //

	#if defined(PLATFORM_WEB)
	static void raykai_webFrame(void)
	{
		raykai_frameFn();
		if (raykai_quitFlag && *raykai_quitFlag) emscripten_cancel_main_loop();
	}
	#endif

	// Runs frameFn every frame.
	//   web:     emscripten_set_main_loop (requestAnimationFrame); fps is ignored
	//            and SetTargetFPS() is NOT called, because it would block.
	//   desktop: SetTargetFPS(fps), loops until the window closes or *quitFlag is true.
	// quitFlag may be NULL. On web this call does not return until the loop is cancelled.
	static inline void RayKaiRunMainLoop(void (*frameFn)(void), int fps, const bool *quitFlag)
	{
		raykai_frameFn = frameFn;
		raykai_quitFlag = quitFlag;
	#if defined(PLATFORM_WEB)
		(void)fps;
		emscripten_set_main_loop(raykai_webFrame, 0, 1);
	#else
		SetTargetFPS(fps);
		while (!WindowShouldClose() && !(quitFlag && *quitFlag)) frameFn();
	#endif
	}

	#endif
