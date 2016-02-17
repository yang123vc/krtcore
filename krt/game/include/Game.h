#pragma once

// Game things go here!

#define GAME_NUM_STREAMING_CHANNELS     4

#include "vfs\Device.h"

#include "Streaming.h"
#include "TexDict.h"
#include "ModelInfo.h"

#include "Console.Commands.h"

namespace krt
{

class Game
{
public:
    Game( void );
    ~Game( void );

    std::string GetGamePath( std::string relPath );

    void RunCommandFile( std::string relDir );

    inline streaming::StreamMan& GetStreaming( void )           { return this->streaming; }
    inline TextureManager& GetTextureManager( void )            { return this->texManager; }
    inline ModelManager& GetModelManager( void )                { return this->modelManager; }

    void LoadIMG( std::string relPath );

    void LoadIDEFile( std::string relPath );
    void LoadIPLFile( std::string relPath );

    std::string GetDevicePathPrefix( void )                     { return "gta3:/"; }

private:
    std::string gameDir;

    streaming::StreamMan streaming;

    TextureManager texManager;
    ModelManager modelManager;
};

extern Game *theGame;

};