#include "precomp.h"
#include "mygame.h"

Game* CreateGame() { return new MyGame(); }

uint ship, corvette, frame = 0;

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void MyGame::Init()
{
	// change the skydome - defaults can be found in precomp.h, class Game
	skyDomeImage = "assets/sky_17.hdr";
	skyDomeScale = 0.7f;
	// clear world geometry
	ClearWorld();
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void MyGame::Tick( float deltaTime )
{
	// free cam controls
	static float3 D( 0, 0, 1 ), O( 512, 512, 512 );
	float3 tmp( 0, 1, 0 ), right = normalize( cross( tmp, D ) ), up = cross( D, right );
	float speed = deltaTime * 0.1f;
	if (GetAsyncKeyState( 'W' )) O += speed * D; else if (GetAsyncKeyState( 'S' )) O -= speed * D;
	if (GetAsyncKeyState( 'A' )) O -= speed * right; else if (GetAsyncKeyState( 'D' )) O += speed * right;
	if (GetAsyncKeyState( 'R' )) O += speed * up; else if (GetAsyncKeyState( 'F' )) O -= speed * up;
	if (GetAsyncKeyState( VK_LEFT )) D = normalize( D - right * 0.025f * speed );
	if (GetAsyncKeyState( VK_RIGHT )) D = normalize( D + right * 0.025f * speed );
	if (GetAsyncKeyState( VK_UP )) D = normalize( D - up * 0.025f * speed );
	if (GetAsyncKeyState( VK_DOWN )) D = normalize( D + up * 0.025f * speed );
	LookAt( O, O + D );
}