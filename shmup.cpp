#include "precomp.h"
#include "shmup.h"
#include "irrklang.h"
using namespace irrklang;

Game* CreateGame() { return new SHMUP(); }

ISoundEngine* engine;
ISoundSource* pew = 0, * rumble = 0, * explosion = 0, * boom = 0, * shot = 0;
ISoundSource* alarm = 0, * charge = 0;

// actor implementations

bool Laser::Tick( int worldPos )
{
	if (relPos.x == OUTOFRANGE) return true; // beam is inactive
	MoveSpriteTo( sprite, SHMUP::Corrected( relPos ) );
	if ((relPos.x += 10) > 1024) relPos.x = OUTOFRANGE;
	return true;
}

bool Bullet::Tick( int worldPos )
{
	if (state == ACTIVE)
	{
		MoveSpriteTo( sprite, SHMUP::Corrected( relPos >> 8 ) );
		relPos += make_int3( velocity.x - 256, 0, velocity.y );
		if (relPos.x < 0 || relPos.z < 0 || relPos.x >( 1024 << 8 ) || relPos.z >( 1024 << 8 )) state = INACTIVE;
	}
	return true;
}

bool Rock::Tick( int worldPos )
{
	// basic behavior
	if (state == INACTIVE)
	{
		if (worldPos == startPos) state = FLYING, relPos.x = 900 << 8, relPos.y = 50 << 8;
	}
	else if (state == KILLED)
	{
		for (int i = 0; i < 1024; i++)
		{
			SetParticle( particles, i, SHMUP::Corrected( make_int3( pos[i] ) ), color[i] );
			pos[i] += speed[i], speed[i].x -= 0.11f;
		}
	}
	else /* state == FLYING */
	{
		SetSpriteFrame( sprite, (frame = (frame + 2) & 255) >> 3 );
		MoveSpriteTo( sprite, SHMUP::Corrected( relPos >> 8 ) );
		if (((relPos.x -= 256) >> 8) < 100) { state = INACTIVE; return true; }
		// check for bullet impact
		for (int i = 0; i < MAXBEAMS; i++) if (SpriteHit( sprite, game->laser[i]->sprite ))
		{
			state = KILLED;
			RemoveSprite( sprite );
			engine->play2D( explosion );
			// copy frame to debris
			int3 frameSize = GetSpriteFrameSize( sprite );
			int particleIdx = 0, v;
			for (int j = 0; j < 4; j++) for (int z = 0; z < frameSize.z; z++) for (int y = 0; y < frameSize.y; y++)
				for (int x = j + ((y + z) & 3); x < frameSize.x; x += 4) if (v = GetSpriteVoxel( sprite, x, y, z ))
				{
					pos[particleIdx] = make_float3( make_int3( x, y, z ) + (relPos >> 8) ), color[particleIdx] = v;
					speed[particleIdx] = normalize( make_float3( RandomFloat() - 0.5f, 0, RandomFloat() - 0.5f ) ) * 0.5f;
					if (++particleIdx == 1024) goto break_all; // there's no break(4) in c++
				}
		break_all:;
		}
	}
	return true;
}

bool Wind::Tick( int worldPos )
{
	if (!initialized)
	{
		for (int z = 0; z < 512; z++) dust[z] = make_int2( RandomUInt() & 1023, z * 2 );
		initialized = true;
	}
	for (int i = 0; i < 512; i++)
	{
		dust[i].x -= 3;
		if (dust[i].x < 0) dust[i].x += 1024;
		int3 voxelPos = SHMUP::Corrected( make_int3( dust[i].x, 50 + ((i * 182733) & 7), dust[i].y ) );
		SetParticle( particles, i, voxelPos, WHITE );
	}
	return true;
}

