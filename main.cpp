/*******************************************************************
*   main.cpp
*   KNES
*
*	Author: Kareem Omar
*	kareem.omar@uah.edu
*	https://github.com/komrad36
*
*	Last updated Dec 18, 2016
*******************************************************************/
//
// Lightweight but complete NES emulator. Straightforward implementation in a
// few thousand lines of C++.
//
// Written from scratch in a speedcoding challenge in just 72 hours.
// Intended to showcase low-level and 6502 emulation, basic game loop mechanics,
// audio, video, user interaction. Also provides a compact emulator
// fully open and free to study and modify.
//
// No external dependencies except for
// those needed for interfacing:
// 
// - PortAudio for sound (http://www.portaudio.com/)
// - GLFW for video (http://www.glfw.org/)
//
// If you compile GLFW yourself, be sure to specify
// shared build ('cmake -DBUILD_SHARED_LIBS=ON .')
// or you will enter dependency hell at link-time.
//
// Fully cross-platform. Tested on Windows and Linux.
//
// Fully playable, with CPU, APU, PPU emulated and 6 of the most common
// mappers supported (0, 1, 2, 3, 4, 7). Get a .nes v1 file and go!
//
// Written from scratch in a speedcoding challenge (72 hours!). This means
// the code is NOT terribly clean. Always loved the 6502 and wanted to try
// something crazy. Got it fully working, with 6 mappers, in 3 days.
//
// I tend not to like OO much, especially for speedcoding, so here it's pretty
// much only used for mapper polymorphism.
//
// Usage: KNES <rom_file>
//
// Keymap (modify as desired in 'main.cpp'):
// -------------------------------------
//  Up/Down/Left/Right   |  Arrow Keys
//  Start                |  Enter
//  Select               |  Right Shift
//  A                    |  Z
//  B                    |  X
//  Turbo A              |  S
//  Turbo B              |  D
// -------------------------------------
// Emulator keys:
//  Tilde                |  Fast-forward
//  Escape               |  Quit
//  ALT+F4               |  Quit
// -------------------------------------
//
// The display window can be freely resized at runtime.
// You can also set proper full-screen mode at the top
// of 'main.cpp', and also enable V-SYNC if you are
// experiencing tearing issues.
//
// I love the 6502 and am relatively confident in the CPU emulation
// but have much less knowledge about the PPU and APU
// and am sure at least a few things are wrong here and there.
//
// Feel free to correct and/or teach me about the PPU and APU!
//
// Major thanks to http://www.6502.org/ for CPU ref, and especially
// to http://nesdev.com/, which I basically spent the three days
// scouring every inch of, especially to figure out the mappers and PPU.
//

#include <iostream>
#include <portaudio.h>
#include <GLFW/glfw3.h>

#ifdef __linux__
#include "pa_linux_alsa.h"
#endif

#include "NES.h"

// NES generates 256 x 240 pixels.
// You are free to resize at runtime
// but you can also set a scale factor to init with.
// 3-5 is good.
constexpr int display_scale_factor = 4;

// Fullscreen is awesome!
// Off by default so console can be seen
// if something goes wrong.
constexpr bool fullscreen = false;

// Recommend off unless severe tearing.
constexpr bool v_sync = false;

// PortAudio callback
// writes out contents of non-blocking ring buffer filled by APU
// writes 0 in case of underrun.
static int paCallback(const void *in_buf, void *out_buf, unsigned long frames_per_buf, const PaStreamCallbackTimeInfo* time_info, PaStreamCallbackFlags status_flags, void *user_data) {
	static_cast<void>(in_buf);
	static_cast<void>(time_info);
	static_cast<void>(status_flags);

	PaUtilRingBuffer *data = reinterpret_cast<PaUtilRingBuffer*>(user_data);
	const unsigned long num_out = frames_per_buf << 1;
	const unsigned long num_avail = static_cast<unsigned long>(PaUtil_GetRingBufferReadAvailable(data));
	PaUtil_ReadRingBuffer(data, out_buf, num_avail > num_out ? num_out : num_avail);
	if (num_avail < num_out) memset(reinterpret_cast<float*>(out_buf) + num_avail, 0, sizeof(float) * (num_out - num_avail));
	
	return paContinue;
}

bool getKey(GLFWwindow* window, int key) {
	return glfwGetKey(window, key) == GLFW_PRESS;
}

