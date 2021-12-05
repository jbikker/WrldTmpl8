#include "precomp.h"
#include "meaning.h"

Game* CreateGame() { return new MeaningOfLife(); }

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void MeaningOfLife::Init()
{
	// clear world geometry
	ClearWorld();
	// produce landscape
	Surface s( "assets/heightmap.png" );
	Surface c( "assets/colours.png" );
	for (int x = 0; x < 1024; x++) for (int z = 0; z < 1024; z++)
	{
		int h = (s.buffer[x + z * 1024] & 255) + 2;
		uint a = RGB32to16( c.buffer[x + z * 1024] );
		for (int y = 0; y < h; y++) Plot( x, y, z, a );
	}
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void MeaningOfLife::Tick( float deltaTime )
{
	// from a distance
	static float r = 0, camDist = 800;
	float a = r * PI / 180;
	float3 camPos = make_float3(
		camDist * (sinf( a ) + cosf( a )) + 512,
		250,
		camDist * (cosf( a ) - sinf( a )) + 512
	);
	LookAt( camPos, make_float3( 512, 50, 512 ) );
	if (GetAsyncKeyState( VK_LEFT )) r -= 0.05f * deltaTime; if (r < 0) r += 360;
	if (GetAsyncKeyState( VK_RIGHT )) r += 0.05f * deltaTime; if (r > 360) r -= 360;
	if (GetAsyncKeyState( VK_UP )) camDist -= 0.5f * deltaTime; if (camDist < 80) camDist = 80;
	if (GetAsyncKeyState( VK_DOWN )) camDist += 0.5f * deltaTime; if (camDist > 600) camDist = 600;
	printf( "x: %5.2f, y: %5.2f, z: %5.2f\n", camPos.x, camPos.y, camPos.z );
}