bool Fighter::Tick( int worldPos )
{
	if (state == INACTIVE)
	{
		if (worldPos == startPos)
		{
			state = ATTACKING, step = 0;
			relPos = make_int3( 1000, 65, 1024 ) << 8;
		}
	}
	else /* state == ATTACKING */
	{
		if (step > 340 && step < 480) angle++;
		if (step >= 480 && angle != 385) angle += 2;
		angle &= 511, step++;
		int dx = (int)(512 * sinf( (angle + 128) * PI / 256 ));
		int dz = (int)(512 * cosf( (angle + 128) * PI / 256 ));
		relPos += make_int3( dx, 0, dz );
		SetSpriteFrame( sprite, (51 - ((angle + 7) >> 4)) & 31 );
		MoveSpriteTo( sprite, SHMUP::Corrected( relPos >> 8 ) );
		if (step == 530) engine->play2D( charge );
		if (step == 560)
		{
			engine->play2D( shot );
			float3 toPlayer = make_float3( game->PlayerPos() - (relPos >> 8) );
			int3 aim = make_int3( 512.0f * normalize( make_float3( toPlayer.x, 0, toPlayer.z ) ) );
			game->NewBullet( relPos + (make_int3( 16, 0, 16 ) << 8), make_int2( aim.x + 256, aim.z ) );
		}
	}
	return true;
}

bool Turret::Tick( int worldPos )
{
	if (state == INACTIVE)
	{
		if (worldPos == startPos)
			state = ONSCREEN, relPos = make_int3( 1000, ypos, startz ) << 8, trigger = 1550;
	}
	else /* state == ONSCREEN */
	{
		MoveSpriteTo( sprite, SHMUP::Corrected( relPos >> 8 ) );
		if ((relPos.x -= 128) < 32) { state = INACTIVE; RemoveSprite( sprite ); }
		if (--trigger < 1280 && trigger > 256)
		{
			int3 pos = relPos + (make_int3( 16, 0, 16 ) << 8);
			int c[] = { -512, -362, 0, 362, 512, 362, 0, -362 };
			for (int i = 0; i < 8; i++) if ((trigger & 255) == (i * 4))
			{
				engine->play2D( shot );
				game->NewBullet( pos, make_int2( c[i], c[(i + 6) & 7] ) );
			}
		}
	}
	return true;
}

