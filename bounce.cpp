#include "precomp.h"
#include "bounce.h"

Game* game = new Bouncer();

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void Bouncer::Init()
{
	ShowCursor( false );
	// default scene is a box; poke a hole in the ceiling
	for (int x = 256; x < 768; x++) for (int z = 256; z < 768; z++) GetWorld()->Set( x, 255, z, 0 );
	// init deer flock
	GetWorld()->LoadSprite( "assets/deer.vox" );
	for( int i = 0; i < 50; i++ ) 
	{
		GetWorld()->CloneSprite( 0 );
		dx[i] = RandomUInt() % 1000 + 1, dz[i] = i * 20 + 10;
		df[i] = RandomUInt() % (GetWorld()->SpriteFrameCount( 0 ) * 4);
	}
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Bouncer::Tick( float deltaTime )
{
	World* world = GetWorld();
	world->SetCameraMatrix( mat4::LookAt( make_float3( 512, 128, 512 ), make_float3( ballPos.x, 64, ballPos.z ) ) );
	// add a landscape; TODO: to make this happen in Init, we should send a full copy of the world after init.
	static int z = 0;
	if (++z < 1023) for( int x = 1; x < 1023; x++ )
	{
		const float f = noise2D( x * 0.5f, z * 0.5f ); // base layer
		const float r = noise2D( x * 15.0f + 5000, z * 15.0f ); // rocks
		int h1 = (int)(f * 512) - 16, h2 = h1 + (int)(r * 64) - 5;
		uint grassColor = (((h1 + h2) & 63) / 30 + 3) << 2, sandColor = 180 + ((h2 & 1) << 2);
		for( int y = 1; y < h1; y++ ) world->Set( x, y, z, h1 < 5 ? sandColor : grassColor );
		for( int y = max( 1, h1 ); y < h2; y++ ) world->Set( x, y, z, GREY );
		world->Set( x, 0, z, LIGHTBLUE );
	}
	// deer
	for( int i = 0; i < 100; i++ )
	{
		world->MoveSpriteTo( i, dx[i], 1, dz[i], df[i] >> 3 );
		if (++df[i] == world->SpriteFrameCount( 0 ) * 8) df[i] = 0;
		if (--dx[i] < 15) dx[i] = 990;
	}
	// bounce ball
	world->Sphere( ballPos.x, ballPos.y, ballPos.z, 25, 0 );
	ballPos += ballVel * 0.25f * deltaTime, ballVel.y -= 0.0025f * deltaTime;
	if (ballPos.y < 26) ballPos.y = 26, ballVel.y *= -0.97f, tree = 0, tx = (int)ballPos.x, tz = (int)ballPos.z, tr = RandomUInt() % 5 + 5;
	if (ballPos.x < 26) ballPos.x = 26, ballVel.x *= -1;
	if (ballPos.z < 26) ballPos.z = 26, ballVel.z *= -1;
	if (ballPos.x > 1023 - 26) ballPos.x = 1023 - 26, ballVel.x *= -1;
	if (ballPos.z > 1023 - 26) ballPos.z = 1023 - 26, ballVel.z *= -1;
	world->Sphere( ballPos.x, ballPos.y, ballPos.z, 25, (7 << 5) + (2 << 2) + 1 );
}