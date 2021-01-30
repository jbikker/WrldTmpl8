#pragma once

namespace Tmpl8
{

class Bouncer : public Game
{
public:
	// game flow methods
	void Init();
	void Tick( float deltaTime );
	void Shutdown() { /* implement if you want to do something on exit */ }
	// input handling
	mat4 MouseLook();
	void MouseUp( int button ) { /* implement if you want to detect mouse button presses */ }
	void MouseDown( int button ) { /* implement if you want to detect mouse button presses */ }
	void MouseMove( int x, int y ) { mousePos.x = x, mousePos.y = y; }
	void KeyUp( int key ) { /* implement if you want to handle keys */ }
	void KeyDown( int key ) { /* implement if you want to handle keys */ }
	// data members
	int2 mousePos;
	float3 ballPos = make_float3( 300, 100, 300 );
	float3 ballVel = make_float3( 0.3f, 0, 0.5f );
	int tree = -1, tx, tz, tr;
	int sx[4000], sy[4000], sz[4000];
};

} // namespace Tmpl8