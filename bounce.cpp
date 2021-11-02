#include "precomp.h"
#include "bounce.h"

Game* CreateGame() { return new Bouncer(); }

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void Bouncer::Init()
{
	ShowCursor( false );
	skyDomeLightScale = 6.0f;
	// default scene is a box; punch a hole in the ceiling
	Box( 256, 240, 256, 768, 260, 768, 0 );
	// add some objects
	ship = LoadSprite( "assets/flyingapts.vx" ), corvette = LoadSprite( "assets/corvette.vx" );
	StampSpriteTo( ship, 200, 250, 400 );
	int c[15] = { 100, 280, 400, 500, 320, 350, 600, 480, 250, 390, 350, 530, 290, 310, 30 };
	for (int i = 0; i < 5; i++) StampSpriteTo( corvette, c[i * 3], c[i * 3 + 1], c[i * 3 + 2] );
	// restore camera, if possible
	FILE* f = fopen( "camera.dat", "rb" );
	if (f)
	{
		fread( &D, 1, sizeof( D ), f );
		fread( &O, 1, sizeof( O ), f );
		fclose( f );
	}
	// load spline path
	PathPoint p;
	FILE* fp = fopen( "assets/splinepath.bin", "rb" );
	while (1)
	{
		fread( &p.O, 1, sizeof( p.O ), fp );
		size_t s = fread( &p.D, 1, sizeof( p.O ), fp );
		if (s > 0) splinePath.push_back( p ); else break;
	}
	fclose( fp );
}

// -----------------------------------------------------------
// HandleInput: reusable input handling / free camera
// -----------------------------------------------------------
void Bouncer::HandleInput( float deltaTime )
{
	// free cam controls
	float3 tmp( 0, 1, 0 ), right = normalize( cross( tmp, D ) ), up = cross( D, right );
	float speed = deltaTime * 0.1f;
	if (GetAsyncKeyState( 'W' )) O += speed * D; else if (GetAsyncKeyState( 'S' )) O -= speed * D;
	if (GetAsyncKeyState( 'A' )) O -= speed * right; else if (GetAsyncKeyState( 'D' )) O += speed * right;
	if (GetAsyncKeyState( 'R' )) O += speed * up; else if (GetAsyncKeyState( 'F' )) O -= speed * up;
	if (GetAsyncKeyState( VK_LEFT )) D = normalize( D - right * 0.025f * speed );
	if (GetAsyncKeyState( VK_RIGHT )) D = normalize( D + right * 0.025f * speed );
	if (GetAsyncKeyState( VK_UP )) D = normalize( D - up * 0.025f * speed );
	if (GetAsyncKeyState( VK_DOWN )) D = normalize( D + up * 0.025f * speed );
#if 0
	// enable to set spline path points using P key
	static bool pdown = false;
	static FILE* pf = 0;
	if (!GetAsyncKeyState( 'P' )) pdown = false; else
	{
		if (!pdown) // save a point for the spline
		{
			if (!pf) pf = fopen( "spline.bin", "wb" );
			fwrite( &O, 1, sizeof( O ), pf );
			float3 t = O + D;
			fwrite( &t, 1, sizeof( t ), pf );
		}
		pdown = true;
	}
	LookAt( O, O + D );
#else
	// playback of recorded spline path
	const size_t N = splinePath.size();
	PathPoint p0 = splinePath[(pathPt + (N - 1)) % N], p1 = splinePath[pathPt];
	PathPoint p2 = splinePath[(pathPt + 1) % N], p3 = splinePath[(pathPt + 2) % N];
	LookAt( CatmullRom( p0.O, p1.O, p2.O, p3.O ), CatmullRom( p0.D, p1.D, p2.D, p3.D ) );
	if ((t += deltaTime * 0.0005f) > 1) t -= 1, pathPt = (pathPt + 1) % N;
#endif
}

// -----------------------------------------------------------
// Update lasers
// -----------------------------------------------------------
void Bouncer::UpdateLasers( float deltaTime )
{
	if (laserT > 0)
	{
		float3 D = normalize( make_float3( laserB - laserA ) );
		float3 T = make_float3( -D.y, D.x, 0 );
		float3 B = cross( D, T );
		for( int i = 0; i < 10; i++ )
		{
			float3 pos = laserA + D * laserT;
			for( int u = -4; u <= 4; u++ ) for( int v = -4; v <= 4; v++ ) for( int w = -4; w <= 4; w++ )
				Plot( (int)pos.x + u, (int)pos.y + v, (int)pos.z + w, RED );
			laserT -= 0.5f;
		}
	}
	else if ((laserDelay -= deltaTime) < 0) while (1)
	{
		laserA = make_int3( RandomUInt() & 1023, RandomUInt() & 1023, RandomUInt() & 1023 );
		laserB = make_int3( RandomUInt() & 1023, RandomUInt() & 1023, RandomUInt() & 1023 );
		if ((laserT = length( laserB - laserA )) > 512) break;
	}
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Bouncer::Tick( float deltaTime )
{
	// update camera
	HandleInput( deltaTime );
	// bounce ball
	Sphere( ballPos.x, ballPos.y, ballPos.z, 25, 0 );
	ballPos += ballVel * 0.25f * deltaTime, ballVel.y -= 0.0025f * deltaTime;
	if (ballPos.y < 33) ballPos.y = 33, ballVel.y *= -0.97f;
	if (ballPos.x < 33) ballPos.x = 33, ballVel.x *= -1;
	if (ballPos.z < 33) ballPos.z = 33, ballVel.z *= -1;
	if (ballPos.x > 1023 - 33) ballPos.x = 1023 - 33, ballVel.x *= -1;
	if (ballPos.z > 1023 - 33) ballPos.z = 1023 - 33, ballVel.z *= -1;
	Sphere( ballPos.x, ballPos.y, ballPos.z, 25, LIGHTRED );
	// update lasers
	// UpdateLasers( deltaTime );
}