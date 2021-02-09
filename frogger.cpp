#include "precomp.h"
#include "frogger.h"

Game* game = new Frogger(); // it's not frogger, I know :)

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void Frogger::Init()
{
	// kill the mouse pointer
	ShowCursor( false );
	// load a spaceship
	shipSprite = LoadSprite( "assets/ship.vox" );
	// init bullets
	for (int i = 0; i < 128; i++) bulletPos[i].x = oldBulletPos[i].x = -9999;
	// load tiles
	uint tile[12] = {
		LoadBigTile( "assets/tile00.vox" ), // white sphere
		LoadBigTile( "assets/tile01.vox" ), // small horizontal platform tile
		LoadBigTile( "assets/tile02.vox" ), // cylinder bottom left
		LoadBigTile( "assets/tile03.vox" ), // cylinder bottom right
		LoadBigTile( "assets/tile04.vox" ), // cylinder top right
		LoadBigTile( "assets/tile05.vox" ), // cylinder top left
		LoadBigTile( "assets/tile06.vox" ), // pillar bottom left
		LoadBigTile( "assets/tile07.vox" ), // pillar bottom right
		LoadBigTile( "assets/tile08.vox" ), // pillar top right
		LoadBigTile( "assets/tile09.vox" ), // pillar top left
		LoadBigTile( "assets/tile10.vox" ), // double horizontal platform, left
		LoadBigTile( "assets/tile11.vox" )  // double horizontal platform, right
	};
	// prepare level
	ClearWorld();
	for( int z = 28; z < 33; z++ ) DrawBigTiles( "1111111111111111111111111111111111111111", 4, 4, z );
	for( int y = 1; y < 10; y++ ) 
	{
		DrawBigTiles( "67   67   67   67   67   67   67", 10, y, 38 );
		DrawBigTiles( "98   98   98   98   98   98   98", 10, y, 39 );
		DrawBigTiles( "67   67   67   67   67   67", 10, y, 18 );
		DrawBigTiles( "98   98   98   98   98   98", 10, y, 19 );
	}
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Frogger::Tick( float deltaTime )
{
	// delete options at old location (before we may do a block scroll!)
	if (option1Pos.x != -9999)
	{
		Copy( 0, 1000, 0, 10, 1010, 10, make_int3( option1Pos ) - 5 );
		Copy( 20, 1000, 0, 30, 1010, 10, make_int3( option2Pos ) - 5 );
	}
	// remove bullets at previous positions
	for (int i = 0; i < 128; i++) if (oldBulletPos[i].x != -9999) XLine( oldBulletPos[i], 24, 0 );
	// 'block scroll' level
	playerPos.x += 1;
	if (++scroll == 16)
	{
		playerPos.x -= 16, scroll = 0;
		WorldXScroll( -16 /* must be multiples of BRICKDIM */ );
	}
	LookAt( make_float3( (float)playerPos.x, 128.1f, 92 ), make_float3( playerPos ) );
	// draw player ship
	MoveSpriteTo( shipSprite, playerPos );
	mat4 M = mat4::RotateX( optionAngle );
	if ((optionAngle += 0.1f) > 2 * PI) optionAngle -= 2 * PI;
	option1Pos = TransformPosition( make_float3( 0, 16, 0 ), M ) + playerPos;
	option2Pos = TransformPosition( make_float3( 0, -16, 0 ), M ) + playerPos;
	Copy( make_int3( option1Pos ) - 5, make_int3( option1Pos ) + 5, 0, 1000, 0 );
	Copy( make_int3( option2Pos ) - 5, make_int3( option2Pos ) + 5, 20, 1000, 0 );
	optionRadius = 3 + 0.75f * sinf( optionAngle * 2 );
	Sphere( option1Pos, optionRadius, LIGHTBLUE );
	Sphere( option2Pos, optionRadius, LIGHTBLUE );
	// handle keys
	if (GetAsyncKeyState( VK_UP )) playerPos.y++;
	if (GetAsyncKeyState( VK_DOWN )) playerPos.y--;
	if (GetAsyncKeyState( VK_LEFT )) playerPos.x--;
	if (GetAsyncKeyState( VK_RIGHT )) playerPos.x++;
	// spawn bullets
	if (gunDelay > 0) gunDelay--;
	if (GetAsyncKeyState( 32 )) if (!gunDelay)
	{
		int3 gunPos;
		if (gun == 0) gunPos = make_int3( playerPos.x + 16, playerPos.y + 1, playerPos.z + 3 );
		if (gun == 1) gunPos = make_int3( playerPos.x + 16, playerPos.y + 1, playerPos.z + 13 );
		if (gun == 2) gunPos = make_int3( option1Pos ) + make_int3( 7, 0, 0 );
		if (gun == 3) gunPos = make_int3( option2Pos ) + make_int3( 7, 0, 0 );
		bulletPos[bulletPtr++ & 127] = gunPos;
		gunDelay = 6, gun = (gun + 1) & 3;
	}
	// draw active bullets
	for (int i = 0; i < 128; i++) if (bulletPos[i].x != -9999)
	{
		XLine( bulletPos[i], 24, YELLOW );
		oldBulletPos[i] = bulletPos[i];
		if ((bulletPos[i].x += 10) > 1024) bulletPos[i].x = -9999;
	}
}