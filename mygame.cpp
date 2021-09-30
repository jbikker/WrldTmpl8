#include "precomp.h"
#include "mygame.h"

Game* CreateGame() { return new MyGame(); }

uint sprite, frame = 0;

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void MyGame::Init()
{
    // This function is for functionality that you only want to run once,
    // at the start of your game. You can setup the scene here, load some
    // sprites, and so on.
	ClearWorld();
    sprite = LoadSprite( "assets/rock.vox" ); // this one has 32 frames
    LookAt( make_float3( 280, 128, 50 ), make_float3( 320, 128, 512 ) );
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void MyGame::Tick( float deltaTime )
{
	// This function gets called once per frame by the template code.
	Print( "Hello World!", 280, 128, 512, 1 );
	MoveSpriteTo( sprite, 320, 80, 512 );
	SetSpriteFrame( sprite, frame / 4 );
	frame = (frame + 1) % 128;
}