bool Boss::Tick( int worldPos )
{
	if (state == INACTIVE)
	{
		if (worldPos == startPos) 
		{
			engine->play2D( alarm );
			state = RISING, relPos = make_int3( 600, -150, 412 ) << 2;
		}
	}
	else if (state == RISING)
	{
		MoveSpriteTo( sprite, SHMUP::Corrected( relPos >> 2 ) );
		if ((++relPos.y >> 2) == 50) state = HOVERING, countdown = 160;
	}
	else if (state == HOVERING)
	{
		MoveSpriteTo( sprite, SHMUP::Corrected( relPos >> 2 ) );
		if (!--countdown)
		{
			state = EXPLODING;
			// copy frame to debris
			int3 frameSize = GetSpriteFrameSize( sprite );
			int particleIdx = 0, v;
			for (int j = 0; j < 4; j++) for (int z = 0; z < frameSize.z; z++) for (int y = 0; y < frameSize.y; y++)
				for (int x = j + ((y + z) & 3); x < frameSize.x; x += 4) if (v = GetSpriteVoxel( sprite, x, y, z ))
				{
					pos[particleIdx] = make_float3( make_int3( x, y, z ) + (relPos >> 2) ), color[particleIdx] = v;
					speed[particleIdx] = normalize( make_float3( RandomFloat() - 0.5f, 0, RandomFloat() - 0.5f ) ) * 1.0f;
					if (++particleIdx == 20000) goto break_all; // there's no break(4) in c++
				}
		break_all:
			RemoveSprite( sprite );
		}
	}
	else /* state == EXPLODING */ for (int i = 0; i < 20000; i++)
	{
		SetParticle( particles, i, SHMUP::Corrected( make_int3( pos[i] ) ), color[i] );
		pos[i] += speed[i], speed[i].x -= 0.11f;
	}
	return true;
}

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void SHMUP::Init()
{
	// change the skydome - defaults can be found in precomp.h, class Game
	skyDomeImage = "assets/sky_17.hdr";
	skyDomeScale = 0.7f;
	skyDomeLightScale = 2.0f;
	// kill the mouse pointer
	ShowCursor( false );
	// tell the actors about the main game object
	Actor::game = this;
	// load a spaceship
	shipSprite = LoadSprite( "assets/ship.vox", false );
	shipDebris = CreateParticles( 410 );
	EnableShadow( shipSprite );
	// mothership
	bossSprite = LoadSprite( "assets/mother.vox" );
	// generate option sprites
	ClearWorld();
	for (int i = 0; i < 64; i++)
		Sphere( make_float3( i * 12.0f + 6, 6, 6 ), 3 + 0.75f * sinf( i * PI / 32 ), LIGHTBLUE );
	option1Sprite = CreateSprite( 0, 0, 0, 12, 12, 12, 64 );
	option2Sprite = CloneSprite( option1Sprite );
	ClearWorld();
	// load a rock, 16 frames
	rockSprite[0] = LoadSprite( "assets/rock.vox" );
	EnableShadow( rockSprite[0] );
	// enemy jet
	enemySprite[0] = LoadSprite( "assets/yet.vox" );
	EnableShadow( enemySprite[0] );
	// turret
	turretSprite[0] = LoadSprite( "assets/turret.vox" );
	// create some copies
	for (int i = 1; i < 8; i++)
		rockSprite[i] = CloneSprite( rockSprite[0] ),
		enemySprite[i] = CloneSprite( enemySprite[0] ),
		turretSprite[i] = CloneSprite( turretSprite[0] );
	for (int i = 0; i < 8; i++) rockDebris[i] = CreateParticles( 1024 );
	// populate level with actors
	int rock_t[] = { 160, 80, 190, 240, 1000, 1050, 1070, 1100, 920, 970, 890, 950 };
	int rock_z[] = { 380, 450, 520, 590, 380, 450, 520, 590, 415, 485, 555, 625 };
	for (int i = 0; i < 12; i++) pool.Add( new Rock( rockSprite[i & 7], rockDebris[i & 7], rock_t[i], rock_z[i] ) );
	int fighter_z[] = { 300, 360, 410, 460 };
	for (int i = 0; i < 4; i++) pool.Add( new Fighter( enemySprite[i], fighter_z[i] ) );
	int turret_t[] = { 130, 1040, 1130, 1210, 1570, 1555, 1540 };
	int turret_z[] = { 580, 560, 650, 700, 400, 470, 540 };
	for (int i = 0; i < 7; i++) pool.Add( new Turret( turretSprite[i], turret_t[i], turret_z[i], 20 + ((i == 2) ? 10 : 0) ) );
	pool.Add( new Wind() );
	pool.Add( new Boss( bossSprite, 2150 ) );
	// load the monolith
	mnltSprite[0] = LoadSprite( "assets/pillar.vox" );
	for (int i = 1; i < 8; i++) mnltSprite[i] = CloneSprite( mnltSprite[0] );
	// init player bullets
	for (int i = 0; i < 20; i++) Plot( i, 0, 0, YELLOW );
	laserSprite = CreateSprite( 0, 0, 0, 20, 1, 1 );
	Sphere( make_float3( 512 + 4, 4, 4 ), 2.5f, ORANGE );
	bulletSprite = CreateSprite( 512, 0, 0, 8, 8, 8 );
	ClearWorld();
	for (int i = 0; i < MAXBEAMS; i++) pool.Add( laser[i] = new Laser( laserSprite ) );
	for (int i = 0; i < MAXBULLETS; i++) pool.Add( bullet[i] = new Bullet( bulletSprite ) );
	// prepare level
	landscape = new Surface( "assets/landscape.png" );
	// initialize audio engine
	engine = createIrrKlangDevice();
	engine->play2D( "assets/bgm.mp3", true );
	pew = engine->addSoundSourceFromFile( "assets/laser.wav", ESM_AUTO_DETECT, true );
	rumble = engine->addSoundSourceFromFile( "assets/rumble.wav", ESM_AUTO_DETECT, true );
	explosion = engine->addSoundSourceFromFile( "assets/expl_big.wav", ESM_AUTO_DETECT, true );
	boom = engine->addSoundSourceFromFile( "assets/expl_huge.wav", ESM_AUTO_DETECT, true );
	shot = engine->addSoundSourceFromFile( "assets/shot.wav", ESM_AUTO_DETECT, true );
	charge = engine->addSoundSourceFromFile( "assets/charge.wav", ESM_AUTO_DETECT, true );
	alarm = engine->addSoundSourceFromFile( "assets/alarm.wav", ESM_AUTO_DETECT, true );
}

