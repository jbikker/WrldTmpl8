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
	// add a plane to debug the CPU traversal code
	for( int x = 100; x < 900; x++ ) for( int z = 100; z < 900; z++ ) Plot( x, 50, z, GREEN );
	// disable automatic rendering: we will spawn our own rays
	autoRendering = false;
}

// screen plane corners - relative to (0,0,0)
static float3 p0 = make_float3( -1, 1, 2 );
static float3 p1 = make_float3(  1, 1, 2 );
static float3 p2 = make_float3( -1, -1, 2 );

// -----------------------------------------------------------
// CPU-only ray tracing
// -----------------------------------------------------------
void Advanced::CPURays()
{
	Ray ray;
	ray.O = make_float3( 420, 100, 50 ); // camera position for all rays
	for( int y = 0; y < SCRHEIGHT; y++ ) for( int x = 0; x < SCRWIDTH; x++ )
	{
		// setup primary ray for pixel [x,y]
		float u = (float)x / SCRWIDTH;
		float v = (float)y / SCRHEIGHT;
		ray.D = normalize( p0 + u * (p1 - p0) + v * (p2 - p0) );
		ray.t = 1e34f;
		// trace the ray
		Intersection intersection = Trace( ray );
		// visualize result
		if (intersection.GetVoxel() == 0)
		{
			// ray missed the scene; blue sky
			screen->Plot( x, y, 0x4444ff /* light blue */ );
		}
		else
		{
			float t = intersection.GetDistance(); // TODO: use
			float3 N = intersection.GetNormal(); // TODO: use
			screen->Plot( x, y, ((int)((N.x + 1) * 127) << 16) + ((int)((N.y + 1) * 127) << 8) + (int)((N.z + 1) * 127) );
			// screen->Plot( x, y, RGB16to32( intersection.GetVoxel() ) );
		}
	}
	// TODO: Whitted
}

// -----------------------------------------------------------
// GPU-assisted ray tracing
// -----------------------------------------------------------
void Advanced::GPURays()
{
	// spawn all camera rays
	Ray* rays = GetBatchBuffer();
	for( int i = 0, y = 0; y < SCRHEIGHT; y++ ) for( int x = 0; x < SCRWIDTH; x++, i++ )
	{
		float2 uv = make_float2( (float)x / SCRWIDTH, (float)y / SCRHEIGHT );
		rays[i].O = make_float3( 420, 100, 50 ); // camera position
		rays[i].D = normalize( p0 + uv.x * (p1 - p0) + uv.y * (p2 - p0) );
		rays[i].t = 1e34f; // basically infinity
		rays[i].pixelIdx = i; // not useful for primary rays, but needed for later waves
	}
	// trace the ray batch using the GPU
	Intersection* result = TraceBatch( SCRWIDTH * SCRHEIGHT );
	// process results
	for( int i = 0, y = 0; y < SCRHEIGHT; y++ ) for( int x = 0; x < SCRWIDTH; x++, i++ )
	{
		if (result[i].GetVoxel() == 0)
		{
			// ray missed the scene; blue sky
			screen->Plot( x, y, 0x4444ff /* light blue */ );
		}
		else
		{
			float t = result[i].GetDistance(); // TODO: use
			float3 N = result[i].GetNormal(); // TODO: use
			screen->Plot( x, y, ((int)((N.x + 1) * 127) << 16) + ((int)((N.y + 1) * 127) << 8) + (int)((N.z + 1) * 127) );
			// screen->Plot( x, y, RGB16to32( result[i].GetVoxel() ) );
		}
	}
	// TODO: Whitted
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Advanced::Tick( float deltaTime )
{
	Timer t;
	// CPURays();
	GPURays();
	printf( "%5.2fms\n", t.elapsed() * 1000 );
}