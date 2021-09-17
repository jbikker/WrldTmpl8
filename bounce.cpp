#include "precomp.h"
#include "bounce.h"

Game* CreateGame() { return new Bouncer(); }

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void Bouncer::Init()
{
	ShowCursor( false );
	// default scene is a box; poke a hole in the ceiling
	for (int x = 256; x < 768; x++) for (int z = 256; z < 768; z++) Plot( x, 255, z, 0 );
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Bouncer::Tick( float deltaTime )
{
	LookAt( 300, 100, 300, (int)ballPos.x, 100, (int)ballPos.z );
	// bounce ball
	Sphere( ballPos.x, ballPos.y, ballPos.z, 25, 0 );
	ballPos += ballVel * 0.25f * deltaTime, ballVel.y -= 0.0025f * deltaTime;
	if (ballPos.y < 33) ballPos.y = 33, ballVel.y *= -0.97f, tree = 0, tx = (int)ballPos.x, tz = (int)ballPos.z, tr = RandomUInt() % 5 + 5;
	if (ballPos.x < 33) ballPos.x = 33, ballVel.x *= -1;
	if (ballPos.z < 33) ballPos.z = 33, ballVel.z *= -1;
	if (ballPos.x > 1023 - 33) ballPos.x = 1023 - 33, ballVel.x *= -1;
	if (ballPos.z > 1023 - 33) ballPos.z = 1023 - 33, ballVel.z *= -1;
	Sphere( ballPos.x, ballPos.y, ballPos.z, 25, LIGHTRED );
}