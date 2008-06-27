#include <iostream>
#include <map>

#include "SDL.h"
#include "SDL_mixer.h"

#include "sound.hpp"

namespace sound {

namespace {
// number of allocated channels, 
const size_t NumChannels = 16;
const int SampleRate = 44100;

#ifdef WIN32
const size_t BufferSize = 4096;
#else
const size_t BufferSize = 1024;
#endif

bool sound_ok = false;

typedef std::map<std::string, Mix_Chunk*> cache_map;
cache_map cache;

Mix_Music* current_music = NULL;
std::string next_music;

//function which gets called when music finishes playing. It starts playing
//of the next scheduled track, if there is one.
void on_music_finished()
{
	std::cerr << "music finished...\n";
	Mix_FreeMusic(current_music);
	current_music = NULL;
	play_music(next_music);
	next_music.clear();
}

}

manager::manager()
{
	if(SDL_WasInit(SDL_INIT_AUDIO) == 0) {
		if(SDL_InitSubSystem(SDL_INIT_AUDIO) == -1) {
			sound_ok = false;
			std::cerr << "failed to init sound!\n";
			return;
		}
	}

	if(Mix_OpenAudio(SampleRate, MIX_DEFAULT_FORMAT, 2, BufferSize) == -1) {
		sound_ok = false;
		std::cerr << "failed to open audio!\n";
		return;
	}

	Mix_AllocateChannels(NumChannels);
	sound_ok = true;

	Mix_HookMusicFinished(on_music_finished);
	Mix_VolumeMusic(MIX_MAX_VOLUME);
}

manager::~manager()
{
	Mix_HookMusicFinished(NULL);
	next_music.clear();
	Mix_CloseAudio();
}

bool ok() { return sound_ok; }

void play(const std::string& file)
{
	if(!sound_ok) {
		return;
	}

	Mix_Chunk*& chunk = cache[file];
	if(chunk == NULL) {
		chunk = Mix_LoadWAV(("sounds/" + file).c_str());
		if(chunk == NULL) {
			return;
		}
	}

	Mix_PlayChannel(-1, chunk, 0);
}

void play_music(const std::string& file)
{
	if(file.empty()) {
		return;
	}

	if(current_music) {
		std::cerr << "fading out music...\n";
		next_music = file;
		Mix_FadeOutMusic(1000);
		return;
	}

	current_music = Mix_LoadMUS(("music/" + file).c_str());
	if(!current_music) {
		std::cerr << "Mix_LaadMUS ERROR loading " << file << ": " << Mix_GetError() << "\n";
		return;
	}

	std::cerr << "playing music: " << file << "\n";

	Mix_FadeInMusic(current_music, -1, 1000);
}

}
