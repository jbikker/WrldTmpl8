#include "precomp.h"
#include "mygame.h"

Game* game = new MyGame();

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void MyGame::Init()
{
	// This function is for functionality that you only want to run once,
	// at the start of your game. You can setup the scene here, load some
	// sprites, and so on.
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void MyGame::Tick( float deltaTime )
{
	// This function gets called once per frame by the template code.
	World* world = GetWorld();
	Print( "Hello World!", 280, 128, 512, 1 );
	LookAt( make_float3( 280, 128, 50 ), make_float3( 320, 128, 512 ) );
}