// -----------------------------------------------------------
// Spawning an enemy bullet
// -----------------------------------------------------------
void SHMUP::NewBullet( int3 pos, int2 vel )
{
	bullet[bulletPtr]->relPos = make_int3( pos.x - (4 << 8), 70 << 8, pos.z - (4 << 8) );
	bullet[bulletPtr]->velocity = vel;
	bullet[bulletPtr++]->state = Bullet::ACTIVE;
	bulletPtr &= MAXBULLETS - 1;
}

// -----------------------------------------------------------
// Position camera
// -----------------------------------------------------------
void SHMUP::UpdateCamera()
{
	// default position and target
	float3 position = make_float3( (cameraPos.x >> 8) - 5.f, 410, 358 );
	float3 target = make_float3( (float)(cameraPos.x >> 8), (float)(cameraPos.y >> 8), 512 );
	// camera zoom
	if (landBlock < 5) position.y = 40, position.z = 250; else if (landBlock < 15)
	{
		static float3 currentPos = make_float3( 0, 40, 308 );
		static float step = 0.0125f;
		currentPos += (position - currentPos) * step, step *= 1.02f;
		position = make_float3( position.x, currentPos.y, currentPos.z );
	}
	LookAt( position, target );
}

// -----------------------------------------------------------
// LandColor: helper for land texture calculation
// -----------------------------------------------------------
uint SHMUP::LandColor( int odd, int h, int dhx, int dhy )
{
#if PAYLOADSIZE == 1
	uint green = min( 7, (h + odd * 2) >> 3 );
	uint grey = min( 3, max( dhx, dhy ) );
	return (green << 2) + (grey << 6) + grey;
#else
	uint green = min( 15, (h + odd * 2) >> 2 );
	uint grey = min( 7, 2 * max( dhx, dhy ) );
	return (green << 4) + (grey << 9) + grey;
#endif
}

// -----------------------------------------------------------
// Update level visuals
// -----------------------------------------------------------
void SHMUP::UpdateLevel()
{
	// 'block scroll' level
	cameraPos.x += 128, scroll += 128;
	if (scroll == (16 << 8))
	{
		cameraPos.x -= 16 << 8, scroll = 0;
		WorldXScroll( -16 /* must be multiples of BRICKDIM */ );
		landBlock++;
	}
	// draw level
	for (int line = scroll >> 8, z = 0; z < 1024; z++)
	{
		int wave = (int)(200 + 90 * (sinf( z * 0.05f ) + cosf( z * 0.021f )));
		if (landBlock * 16 + line > wave)
		{
			int x = (landBlock & 63) * 16 + line;
			int h = (landscape->buffer[x + z * 1024] & 255) >> 2;
			int h0 = max( h - landBlock, 0 ), h1 = max( h0 - 4, 0 );
			int dhx = abs( (int)((landscape->buffer[((x + 1) & 1023) + z * 1024] & 255) >> 2) - h );
			int dhy = abs( (int)((landscape->buffer[x + ((z + 1) & 1023) * 1024] & 255) >> 2) - h );
			int c = LandColor( (line + z) & 1, h, dhx, dhy );
			for (int i = h0; i <= h - 4; i++) Plot( 896 + line, i, z, 137 );
			for (int i = h1; i <= h; i++) Plot( 892 + line, h, z, c );
			for (int i = 1; i < 10; i++) Plot( 896 + line, h + i, z, 0 );
		}
	}
}

