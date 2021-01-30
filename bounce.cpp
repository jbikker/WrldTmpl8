#include "template.h"
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
	// init snow
	for( int i = 0; i < 3000; i++ ) 
		sx[i] = RandomUInt() % 1000 + 12, sy[i] = RandomUInt() % 1000 + 3, sz[i] = RandomUInt() % 1000 + 12;
}

// -----------------------------------------------------------
// Handle mouse movement
// -----------------------------------------------------------
static float3 camPos = make_float3( 512, 128, 512 );
mat4 Bouncer::MouseLook()
{
	static bool firstMouse = true;
	static float yaw = 0, pitch = 0, sensitivity = 0.004f;
	POINT mouse;
	GetCursorPos( &mouse );
	float2 offset = make_float2( make_int2( mouse.x, mouse.y ) - make_int2( SCRWIDTH / 2, SCRHEIGHT / 2 ) );
	SetCursorPos( SCRWIDTH / 2, SCRHEIGHT / 2 );
	yaw += offset.x * sensitivity, pitch += offset.y * sensitivity;
	if (pitch > TWOPI) pitch -= TWOPI; else if (pitch < 0) pitch += TWOPI;
	if (yaw > TWOPI) yaw -= TWOPI; else if (yaw < 0) yaw += TWOPI;
	return mat4::Translate( camPos ) * mat4::RotateY( yaw ) * mat4::RotateX( pitch );
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
	world->Print( "Hello World!", 800, 80, 5, 1 );
	world->Print( "Hello World!", 800, 80, 1020, 1 );
	// grow tree
	uint r = tr - (tree / 26);
	if (tree > -1 && r > 1) 
	{
		#define R(x) RandomUInt()%(x)
		world->Sphere( (float)tx, tree++ / 2.0f, (float)tz, (float)r, BROWN );
		if (!(R(15))) world->Sphere( (float)tx + (R(3) - 1) * (3 + r), tree / 2.0f, (float)tz + (R(3) - 1) * (3 + r), R(3) + 3.0f, R(8) ? GREEN : RED );
		tx += R(5) - 2, tz += R(5) - 2;
	}
	// snow
	for( int i = 0; i < 3000; i++ )
	{
		world->Set( sx[i], sy[i], sz[i], 0 );
		if (world->Get( sx[i], --sy[i], sz[i] )) 
		{
			world->Set( sx[i], sy[i], sz[i], WHITE );
			sx[i] = RandomUInt() % 1000 + 12, sy[i] = 255, sz[i] = RandomUInt() % 1000 + 12;
		}
		world->Set( sx[i], sy[i], sz[i], WHITE );
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