//  Keymap (modify as desired)
// -------------------------------------
//  Up/Down/Left/Right   |  Arrow Keys
//  Start                |  Enter
//  Select               |  Right Shift
//  A                    |  Z
//  B                    |  X
//  Turbo A              |  S
//  Turbo B              |  D
// -------------------------------------
//  Emulator keys:
//  Tilde                |  Fast-forward
//  Escape               |  Quit
//  ALT+F4               |  Quit
// -------------------------------------
uint8_t getKeys(GLFWwindow* window, bool turbo) {
	uint8_t ret = getKey(window, GLFW_KEY_Z) || (turbo && getKey(window, GLFW_KEY_S));
	ret |= (getKey(window, GLFW_KEY_X) || (turbo && getKey(window, GLFW_KEY_D))) << 1;
	ret |= (getKey(window, GLFW_KEY_RIGHT_SHIFT)) << 2;
	ret |= (getKey(window, GLFW_KEY_ENTER)) << 3;
	ret |= (getKey(window, GLFW_KEY_UP)) << 4;
	ret |= (getKey(window, GLFW_KEY_DOWN)) << 5;
	ret |= (getKey(window, GLFW_KEY_LEFT)) << 6;
	ret |= (getKey(window, GLFW_KEY_RIGHT)) << 7;
	return ret;
}

uint8_t getJoy(int joy, bool turbo) {
	if (!glfwJoystickPresent(joy)) {
		return 0;
	}
	int count;
	const float* axes = glfwGetJoystickAxes(joy, &count);
	const unsigned char* buttons = glfwGetJoystickButtons(joy, &count);

	uint8_t ret = buttons[0] == 1 || (turbo && buttons[2] == 1);
	ret |= (buttons[1] == 1 || (turbo && buttons[3] == 1)) << 1;
	ret |= (buttons[6] == 1) << 2;
	ret |= (buttons[7] == 1) << 3;
	ret |= (axes[1] < -0.5f) << 4;
	ret |= (axes[1] > 0.5f) << 5;
	ret |= (axes[0] < -0.5f) << 6;
	ret |= (axes[0] > 0.5f) << 7;

	return ret;
}

void notifyPaError(const PaError err) {
	Pa_Terminate();
	std::cout << std::endl << "PortAudio error:" << std::endl;
	std::cout << "Error code: " << err << std::endl;
	std::cout << "Error message: " << Pa_GetErrorText(err) << std::endl;

	if ((err == paUnanticipatedHostError)) {
		const PaHostErrorInfo* hostErrorInfo = Pa_GetLastHostErrorInfo();
		std::cout << "Host info error code: " << hostErrorInfo->errorCode << std::endl;
		std::cout << "Host info error message: " << hostErrorInfo->errorText << std::endl << std::endl;
	}
}

void printState(NES* nes) {
	printf("\rSTATUS CPU PC=%hu APU DM=%hhu P1=%hhu P2=%hhu TR=%hhu NO=%hhu PPU BG=%hhu BL=%hhu SP=%hhu SL=%hhu",
	nes->cpu->PC,
	nes->apu->dmc.enabled,
	nes->apu->pulse1.enabled,
	nes->apu->pulse2.enabled,
	nes->apu->triangle.enabled,
	nes->apu->noise.enabled,
	nes->ppu->flag_show_background,
	nes->ppu->flag_show_left_background,
	nes->ppu->flag_show_sprites,
	nes->ppu->flag_show_left_sprites);
}

