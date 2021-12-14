#include "precomp.h"
#include "meaning.h"

Game* CreateGame() { return new MeaningOfLife(); }
uint spriteBase = LoadSprite( "assets/creature.vox" );

Creature::Creature( const int idx, const float3& p, const float3& t )
{
	position = p, cycle = -jump, target = t, id = idx, base = 0;
	int ix = (int)p.x, iz = (int)p.z;
	sprite = CloneSprite( spriteBase );
	for (int y = 260; y > 0; y--) if (Read( ix, y, iz )) { position.y = base = (float)y + 1; break; }
}

void Creature::Tick( const float deltaTime )
{
	cycle += 0.0075f * deltaTime; // advance bounce cycle from -2.5 to +inf
	if (cycle < -jump)
	{
		SetSpriteFrame( sprite, 0 );
	}
	else
	{
		SetSpriteFrame( sprite, cycle < 0 ? 1 : 2 );
		position.y = base + (jump * jump) - cycle * cycle;
		float3 dir = deltaTime * 0.01f * normalize( target - position );
		position.x += dir.x, position.z += dir.z;
		int3 voxelPos = make_int3( position );
		while (Read( voxelPos.x, voxelPos.y, voxelPos.z ) != 0 && voxelPos.y < 512) 
			base = position.y = (float)(++voxelPos.y), cycle = -jump - 1.3f;
		MoveSpriteTo( sprite, voxelPos );
	}
}

void Creature::Respawn()
{
	position.x = RandomFloat() * 20 + 512;
	position.y = base = 280;
	position.z = RandomFloat() * 20 + 512;
	uint side = RandomUInt() & 3;
	if (side == 0) target = make_float3( 0, 0, RandomFloat() * 1000 + 12 );
	if (side == 1) target = make_float3( 1023, 0, RandomFloat() * 1000 + 12 );
	if (side == 2) target = make_float3( RandomFloat() * 1000 + 12, 0, 0 );
	if (side == 3) target = make_float3( RandomFloat() * 1000 + 12, 0, 1023 );
	cycle = 0;
}

Creature creature[CREATURECOUNT];

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void MeaningOfLife::Init()
{
	// change the skydome - defaults can be found in precomp.h, class Game
	skyDomeImage = "assets/sky_16.hdr";
	skyDomeScale = 0.7f;
	skyDomeLightScale = 2.0f;
	// clear world geometry
	ClearWorld();
	// produce landscape
	Surface heights( "assets/heightmap.png" );
	Surface colours( "assets/colours.png" );
	uint* src1 = heights.buffer, * src2 = colours.buffer;
	for (int x = 0; x < 1024; x++) for (int z = 0; z < 1024; z++)
	{
		const int h = (*src1++ & 255) + 2;
		const uint c = RGB32to16( *src2++ );
		for (int y = 0; y < h - 4; y++) Plot( x, y, z, WHITE );
		for (int y = h - 4; y < h; y++) Plot( x, y, z, c );
	}
	// add creatures
	for (int i = 0; i < CREATURECOUNT; i++) 
	{
		creature[i] = Creature( i, make_float3( 0 ), make_float3( 0 ) );
		creature[i].Respawn();
	}
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void MeaningOfLife::Tick( float deltaTime )
{
	int test = sizeof( Creature );
	// from a distance
	static float r = 0, camDist = 800;
	float a = r * PI / 180;
	float3 camPos = make_float3(
		camDist * (sinf( a ) + cosf( a )) + 512, 250,
		camDist * (cosf( a ) - sinf( a )) + 512
	);
	LookAt( camPos, make_float3( 512, 50, 512 ) );
	if (GetAsyncKeyState( VK_LEFT )) r -= 0.05f * deltaTime; if (r < 0) r += 360;
	if (GetAsyncKeyState( VK_RIGHT )) r += 0.05f * deltaTime; if (r > 360) r -= 360;
	if (GetAsyncKeyState( VK_UP )) camDist -= 0.5f * deltaTime; if (camDist < 80) camDist = 80;
	if (GetAsyncKeyState( VK_DOWN )) camDist += 0.5f * deltaTime; if (camDist > 600) camDist = 600;
	// update creatures
	for (int i = 0; i < CREATURECOUNT; i++) creature[i].Tick( deltaTime );
}