#include "precomp.h"
#include "benchmark.h"

Game* CreateGame() { return new Benchmark(); }

uint sprite, frame = 0;

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void Benchmark::Init()
{
#if GIRAYS > 0
	FatalError( "Disable GIRAYS and TAA for an accurate performance measurement." );
#endif
#if TAA > 0
	FatalError( "Disable TAA and GIRAYS for an accurate performance measurement." );
#endif
    ClearWorld();
	uint colors[] = { RED, GREEN, BLUE, YELLOW, LIGHTRED, LIGHTBLUE, WHITE };
	for( int i = 0; i < 500; i++ )
	{
		int x = RandomUInt() % 800 + 100;
		int y = RandomUInt() % 800 + 100;
		int z = RandomUInt() % 800 + 100;
		int r = RandomUInt() % 20 + 20;
		Sphere( (float)x, (float)y, (float)z, (float)r, colors[RandomUInt() % (sizeof( colors ) / 4)] );
	}
    LookAt( make_float3( 20, 20, 20 ), make_float3( 512, 512, 512 ) );
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Benchmark::Tick( float deltaTime )
{
	float s = GetRenderTime();
	int rays = SCRWIDTH * SCRHEIGHT * AA_SAMPLES * AA_SAMPLES /* AA */;
	float Mrays = rays / 1000000.0f;
	static float smoothed = 0, frameIdx = 0;
	if (++frameIdx < 10) smoothed = Mrays / s; else smoothed = 0.95f * smoothed + 0.05f * Mrays / s;
	printf( "rendering statistics: %4.2fms, %4.1fMrays (%4.1fMrays/s)\n", s * 1000, Mrays, smoothed );
}