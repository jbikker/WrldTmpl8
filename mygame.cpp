#include "precomp.h"
#include "mygame.h"
 
Game* game = new MyGame();
uint sprite, frame = 0;
 
// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void MyGame::Init()
{
    // This function is for functionality that you only want to run once,
    // at the start of your game. You can setup the scene here, load some
    // sprites, and so on.
    sprite = LoadSprite( "assets/rock.vox" ); // this one has 32 frames
    LookAt( make_float3( 280, 128, 50 ), make_float3( 320, 128, 512 ) );
}

void MyGame::DrawI( int x, int y, int z, int color )
{
	Line( x, y, z + 400, x + 60, y, z + 400, color );
	Line( x + 30, y, z + 400, 270, y, z, color );
	Line( x, y, z, x + 60, y, z, color );
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void MyGame::Tick( float deltaTime )
{
	DrawI( 240, 20, 400, 1 );
}