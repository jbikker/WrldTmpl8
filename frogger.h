#pragma once

namespace Tmpl8
{

class Frogger : public Game
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
	float3 ballPos = make_float3( 300, 100, 300 );
	float3 ballVel = make_float3( 0.3f, 0, 0.5f );
private:
	int shipSprite;
	int3 playerPos = make_int3( 512, 128, 512 );
	int3 bulletPos[128], oldBulletPos[128];
	int gunDelay = 0, gun = 0, bulletPtr = 0;
	float3 option1Pos = make_float3( -9999 ), option2Pos;
	float optionAngle = 0, optionRadius;
	int scroll = 0;
};

} // namespace Tmpl8