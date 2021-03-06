#include "StdInc.h"
#include "Game.h"

#include <cstdio>
#include <fstream>
#include <iostream>

#include "GameWindow.h"

#include "Console.CommandHelpers.h"
#include "Console.h"

#include "CdImageDevice.h"
#include "vfs/Manager.h"

#include "EventSystem.h"

#include "sys/Timer.h"

#include <src/rwgta.h>

#include "fonts/FontRenderer.h"

#include <Windows.h>

#pragma warning(disable : 4996)

namespace krt
{

Game* theGame = NULL;

Game::Game(const std::vector<std::pair<std::string, std::string>>& setList) : streaming(GAME_NUM_STREAMING_CHANNELS), texManager(streaming), modelManager(streaming, texManager), colStore(streaming)
{
	assert(theGame == NULL);

	// Initialize console variables.
	maxFPSVariable    = std::make_unique<ConVar<int>>("maxFPS", ConVar_Archive, 60, &maxFPS);
	timescaleVariable = std::make_unique<ConVar<float>>("timescale", ConVar_None, 1.0f, &timescale);

	// Console variables for loading the default game universe.
	gameVariable     = std::make_unique<ConVar<std::string>>("gameName", ConVar_Archive, "gta3");
	gamePathVariable = std::make_unique<ConVar<std::string>>("gamePath", ConVar_Archive, "");

	// We can only have one game :)
	theGame = this;

	// Initialize RW.
	rw::platform     = rw::PLATFORM_D3D9;
	rw::loadTextures = true;

	gta::attachPlugins();

	// Keep track of the active camera, because only one camera can be active at a time in our engine.
	this->activeCam = NULL;

	// Prepare main world camera. (NOT FINAL).
	worldCam.Initialize();

	worldCam.SetAspectRatio(16.0f / 9.0f);
	worldCam.SetFOV(65.0f);
	worldCam.SetFarClip(1500.0f);

	// mount the user directory
	MountUserDirectory();

	// run config.cfg
	console::ExecuteSingleCommand(ProgramArguments{"exec", "user:/config.cfg"});

	// override variables from console
	for (auto& pair : setList)
	{
		console::ExecuteSingleCommand(ProgramArguments{"set", pair.first, pair.second});
	}

	// Set up game related things.
	LIST_CLEAR(this->activeEntities.root);

	// Do a test that loads all game models.
	//modelManager.LoadAllModels();
}

Game::~Game(void)
{
	assert(this->activeCam == NULL);

	// Delete important RW resources owned by the game.
	worldCam.Shutdown();

	// Delete all our entities.
	{
		while (!LIST_EMPTY(this->activeEntities.root))
		{
			Entity* entity = LIST_GETITEM(Entity, this->activeEntities.root.next, gameNode);

			// It will remove itself from the list.
			delete entity;
		}
	}

	// There is no more game.
	theGame = NULL;
}

void RenderingTest(void* gfxDevice);

void Game::Run()
{
	sys::TimerContext timerContext;

	EventSystem eventSystem;

	std::unique_ptr<GameWindow> gameWindow = GameWindow::Create("ATG: TheGame", 1280, 720, &eventSystem);
	void* gfxContext                       = gameWindow->CreateGraphicsContext();

	TheFonts->Initialize(CreateGameInterface(gameWindow.get()));

	eventSystem.RegisterEventSourceFunction([&]() {
		gameWindow->ProcessEvents();
	});

	// run the main game loop
	bool wantsToExit  = false;
	uint64_t lastTime = 0;

	this->lastGameTime = 0;

	// exit command
	ConsoleCommand quitCommand("quit", [&]() {
		wantsToExit = true;
	});

	while (!wantsToExit)
	{
		// limit frame rate and handle events
		// TODO: non-busy wait?
		uint32_t minMillis = (this->maxFPS > 0) ? (1000u / (uint32_t) this->maxFPS) : 1u; // we can cast to unsigned because its bigger than zero.
		uint32_t millis    = 0;

		uint64_t thisTime = 0;

		gameWindow->ProcessEventsOnce();

		do
		{
			thisTime = eventSystem.HandleEvents();

			millis = (uint32_t)(thisTime - lastTime);

			YieldThreadForShortTime();
		} while (millis < minMillis);

		// handle time scaling
		float scale = this->timescale; // to be replaced by a console value, again
		millis      = (uint32_t)((float)millis * scale);

		if (millis < 1)
		{
			millis = 1;
		}
		// prevent too big jumps from being made
		else if (millis > 5000)
		{
			millis = 5000;
		}

		if (millis > 500)
		{
			console::Printf("long frame: %d millis\n", millis);
		}

		// store timing values for this frame
		this->dT            = millis / 1000.0f;
		this->lastFrameTime = millis;

		this->lastGameTime += millis;

		lastTime = thisTime;

		// execute the command buffer for the global console
		console::ExecuteBuffer();

		// try saving changed console variables
		console::SaveConfigurationIfNeeded("user:/config.cfg");

		// load the game universe if variables are valid
		LoadUniverseIfAvailable();

		// rendering test
		if (this->universes.size() > 0)
		{
			//RenderingTest(gfxContext);
			theGame->GetWorld()->RenderWorld(gfxContext);
		}

		// whatever else might come to mind
	}
}

void Game::LoadUniverseIfAvailable()
{
	// exit if we already have an universe
	if (this->universes.size() > 0)
	{
		return;
	}

	// store variables
	std::string gameName   = this->gameVariable->GetValue();
	std::string gamePath   = this->gamePathVariable->GetValue() + "/";
	std::string configFile = "data/gta.dat";

	if (gameName == "gta3")
	{
		configFile = "data/gta3.dat";
	}
	else if (gameName == "gtavc")
	{
		configFile = "data/gta_vc.dat";
	}

	// is the variable even set?
	if (gamePath == "/")
	{
		return;
	}

	// verify the game directory existing
	{
		vfs::StreamPtr stream = vfs::OpenRead(gamePath + configFile);

		if (!stream)
		{
			// reset the game path
			this->gamePathVariable->GetHelper()->SetRawValue("");

			// print a warning
			console::PrintWarning("Invalid %s game path: %s\n", gameName.c_str(), gamePath.c_str());

			return;
		}
	}

	// set game directory
	gameDir = gamePath;

	// create the game universe
	GameConfiguration configuration;
	configuration.gameName = gameName;
	configuration.rootPath = gameDir;

	configuration.imageFiles.push_back("models/txd.img");
	configuration.imageFiles.push_back("models/gta3.img");
	configuration.imageFiles.push_back("models/gta_int.img");

	configuration.configurationFiles.push_back(configFile);

	GameUniversePtr universe = AddUniverse(configuration);
	universe->Load();
}

GameUniversePtr Game::AddUniverse(const GameConfiguration& configuration)
{
	auto universe = std::make_shared<GameUniverse>(configuration);
	this->universes.push_back(universe);

	return universe;
}

GameUniversePtr Game::GetUniverse(const std::string& name)
{
	for (const GameUniversePtr& universe : this->universes)
	{
		if (universe->GetConfiguration().gameName == name)
		{
			return universe;
		}
	}

	return nullptr;
}

std::string Game::GetGamePath(std::string relPath)
{
	// Get some sort of relative directory from the game directory.
	// Note that we want the gameDir to have a slash at the end!
	return (this->gameDir + relPath);
}
};