// -----------------------------------------------------------
// Update obstacles and enemies
// -----------------------------------------------------------
void SHMUP::UpdateActors()
{
	int worldPos = landBlock * 16;
	// entry monoliths, lowering when player approaches
	if (worldPos > 500 && worldPos < 1500)
	{
		static int ypos[4] = { 30, 30, 30, 30 };
		static bool rumbling[4] = { false, false, false, false };
		for (int i = 0; i < 4; i++)
		{
			if (worldPos > 800 + 32 * i && ypos[i] > -135 + (3 - i) * 5)
			{
				if (!rumbling[i]) { engine->play2D( rumble ); rumbling[i] = true; }
				ypos[i]--;
			}
			MoveSpriteTo( mnltSprite[i], make_int3( 1560 - 20 * i - worldPos, ypos[i] / 2, 680 - 50 * i ) );
			MoveSpriteTo( mnltSprite[i + 4], make_int3( 1500 + i * 20 - worldPos, ypos[3 - i] / 2, 480 - 50 * i ) );
		}
	}
	else if (worldPos >= 1500)
	{
		static uint removed = 0;
		if (!removed) for (int i = 0; i < 8; i++, removed = 0) RemoveSprite( mnltSprite[i] );
	}
	// killer monoliths, raising when player approaches
	if (worldPos > 3200 && worldPos < 4300)
	{
		static int ypos[4] = { -130, -130, -130, -130 };
		static bool rumbling[4] = { false, false, false, false };
		for (int i = 0; i < 4; i++)
		{
			if (worldPos > 3500 + 32 * i && ypos[i] < 50)
			{
				if (!rumbling[i]) { engine->play2D( rumble ); rumbling[i] = true; }
				ypos[i]++;
			}
			MoveSpriteTo( mnltSprite[i], make_int3( 4160 - 20 * i - worldPos, ypos[i] / 2, 680 - 50 * i ) );
			MoveSpriteTo( mnltSprite[i + 4], make_int3( 4100 + i * 20 - worldPos, ypos[3 - i] / 2, 480 - 50 * i ) );
		}
	}
	else if (worldPos >= 4500)
	{
		static uint killed = 0;
		if (!killed) for (int i = 0; i < 8; i++, killed = 1) RemoveSprite( mnltSprite[i] );
	}
	// all other actors
	pool.Tick( worldPos + (scroll >> 8) );
}

// -----------------------------------------------------------
// Player ship rendering
// -----------------------------------------------------------
void SHMUP::DrawShip( int3 pos )
{
	if (((revived >> 2) & 1) == 0)
	{
		// position ship sprite
		MoveSpriteTo( shipSprite, pos );
		// draw options
		mat4 M = mat4::RotateX( optionAngle );
		if ((optionAngle += 0.06f) > 2 * PI) optionAngle -= 2 * PI;
		option1Pos = TransformPosition( make_float3( 0, 16, 0 ), M ) + pos + make_float3( -6, -6, 4 );
		option2Pos = TransformPosition( make_float3( 0, -16, 0 ), M ) + pos + make_float3( -6, -6, 4 );
		int frame = (int)((optionAngle * 64) / (2 * PI));
		SetSpriteFrame( option1Sprite, frame );
		SetSpriteFrame( option2Sprite, frame );
		MoveSpriteTo( option1Sprite, make_int3( option1Pos ) );
		MoveSpriteTo( option2Sprite, make_int3( option2Pos ) );
	}
	else
	{
		RemoveSprite( shipSprite );
		RemoveSprite( option1Sprite );
		RemoveSprite( option2Sprite );
	}
	if (revived) revived--;
}

