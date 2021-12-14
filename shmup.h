#pragma once

#define MAXACTORS	1024
#define MAXBEAMS	128
#define MAXBULLETS	128

namespace Tmpl8
{

class SHMUP;

// abstract base class for an 'Actor'
class Actor
{
public:
	virtual bool Tick( int worldPos ) = 0;
	int3 relPos = make_int3( OUTOFRANGE, 0, 0 );
	inline static SHMUP* game = 0; // for global access to game data
	int sprite = -1;
	bool isFriendly = false;
};

// actor pool, for conveniently updating all actors
class ActorPool
{
public:
	void Tick( int worldPos )
	{
		for( int i = 0; i < count; i++ ) if (!actors[i]->Tick( worldPos ))
		{
			// actor got killed; remove by swapping with last one
			swap( actors[i], actors[count - 1] );
			delete actors[count--];
		}
	}
	void Add( Actor* a ) { actors[count++] = a; }
	Actor* actors[MAXACTORS];
	int count = 0;
};

// laser: projectile fired by player 
class Laser : public Actor
{
public:
	Laser( uint spriteIdx ) { sprite = CloneSprite( spriteIdx ); isFriendly = true; }
	virtual bool Tick( int worldPos ) final;
};

// rock: dump space debris trying to hit the player
class Rock : public Actor
{
public:
	enum { INACTIVE = 0, FLYING, KILLED };
	Rock( int spriteID, int particlesID, int start, int zpos ) : 
		particles( particlesID ), startPos( start & 0xffff8 ) { relPos.z = zpos << 8, sprite = spriteID; }
	virtual bool Tick( int worldPos ) final;
	int frame = 0;
	int startPos = 0xffffff;
	int particles = -1;
	int state = INACTIVE; // one does not kill the rock, it becomes inactive
	// particle data for debris: position, velocity, color
	float3 pos[1024], speed[1024]; 
	uint color[1024];
};

// fighter: fast enemy plane
class Fighter : public Actor
{
public:
	enum { INACTIVE = 0, ATTACKING };
	Fighter( int spriteID, int start ) :
		startPos( start & 0xffff8 ) { sprite = spriteID; }
	virtual bool Tick( int worldPos ) final;
	int state = INACTIVE;
	int step = 0;
	int angle = 180;
	int startPos = 0xffffff;
};

// turret: generic bullet hell turret
class Turret : public Actor
{
public:
	enum { INACTIVE = 0, ONSCREEN };
	Turret( int spriteID, int start, int zpos, int height ) : 
		startPos( start & 0xffff8 ), startz( zpos ), ypos( height ) { sprite = spriteID; }
	virtual bool Tick( int worldPos ) final;
	int state = INACTIVE, trigger = 0;
	int startPos = 0xffffff, startz = 0, ypos = 0;
};

// bullet: generic enemry bullet
class Bullet : public Actor
{
public:
	enum { INACTIVE = 0, ACTIVE };
	Bullet( int spriteIdx ) { sprite = CloneSprite( spriteIdx ); }
	virtual bool Tick( int worldPos ) final;
	int state = INACTIVE;
	int2 velocity = make_int2( 0 );
};

// wind: dust particles flowing in the wind
class Wind : public Actor
{
public:
	Wind() : particles( CreateParticles( 512 ) ) {}
	virtual bool Tick( int worldPos ) final;
	int2 dust[512];
	uint particles;
	bool initialized = false;
};

// boss: placeholder boss with massive sprite
class Boss : public Actor
{
public:
	enum { INACTIVE = 0, RISING, HOVERING, EXPLODING };
	Boss( int spriteIdx, int start ) : 
		startPos( start & 0xffff8 ), particles( CreateParticles( 32768 ) ) { sprite = spriteIdx; }
	virtual bool Tick( int worldPos ) final;
	int state = INACTIVE;
	int startPos = 0;
	int particles = -1;
	int countdown = 0;
	// particle data for debris: position, velocity, color
	float3 pos[20000], speed[20000]; 
	uint color[20000];
};

// SHMUP game class
class SHMUP : public Game
{
public:
	// game flow methods
	void Init();
	void UpdateCamera();
	uint LandColor( int odd, int h, int dhx, int dhy );
	void UpdateLevel();
	void UpdateActors();
	void UpdatePlayer();
	void NewBullet( int3 pos, int2 vel );
	void DrawShip( int3 pos );
	void Tick( float deltaTime );
	void Shutdown() { /* implement if you want to do something on exit */ }
	// input handling
	void MouseUp( int button ) { /* implement if you want to detect mouse button presses */ }
	void MouseDown( int button ) { /* implement if you want to detect mouse button presses */ }
	void MouseMove( int x, int y ) { mousePos.x = x, mousePos.y = y; }
	void KeyUp( int key ) { /* implement if you want to handle keys */ }
	void KeyDown( int key ) { /* implement if you want to handle keys */ }
	// block scroll correction helper
	static inline int scroll = 0;
	static int3 Corrected( const int3 v) { return make_int3( v.x + (scroll >> 8), v.y, v.z ); }
	int3 PlayerPos() { return (cameraPos >> 8) + (relPos >> 8); }
	// data members
	int2 mousePos;
	Laser* laser[MAXBEAMS];		// actors may query these for hits
	Bullet* bullet[MAXBULLETS];	// player will query these for hits
	int bulletPtr = 0;
private:
	int shipSprite, option1Sprite, option2Sprite, laserSprite, bulletSprite, bossSprite;
	int rockSprite[8], rockDebris[8], mnltSprite[8], enemySprite[8];
	int turretSprite[8];
	int3 cameraPos = make_int3( 462 << 8, 70 << 8, 512 << 8 );
	int3 relPos = make_int3( 0 );
	int gunDelay = 0, gun = 0, laserPtr = 0;
	float3 option1Pos = make_float3( OUTOFRANGE ), option2Pos;
	float optionAngle = 0, optionRadius;
	Surface* landscape = 0;
	int landBlock = 0;
	ActorPool pool;
	// debris for killed player - TODO: turn into proper actor
	int shipDebris;
	float3 debrisPos[410], debrisSpeed[410];
	uint debrisColor[410];
	bool playerDied = false;
	int reincarnate = 0; // countdown to reincarnation
	int revived = 0; // invincibility timer
};

} // namespace Tmpl8