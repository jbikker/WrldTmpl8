#pragma once

namespace Tmpl8 {

class Game
{
public:
	void SetTarget( Surface* surface ) { screen = surface; }
	void Init();
	void AddLine( const char* text );
	void Shutdown();
	void Tick( float deltaTime );
	void Process( const char* file );
	void MouseUp( int button ) { mdown = false; }
	void MouseDown( int button ) { if (!mdown) mclick = true; mdown = true; }
	void MouseMove( int x, int y ) { mx = x, my = y; }
	void KeyUp( int key ) { /* implement if you want to handle keys */ }
	void KeyDown( int key ) { /* implement if you want to handle keys */ }
	void HandleDrop( const char* path );
	Surface* screen = 0, *panel = 0, *yesno = 0;
	const char* toProcess = 0;
	Font* font;
	vector<const char*> toDo;
	char** lines = new char* [16];
	int lineNr = 0;
	bool mdown = false, mclick = false;
	int mx = 0, my = 0;
};

}; // namespace Tmpl8