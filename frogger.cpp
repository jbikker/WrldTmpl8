#include "precomp.h"
#include "frogger.h"

Game* game = new Frogger();
int ship;
int3 playerPos = make_int3( 512, 128, 512 );
int3 bulletPos[128], oldBulletPos[128];
int gunDelay = 0;
int gun = 0;
int bulletPtr = 0;

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void Frogger::Init()
{
	// kill the mouse pointer
	ShowCursor( false );
	// load a spaceship
	ship = LoadSprite( "assets/ship.vox" );
	// init bullets
	for( int i = 0; i < 128; i++ ) bulletPos[i].x = oldBulletPos[i].x = -9999;
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Frogger::Tick( float deltaTime )
{
	// draw player ship
	MoveSpriteTo( ship, playerPos );
	// handle keys
	if (GetAsyncKeyState( VK_UP )) playerPos.y++;
	if (GetAsyncKeyState( VK_DOWN )) playerPos.y--;
	if (GetAsyncKeyState( VK_LEFT )) playerPos.x--;
	if (GetAsyncKeyState( VK_RIGHT )) playerPos.x++;
	// camera
	LookAt( make_float3( 512 + (playerPos.x - 512) / 2, 128.1f, 160 + (playerPos.x - 512) / 2 ), make_float3( playerPos ) );
	// span bullets
	if (GetAsyncKeyState( 32 )) if (!gunDelay) 
	{
		bulletPos[bulletPtr++] = make_int3( playerPos.x + 10, playerPos.y, playerPos.z + 3 + gun * 10 );
		gunDelay = 10, gun = 1 - gun;
	}
	// remove bullets at previous positions
	for( int i = 0; i < 128; i++ ) if (oldBulletPos[i].x != -9999)
		for( int x = 0; x < 24; x++ ) Plot( oldBulletPos[i].x + x, bulletPos[i].y, bulletPos[i].z, 0 );
	// draw active bullets
	for( int i = 0; i < 128; i++ ) if (bulletPos[i].x != -9999)
	{
		for( int x = 0; x < 24; x++ ) Plot( bulletPos[i].x + x, bulletPos[i].y, bulletPos[i].z, YELLOW );
		oldBulletPos[i] = bulletPos[i];
		if ((bulletPos[i].x += 10) > 1024) bulletPos[i].x = -9999;
	}
	// gun cooldown
	if (gunDelay > 0) gunDelay--;
}