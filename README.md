# Running raylib games on KaiOS

Notes from getting a raylib game to run on KaiOS 2.5, which ships Firefox 48 (Gecko 48, B2G).

> Since i don't have a KaiOS 3.x+ device and they support wasm natively, i guess they can run modern raylib without problems aside the input handling, not tested.


- Use **emsdk 1.38.48-fastcomp**, not a modern emsdk.
- Use **raylib 3.0 – 4.5** from the GitHub releases. Avoid 5.0+.
- Drive the game with `emscripten_set_main_loop`. No `SetTargetFPS()` and no blocking loops on web.
- Compile with `-s WASM=0 -s USE_GLFW=3 -DPLATFORM_WEB`.
- Use a custom `--shell-file` and `--memory-init-file 0`.
- Add the **AudioContext shim** (see [Audio](#audio-on-kaios)) or `InitAudioDevice()` will crash at startup.
- Design for 240x320, D-pad input and low memory.

## Compatibility matrix

| Component | Works | Doesn't work |
| --- | --- | --- |
| Emscripten | **1.38.48** (fastcomp, real asm.js) | Modern upstream Emscripten (2.0+) |
| raylib | **3.0, 3.5, 3.7, 4.0, 4.5** | **5.0+** (`undefined symbol: GetWindowInnerHeight`), 5.0+ does not build with old emcc |
| Main loop | `emscripten_set_main_loop` | Blocking `while (!WindowShouldClose())` + `SetTargetFPS()` |
| Audio init | `InitAudioDevice()` with the shim, or skipped via a guard | Plain `InitAudioDevice()` on KaiOS (throws, see below) |

## Why Emscripten 1.38.48

- In modern upstream Emscripten, `-sWASM=0` does **not** emit asm.js. It emits generic JavaScript that emulates the WebAssembly API.
- Modern output relies on newer ECMAScript features (e.g. `BigInt` integration, modern syntax) that throw syntax or runtime errors on Firefox 48 (released 2016).
- The legacy **fastcomp** backend, which produced strict, validating asm.js, was removed in Emscripten **2.0.0**.

> [!NOTE]
> The latest emsdk cannot install the fastcomp toolchain, so it was not an option there. See the workaround below.

### Installing the old toolchain

Since current emsdk refuses to install fastcomp (which is fastest than the default wasm2js), use the older emsdk release **3.1.29**, which still can:

```sh
git clone -b 3.1.29 https://github.com/emscripten-core/emsdk.git emsdk-3.1.29
cd emsdk-3.1.29
./emsdk install 1.38.48-fastcomp
./emsdk activate 1.38.48-fastcomp
source /path/to/emsdk_env.sh   # see emsdk output for the exact path
```

### Expect noisy, slow builds

Old emsdk builds print a lot of errors (e.g. missing `libxml2.so.2`) and compile slowly. The errors are harmless and can be filtered (needs Bash for process substitution):

```sh
emcc main.c -s WASM=0 2> >(grep -v "libxml2.so.2")
```

## raylib versions

Modern raylib will not work on this toolchain, and old raylib may not build on modern systems. The tagged **GitHub releases** build fine.

| raylib | Result |
| --- | --- |
| 2.6 and older | No WebAssembly/web build available |
| 3.0 | OK |
| 3.5 | OK |
| 3.7 | OK |
| 4.0 | OK |
| 4.5 | OK |
| 5.0+ | `undefined symbol: GetWindowInnerHeight`; patching it leads to further emcc errors |

> [!IMPORTANT]
> If you use fastcomp, you must rebuild `libraylib.a` yourself with the **same** old emsdk:
> `make PLATFORM=PLATFORM_WEB -B` from the raylib release source.
> In these tests 4.5 built successfully, while 3.7 and older did not.

## Code considerations

### Do not use a blocking main loop

A normal raylib loop crashes at startup with:

```
exception thrown: Please compile your program with async support in order to use asynchronous operations like emscripten_sleep
```

Browsers cannot block the main thread. `SetTargetFPS()` makes `EndDrawing()` wait, which calls `emscripten_sleep`. The fix is to move the loop body into a function and hand it to Emscripten.

```c
#include "raylib.h"
#if defined(PLATFORM_WEB)
	#include <emscripten/emscripten.h>
#endif

// State used by the frame function must be file-level (static), not local to main()
static void UpdateDrawFrame(void) {
	// input, update, BeginDrawing() ... EndDrawing()
}

int main(void) {
	InitWindow(800, 600, "game");
	// init state here

#if defined(PLATFORM_WEB)
	// 0 = requestAnimationFrame, 1 = simulate infinite loop.
	// Do NOT call SetTargetFPS() on web.
	emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
	SetTargetFPS(60);
	while (!WindowShouldClose()) UpdateDrawFrame();
#endif

	CloseWindow();
	return 0;
}
```

Notes:

- Do not use `-s ASYNCIFY`. It does not exist in 1.38. The old equivalent is the emterpreter (`EMTERPRETIFY`), which is slow on low-end phones.
- Avoid any other blocking waits (`WaitTime`, custom `while` loops for loading or menu screens).
- If the game has an exit key (e.g. Esc), set a flag and call `emscripten_cancel_main_loop()` on web instead of breaking out of a loop.

### Dynamic resolution (no letterboxing)

**Symptom.** Game shows black bars at the top and bottom, or wastes screen space.

**Cause.** `InitWindow(W, H, ...)` fixes the size of the canvas's drawing buffer. CSS can only scale that buffer uniformly, so when the screen's aspect ratio differs from `W:H` the browser letterboxes it. Stretching it to fill the screen would distort the picture.

**Fix: make the drawing buffer match the screen.** Detect the viewport size once at startup and pass it to `InitWindow`:

```c
static void DetectScreenSize(int *w, int *h)
{
	*w = BASE_WIDTH;    // fallback, also used on desktop
	*h = BASE_HEIGHT;
#if defined(PLATFORM_WEB)
	int iw = EM_ASM_INT({ return window.innerWidth | 0; });
	int ih = EM_ASM_INT({ return window.innerHeight | 0; });
	if (iw >= 120 && ih >= 120) { *w = iw; *h = ih; }
#endif
}

// in main():
	int winW, winH;
	DetectScreenSize(&winW, &winH);
	InitWindow(winW, winH, "Game");
```

The shell then lets the canvas fill the screen. Because the buffer now has the same aspect ratio as the screen, nothing is stretched or letterboxed. `object-fit: contain` is only a fallback for games that still use a fixed size:

```css
canvas {
	position: absolute; left: 0; top: 0;
	width: 100%; height: 100%;
	object-fit: contain;
}
```

**What the game must do.** Dynamic resolution only works if the game does not hard-code its size:

- Do not use fixed `SCREEN_WIDTH` / `SCREEN_HEIGHT` constants after startup. Use `GetScreenWidth()` / `GetScreenHeight()`, or copy them into variables once.
- Compute the layout from those values. For art-based games, pick a reference size and use a scale factor (e.g. `scale = screenH / BASE_HEIGHT`). For grid-style games, derive cell and brick sizes from the width.
- Check that gameplay still makes sense at both extremes, a tall 240x320 phone and a wide desktop window.

**Notes.**

- Detection runs once at startup. Rotating the phone or resizing the window afterwards does not change the resolution. Restarting the game recomputes the layout only if it reads `GetScreenWidth()` again, and the canvas itself won't resize.
- `window.innerWidth/innerHeight` are CSS pixels. On KaiOS they equal the physical pixels (device pixel ratio 1), so no scaling is needed. On high-DPI desktop screens you can multiply by `devicePixelRatio` if you want sharper output, at the cost of speed.
- On desktop builds the detection falls back to the base size, so the same code works everywhere.
- Very large viewports cost fill rate on weak devices. If needed, clamp the detected size (e.g. height at most 600) and let CSS scale it up.
- Tiled textures with `TEXTURE_WRAP_REPEAT` still need power-of-two sizes on WebGL 1 (see Device constraints).


### Soft keys on KaiOS (SoftLeft / SoftRight / Call)

**Symptom.** The D-pad and center key work, but SoftLeft, SoftRight and Call do nothing: `IsKeyPressed()`, `IsKeyDown()` and `GetKeyPressed()` never report them.

**Cause (most likely).** On KaiOS these keys are dispatched as ordinary `keydown`/`keyup` events with `event.key` set to `"SoftLeft"`, `"SoftRight"` or `"Call"`, but `event.keyCode` is typically `0`. raylib's web backend (Emscripten GLFW) maps keys from `keyCode`, so it has nothing to map and drops the event. The keys never reach raylib at all.

**Fix: record the keys in the HTML shell and poll them from C.** The shell listens for the events in the capture phase and stores the state in globals. The game reads them every frame with `EM_ASM_INT`, so no exported C functions or extra emcc flags are needed.

> [!TIP]
> An earlier approach had the shell call an exported C function through `Module.ccall`. It silently did nothing when the game didn't define that function, and the `try/catch` hid the error. Polling avoids that failure mode.

Shell (after the `Module` script, before `{{{ SCRIPT }}}`):

```html
<script>
	(function () {
		window.softKeys = [0, 0, 0];     // 1 while the key is held
		window.softPressed = [0, 0, 0];  // latched to 1 on key down, cleared by the game when read
		window.keyCount = 0;             // debug: incremented on every key event
		window.lastKey = '';             // debug: "key/keyCode" of the last key
		var map = { SoftLeft: 0, SoftRight: 1, Call: 2 };

		document.addEventListener('keydown', function (e) {
			window.keyCount++;
			window.lastKey = e.key + '/' + e.keyCode;
			var id = map[e.key];
			if (id === undefined) return;
			e.preventDefault();
			if (!window.softKeys[id]) window.softPressed[id] = 1;  // ignore key repeat
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
```

C side: call `PollSoftKeys()` once at the start of every frame. On desktop it emulates the keys with Z / X / C so the same code path can be tested without a phone.

```c
#define SOFT_LEFT  0
#define SOFT_RIGHT 1
#define SOFT_CALL  2

static bool softDown[3];     // held this frame
static bool softPressed[3];  // went down since the last frame

static void PollSoftKeys(void)
{
#if defined(PLATFORM_WEB)
	for (int i = 0; i < 3; i++)
	{
		softDown[i] = EM_ASM_INT({ return (window.softKeys && window.softKeys[$0]) ? 1 : 0; }, i) != 0;
		// "pressed" is latched by the shell and cleared on read,
		// so a quick tap between two frames is never missed.
		softPressed[i] = EM_ASM_INT({
			var v = (window.softPressed && window.softPressed[$0]) ? 1 : 0;
			if (window.softPressed) window.softPressed[$0] = 0;
			return v;
		}, i) != 0;
	}
#else
	static const int keys[3] = { KEY_Z, KEY_X, KEY_C };
	for (int i = 0; i < 3; i++)
	{
		softDown[i] = IsKeyDown(keys[i]);
		softPressed[i] = IsKeyPressed(keys[i]);
	}
#endif
}

// usage, inside the frame function:
//   if (softPressed[SOFT_LEFT]) { /* ... */ }
```

**Debugging unknown keys.** `window.lastKey` always holds the last key the browser reported. Show it on screen to see what your phone actually sends:

```c
static char lastKey[32] = "";
static int lastKeyCount = 0;

// in PollSoftKeys(), web branch:
	int n = EM_ASM_INT({ return window.keyCount | 0; });
	if (n != lastKeyCount)
	{
		lastKeyCount = n;
		const char *s = emscripten_run_script_string("window.lastKey || ''");
		strncpy(lastKey, s ? s : "", sizeof(lastKey) - 1);
		lastKey[sizeof(lastKey) - 1] = '\0';
	}

// when drawing:
	DrawText(lastKey, 4, 4, 10, GRAY);
```

If a soft key shows a different `key` string (or only a `keyCode`), add it to the `map` in the shell. Include `<string.h>` for `strncpy`.

### Audio on KaiOS

**Symptom.** The game dies right after the raylib init log, during `InitAudioDevice()`:

```
exception thrown: TypeError: Argument 1 of AudioContext.constructor '[object Object]'
is not a valid value for enumeration AudioChannel.
```

**Cause (most likely).** raylib's bundled miniaudio creates the Web Audio context with `new AudioContext({ sampleRate: ... })`. KaiOS is built on B2G, whose `AudioContext` constructor takes an `AudioChannel` *string* (e.g. `"normal"`) instead of an options object, so the call throws. Desktop Firefox and Waterfox never had that signature and ignore the argument, which is why audio works there. This is a KaiOS/B2G quirk, not a Firefox 48 limitation as such.

| Environment | Result |
| --- | --- |
| KaiOS, plain `InitAudioDevice()` | Crash at startup |
| KaiOS, with an audio-support probe that skips audio | Runs, silent |
| Waterfox (Firefox ~56) | Audio plays |

**Fix 1: shim in the HTML shell (preferred).** Put this in `<head>`, before the Emscripten script. Where the options form works it does nothing. Where it throws, it replaces the constructor with a no-argument one:

```html
<script>
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
```


**Fix 2: guard audio in C (safety net).** Probe for a working context and skip every audio call if it fails, so the game still runs silently:

```c
#if defined(PLATFORM_WEB)
static bool WebAudioOk(void)
{
	return emscripten_run_script_int(
		"(function(){try{var C=window.AudioContext||window.webkitAudioContext;"
		"var c=new C({sampleRate:44100});c.close();return 1;}catch(e){return 0;}})()");
}
#endif

static bool audioEnabled = true;

// in main():
#if defined(PLATFORM_WEB)
	audioEnabled = WebAudioOk();
#endif
	if (audioEnabled) InitAudioDevice();

// then wrap every Load*/Unload*/Play*/Stop*/Update*/Set* audio call in `if (audioEnabled)`
```

With the shim installed, the probe passes and audio is enabled.

**Other audio notes.**

- Browsers keep the context suspended until the first user gesture. Start music on the first input event.
- `.ogg` files are decoded by raylib itself, so they do not depend on the browser's codecs.
- When debugging other problems, test with audio disabled first.

### Device constraints

- **Screen:** KaiOS devices are typically 240x320 (some 240x240). Prefer a native 240x320 canvas.
- **Frame rate:** if movement is per-frame, the game slows down when the device cannot hold 60 fps. Scale by `GetFrameTime() * 60.0f` if needed, or better, use delta time for everything.
- **Input:** there is no keyboard or mouse. The D-pad arrives as arrow keys and the center key as `Enter`. Soft keys (`SoftLeft`/`SoftRight`) may not map to a raylib key, so a small JS shim may be needed to remap them.
- **Textures:** WebGL 1 only, with limited NPOT support (no mipmaps, no repeat; the log shows "NPOT textures extension not found"). Use power-of-two sizes where possible, especially for tiled or scrolling textures that use `TEXTURE_WRAP_REPEAT`.
- **Memory:** devices often have 256–512 MB total. Keep `TOTAL_MEMORY` modest (32–64 MB) and avoid `ALLOW_MEMORY_GROWTH` if it misbehaves.
- **Ignorable log noise:** the "Prefixed Fullscreen API is deprecated" warning and the NPOT-extension warning are harmless.

## Makefile

```make
SHELL := /bin/bash   # needed for the 2> >(grep ...) process substitution

web:
	source /usr/lib/emsdk/emsdk_env.sh && emcc $(SRC) -o index.html \
		-Wall -std=c11 -O3 \
		-DPLATFORM_WEB \
		-I/usr/include \
		~/.local/include/raylib/kaios/libraylib.a \
		-s USE_GLFW=3 \
		-s WASM=0 \
		--shell-file shell.html \
		--memory-init-file 0 \
		2> >(grep -v "libxml2.so.2")
```

> Replace `-I` and the `libraylib.a` path with your own raylib header and library locations. Add `--preload-file assets` if the game loads files from disk.

| Flag | Purpose |
| --- | --- |
| `-DPLATFORM_WEB` | Enables the Emscripten code path in your source and in raylib headers |
| `-s USE_GLFW=3` | raylib 3.x/4.x desktop-style windowing on web |
| `-s WASM=0` | With 1.38 fastcomp this emits real asm.js, which Firefox 48 needs (no WebAssembly) |
| `--shell-file shell.html` | Replaces the default Emscripten page (console, buttons, etc.) with a canvas-only page |
| `--memory-init-file 0` | Embeds the memory image into `index.js`, so there is no separate `index.html.mem` to ship |

Output after `make clean && make web`: just `index.html` and `index.js` (plus the preloaded data package, if you use one).

## Minimal `shell.html`

See `common/shell.html`

If you change the game resolution, update the canvas `width`/`height` attributes to match.