// -----------------------------------------------------------
// Player ship handling
// -----------------------------------------------------------
void SHMUP::UpdatePlayer()
{
	if (playerDied)
	{
		// update player particle effect
		for (int i = 0; i < 410; i++)
		{
			SetParticle( shipDebris, i, SHMUP::Corrected( make_int3( debrisPos[i] ) ), debrisColor[i] );
			debrisPos[i] += debrisSpeed[i], debrisSpeed[i].x -= 0.11f;
		}
		// reincarnate when the time has come
		if (!--reincarnate) playerDied = false, revived = 150;
	}
	else
	{
		// still alive
		int3 playerPos = (cameraPos >> 8) + (relPos >> 8);
		DrawShip( playerPos );
		// check for collisions
		bool playerHit = false;
		if (!revived)
		{
			// check actors
			for (int i = 0; i < pool.count; i++)
			{
				if (pool.actors[i]->isFriendly) continue;
				int actorSprite = pool.actors[i]->sprite;
				if (actorSprite == -1) /* actor has no sprite */ continue;
				if (SpriteHit( shipSprite, actorSprite )) playerHit = true;
			}
			// check pillars
			for (int i = 0; i < 8; i++) if (SpriteHit( shipSprite, mnltSprite[i] )) playerHit = true;
		}
		if (playerHit)
		{
			// turn the player into particles
			int particleIdx = 0 /* we expect exactly 410 for the 20x20x20 player sprite */, v;
			for (int z = 0; z < 20; z++) for (int y = 0; y < 20; y++) for (int x = 0; x < 20; x++) if (v = GetSpriteVoxel( shipSprite, x, y, z ))
			{
				debrisPos[particleIdx] = make_float3( make_int3( x - (scroll >> 8), y, z ) + playerPos );
				debrisColor[particleIdx] = v;
				debrisSpeed[particleIdx++] = normalize( make_float3( RandomFloat() - 0.5f, 0, RandomFloat() - 0.5f ) ) * 0.5f;
			}
			playerDied = true, reincarnate = 130, relPos = make_int3( 0 );
			RemoveSprite( shipSprite );
			RemoveSprite( option1Sprite );
			RemoveSprite( option2Sprite );
			engine->play2D( boom );
		}
		// handle keys, have some momentum
		static int vx = 0, vz = 0;
		if (GetAsyncKeyState( VK_UP )) vz = 640;
		if (GetAsyncKeyState( VK_DOWN )) vz = -640;
		if (GetAsyncKeyState( VK_LEFT )) vx = -640;
		if (GetAsyncKeyState( VK_RIGHT )) vx = 640;
		relPos.x = clamp( relPos.x + vx, -63168, 31680 );
		relPos.z = clamp( relPos.z + vz, -38864, 48768 );
		if (abs( vx -= vx / 8 ) < 32) vx = 0;
		if (abs( vz -= vz / 8 ) < 32) vz = 0;
		// spawn bullets
		if (gunDelay > 0) gunDelay--;
		if (GetAsyncKeyState( 32 )) if (!gunDelay)
		{
			int3 gunPos;
			if (gun == 0) gunPos = make_int3( playerPos.x + 16, 70, playerPos.z + 3 );
			if (gun == 1) gunPos = make_int3( playerPos.x + 16, 70, playerPos.z + 13 );
			if (gun == 2) gunPos = make_int3( option1Pos ) + make_int3( 7, 0, 0 );
			if (gun == 3) gunPos = make_int3( option2Pos ) + make_int3( 7, 0, 0 );
			laser[laserPtr++ & (MAXBEAMS - 1)]->relPos = gunPos;
			gunDelay = 6, gun = (gun + 1) & 3;
			engine->play2D( pew );
		}
	}
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void SHMUP::Tick( float deltaTime )
{
	// update landscape
	UpdateLevel();
	// position camera
	UpdateCamera();
	// update challenges
	UpdateActors();
	// update player ship
	UpdatePlayer();
}