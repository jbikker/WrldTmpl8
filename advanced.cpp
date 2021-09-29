#include "precomp.h"
#include "advanced.h"

Game* CreateGame() { return new Advanced(); }

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void Advanced::Init()
{
	// build a simple world for testing inside the default shoebox
	uint colors[] = { RED, GREEN, BLUE, YELLOW, LIGHTRED, LIGHTBLUE, WHITE };
	ClearWorld();
	for( int x = 0; x < 10; x++ ) for( int z = 0; z < 10; z++ ) for( int y = 0; y < 2; y++ )
		Sphere( x * 96.0f + 32, y * 96.0f + 48, z * 96.0f + 32, 28.0f, colors[RandomUInt() % 7] );
    LookAt( make_float3( 280, 100, 50 ), make_float3( 380, 128, 512 ) );
	// ADVANCED - disable automatic rendering: we will spawn our own rays
	autoRendering = false;
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Advanced::Tick( float deltaTime )
{
	// spawn camera rays
	Ray* rays = GetBatchBuffer();
	const float3 p0 = make_float3( -1, 1, 2 );
	const float3 p1 = make_float3(  1, 1, 2 );
	const float3 p2 = make_float3( -1, -1, 2 );
	for( int i = 0, y = 0; y < SCRHEIGHT; y++ ) for( int x = 0; x < SCRWIDTH; x++, i++ )
	{
		const float2 uv = make_float2( (float)x / SCRWIDTH, (float)y / SCRHEIGHT );
		rays[i].O = make_float3( 420, 100, 50 ); // camera position
		rays[i].D = normalize( p0 + uv.x * (p1 - p0) + uv.y * (p2 - p0) );
		rays[i].t = 1e34f; // basically infinity
		rays[i].pixelIdx = i; // not useful for primary rays, but needed for later waves
	}
	// trace the ray batch
	Intersection* result = TraceBatch( SCRWIDTH * SCRHEIGHT );
	// process the result
	for( int i = 0, y = 0; y < SCRHEIGHT; y++ ) for( int x = 0; x < SCRWIDTH; x++, i++ )
	{
		// extract hit data
		uint color = RGB16to32( result[i].GetVoxel() );
		if (color == 0)
		{
			// ray missed the scene; probe the skydome
			screen->Plot( x, y, 0x4444ff /* blue */ );
		}
		else
		{
			float t = result[i].GetDistance();
			float3 N = result[i].GetNormal();
			screen->Plot( x, y, color );
		}
	}
}