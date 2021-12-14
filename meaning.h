#pragma once

#define CREATURECOUNT	512

namespace Tmpl8
{

class ALIGN( 16 ) Creature
{
public:
	Creature() = default;
	Creature( const int i, const float3& p, const float3& t );
	void Tick( const float deltaTime );
	void Respawn();
	float3 position, target;
	float cycle, base;
	int sprite, id;
	static inline float jump = 2.5f; // object size: 48 bytes
};

class MeaningOfLife : public Game
{
public:
	// game flow methods
	void Init();
	void Tick( float deltaTime );
	void Shutdown() { /* implement if you want to do something on exit */ }
	// input handling 
	void MouseUp( int button ) { /* implement if you want to detect mouse button presses */ }
	void MouseDown( int button ) { /* implement if you want to detect mouse button presses */ }
	void MouseMove( int x, int y ) { mousePos.x = x, mousePos.y = y; }
	void KeyUp( int key ) { /* implement if you want to handle keys */ }
	void KeyDown( int key ) { /* implement if you want to handle keys */ }
	// data members
	int2 mousePos;
};

} // namespace Tmpl8