int main(int argc, char* argv[]) {
	if (argc != 2) {
		std::cout << "Usage: KNES <rom file>" << std::endl;
		return EXIT_FAILURE;
	}

	char* SRAM_path = new char[strlen(argv[1]) + 1];
	strcpy(SRAM_path, argv[1]);
	strcat(SRAM_path, ".srm");

	std::cout << "Initializing NES..." << std::endl;
	NES* nes = new NES(argv[1], SRAM_path);

	std::cout << "Initializing PortAudio..." << std::endl;
	PaError err = Pa_Initialize();
	if (err != paNoError) {
		notifyPaError(err);
		return EXIT_FAILURE;
	}

	PaStreamParameters outputParameters;
#ifdef _WIN32
	outputParameters.device = Pa_GetDefaultOutputDevice();
	// you can select a specific device this way
	//outputParameters.device = Pa_GetHostApiInfo(Pa_HostApiTypeIdToHostApiIndex(PaHostApiTypeId::paWASAPI))->defaultOutputDevice;
#else
	outputParameters.device = Pa_GetDefaultOutputDevice();
#endif

	if (outputParameters.device == paNoDevice) {
		std::cerr << "ERROR: no PortAudio device found." << std::endl;
		notifyPaError(err);
		return EXIT_FAILURE;
	}

	outputParameters.channelCount = 2;
	outputParameters.sampleFormat = paFloat32;
	outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
	outputParameters.hostApiSpecificStreamInfo = NULL;

	std::cout << "Opening audio stream..." << std::endl;
	PaStream *stream;
	err = Pa_OpenStream(
		&stream,
		NULL,
		&outputParameters,
		44100,
		256,
		paNoFlag,
		paCallback,
		&nes->apu->ring_buf);

	if (err != paNoError) {
		notifyPaError(err);
		return EXIT_FAILURE;
	}

	std::cout << "Initializing GLFW..." << std::endl;
	if (!glfwInit()) {
		std::cerr << "ERROR: Failed to initialize GLFW. Aborting." << std::endl;
		return EXIT_FAILURE;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

	std::cout << "Initializing GLFW window..." << std::endl;
	GLFWwindow* window;
	if (fullscreen) {
		GLFWmonitor* const primary = glfwGetPrimaryMonitor();
		const GLFWvidmode* const mode = glfwGetVideoMode(primary);
		glfwWindowHint(GLFW_RED_BITS, mode->redBits);
		glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
		glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
		glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
		window = glfwCreateWindow(mode->width, mode->height, "KNES", primary, nullptr);
		std::cout << "Fullscreen framebuffer created." << std::endl;
	}
	else {
		glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
		window = glfwCreateWindow(256 * display_scale_factor, 240 * display_scale_factor, "KNES", nullptr, nullptr);
		std::cout << "Window created." << std::endl;
	}

	if (!window) {
		std::cerr << "ERROR: Failed to create window. Aborting." << std::endl;
		return EXIT_FAILURE;
	}

	glfwMakeContextCurrent(window);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

	if (v_sync) {
		// wait 1 sync to flip buffer (V_SYNC)
		glfwSwapInterval(1);
		std::cout << "V_SYNC enabled." << std::endl;
	}
	else {
		// sync immediately for lower latency
		// may introduce tearing
		glfwSwapInterval(0);
		std::cout << "V_SYNC disabled." << std::endl;
	}

	int old_w, old_h, w, h;
	glfwGetFramebufferSize(window, &old_w, &old_h);
	std::cout << "Framebuffer reports initial dimensions " << old_w << "x" << old_h << '.' << std::endl;

	std::cout << "Creating display texture..." << std::endl;
	GLuint texture;
	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	// decrease pops on linux
#ifdef __linux__
	PaAlsa_EnableRealtimeScheduling(stream, 1);
#endif

	std::cout << "Starting audio stream..." << std::endl;
	Pa_StartStream(stream);
	if (err != paNoError) {
		notifyPaError(err);
		return EXIT_FAILURE;
	}

	double prevtime = 0.0;
	while (!glfwWindowShouldClose(window)) {
		const double time = glfwGetTime();
		const double dt = time - prevtime < 1.0 ? time - prevtime : 1.0;
		prevtime = time;

		const bool turbo = (nes->ppu->frame % 6) < 3;
		glfwPollEvents();
		nes->controller1->buttons = getKeys(window, turbo) | getJoy(GLFW_JOYSTICK_1, turbo);
		nes->controller2->buttons = getJoy(GLFW_JOYSTICK_2, turbo);

		if ((nes->ppu->frame & 3) == 0) printState(nes);

		// step the NES state forward by 'dt' seconds, or more if in fast-forward
		emulate(nes, getKey(window, GLFW_KEY_GRAVE_ACCENT) ? 4.0 * dt : dt);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 240, 0, GL_RGBA, GL_UNSIGNED_BYTE, nes->ppu->front);
		glfwGetFramebufferSize(window, &w, &h);
		if (w != old_w || h != old_h) {
			old_w = w;
			old_h = h;
			std::cout << std::endl << "Framebuffer reports resize to " << w << "x" << h << '.' << std::endl;
		}

		const float s1 = static_cast<float>(w) / 256.0f;
		const float s2 = static_cast<float>(h) / 240.0f;

		const float x = (s1 >= s2) ? s2 / s1 : 1.0f;
		const float y = (s1 >= s2) ? 1.0f : s1 / s2;

		glViewport(0, 0, w, h);
		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 1.0f);
		glVertex2f(-x, -y);
		glTexCoord2f(1.0f, 1.0f);
		glVertex2f(x, -y);
		glTexCoord2f(1.0f, 0.0f);
		glVertex2f(x, y);
		glTexCoord2f(0.0f, 0.0f);
		glVertex2f(-x, y);
		glEnd();

		glfwSwapBuffers(window);

		if (getKey(window, GLFW_KEY_ESCAPE)) glfwSetWindowShouldClose(window, GLFW_TRUE);
	}

	// save SRAM back to file
	if (nes->cartridge->battery_present) {
		std::cout << std::endl << "Writing SRAM..." << std::endl;
		FILE* fp = fopen(SRAM_path, "wb");
		if (fp == nullptr || (fwrite(nes->cartridge->SRAM, 8192, 1, fp) != 1)) {
			std::cout << "WARN: failed to save SRAM file!" << std::endl;
		}
		else {
			fclose(fp);
		}
	}

	std::cout << std::endl << "Stopping audio stream..." << std::endl;
	Pa_StopStream(stream);

	std::cout << "Closing audio stream..." << std::endl;
	Pa_CloseStream(stream);

	std::cout << "Terminating GLFW..." << std::endl;
	glfwTerminate();

	std::cout << "Terminating PortAudio..." << std::endl;
	Pa_Sleep(500L);
	Pa_Terminate();

	return EXIT_SUCCESS;
}
