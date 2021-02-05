#include "precomp.h"
#include "frogger.h"

Game* game = new Frogger();
int ship;
int3 playerPos = make_int3( 512, 128, 512 );

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void Frogger::Init()
{
	// kill the mouse pointer
	ShowCursor( false );
	// load a spaceship
	ship = LoadSprite( "assets/ship.vox" );
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Frogger::Tick( float deltaTime )
{
	MoveSpriteTo( ship, playerPos );
	if (GetAsyncKeyState( VK_UP )) playerPos.y++;
	if (GetAsyncKeyState( VK_DOWN )) playerPos.y--;
	if (GetAsyncKeyState( VK_LEFT )) playerPos.x--;
	if (GetAsyncKeyState( VK_RIGHT )) playerPos.x++;
	LookAt( make_float3( 512, 128.1f, 160 ), make_float3( playerPos ) );
}