#include "precomp.h"
#include "frogger.h"

Game* game = new Frogger();
int ship;
int3 playerPos = make_int3( 512, 128, 512 );
int3 bulletPos[128], oldBulletPos[128];
int gunDelay = 0;
int gun = 0;
int bulletPtr = 0;
float angle = 0;
float3 option1Pos = make_float3( -9999 ), option2Pos;
float optionRadius;

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
	mat4 M = mat4::RotateX( angle ); // * mat4::RotateY( angle * 3 );
	if ((angle += 0.1f) > 2 * PI) angle -= 2 * PI;
	if (option1Pos.x != -9999)
	{
		Sphere( option1Pos, 4, 0 );
		Sphere( option2Pos, 4, 0 );
	}
	option1Pos = TransformPosition( make_float3( 0, 16, 0 ), M ) + playerPos;
	option2Pos = TransformPosition( make_float3( 0, -16, 0 ), M ) + playerPos;
	optionRadius = 3 + 0.75f * sinf( angle * 2 );
	Sphere( option1Pos, optionRadius, LIGHTBLUE );
	Sphere( option2Pos, optionRadius, LIGHTBLUE );
	// handle keys
	if (GetAsyncKeyState( VK_UP )) playerPos.y++;
	if (GetAsyncKeyState( VK_DOWN )) playerPos.y--;
	if (GetAsyncKeyState( VK_LEFT )) playerPos.x--;
	if (GetAsyncKeyState( VK_RIGHT )) playerPos.x++;
	// camera
	float3 camPos = make_float3( (playerPos.x + 512) * 0.5f, 128.1f, (playerPos.x - 192) * 0.5f );
	LookAt( camPos, make_float3( playerPos ) );
	// span bullets
	if (GetAsyncKeyState( 32 )) if (!gunDelay) 
	{
		int3 gunPos;
		if (gun == 0) gunPos = make_int3( playerPos.x + 10, playerPos.y + 1, playerPos.z + 3 );
		if (gun == 1) gunPos = make_int3( playerPos.x + 10, playerPos.y + 1, playerPos.z + 13 );
		if (gun == 2) gunPos = make_int3( option1Pos );
		if (gun == 3) gunPos = make_int3( option2Pos );
		bulletPos[bulletPtr++ & 127] = gunPos;
		gunDelay = 6, gun = (gun + 1) & 3;
	}
	// remove bullets at previous positions
	for( int i = 0; i < 128; i++ ) if (oldBulletPos[i].x != -9999) XLine( oldBulletPos[i], 24, 0 );
	// draw active bullets
	for( int i = 0; i < 128; i++ ) if (bulletPos[i].x != -9999)
	{
		XLine( bulletPos[i], 24, YELLOW );
		oldBulletPos[i] = bulletPos[i];
		if ((bulletPos[i].x += 10) > 1024) bulletPos[i].x = -9999;
	}
	// gun cooldown
	if (gunDelay > 0) gunDelay--;
}