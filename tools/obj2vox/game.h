#pragma once

namespace Tmpl8 {

class Surface;
class Game
{
public:
	void SetTarget( Surface* _Surface ) { screen = _Surface; }
	void Init();
	void Shutdown() { /* implement if you want code to be executed upon app exit */ };
	void Tick( float _DT );
	void MouseUp( int _Button ) { /* implement if you want to detect mouse button presses */ }
	void MouseDown( int _Button ) { /* implement if you want to detect mouse button presses */ }
	void MouseMove( int _X, int _Y ) { /* implement if you want to detect mouse movement */ }
	void KeyUp( int a_Key ) { /* implement if you want to handle keys */ }
	void KeyDown( int a_Key ) { /* implement if you want to handle keys */ }
	Surface* screen;
};

}; // namespace Tmpl8