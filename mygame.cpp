#include "precomp.h"
#include "mygame.h"

Game* game = new MyGame();

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void MyGame::Init()
{
	ShowCursor( false );
	// default scene is a box; poke a hole in the ceiling
	for (int x = 256; x < 768; x++) for (int z = 256; z < 768; z++) GetWorld()->Set( x, 255, z, 0 );
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void MyGame::Tick( float deltaTime )
{
	World* world = GetWorld();
	world->SetCameraMatrix( mat4::LookAt( make_float3( 512, 128, 512 ), make_float3( ballPos.x, 64, ballPos.z ) ) );
	// bounce ball
	world->Sphere( ballPos.x, ballPos.y, ballPos.z, 25, 0 );
	ballPos += ballVel * 0.25f * deltaTime, ballVel.y -= 0.0025f * deltaTime;
	if (ballPos.y < 26) ballPos.y = 26, ballVel.y *= -0.97f;
	if (ballPos.x < 26) ballPos.x = 26, ballVel.x *= -1;
	if (ballPos.z < 26) ballPos.z = 26, ballVel.z *= -1;
	if (ballPos.x > 1023 - 26) ballPos.x = 1023 - 26, ballVel.x *= -1;
	if (ballPos.z > 1023 - 26) ballPos.z = 1023 - 26, ballVel.z *= -1;
	world->Sphere( ballPos.x, ballPos.y, ballPos.z, 25, (7 << 5) + (2 << 2) + 1 );
}