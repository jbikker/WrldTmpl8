#include "precomp.h" // include (only) this in every .cpp file

Scene scene;
float3 bmin, bmax, offset;
float scale;
unsigned int* dat = 0;
unsigned int* dats = 0;
float xl[256], xr[256], zl[256], zr[256];
float ul[256], ur[256], vl[256], vr[256];
unsigned int palette[256];
unsigned int entries = 0;
int maxSize = 64;
bool doRotation = false;
int axis = 1; // y is default
int animFrames = 32;

// -----------------------------------------------------------
// Find a color in the palette
// -----------------------------------------------------------
unsigned int GetPaletteIdx( unsigned int c )
{
	int bestDist = 10000000, best = 10;
	for (unsigned int i = 0; i < entries; i++)
	{
		int dr = (c >> 16) - (palette[i] >> 16);
		int dg = ((c >> 8) & 255) - ((palette[i] >> 8) & 255);
		int db = (c & 255) - (palette[i] & 255);
		int dist = dr * dr + dg * dg + db * db;
		if (dist < bestDist) best = i, bestDist = dist;
	}
	if ((bestDist < 48) || (entries == 255)) return best;
	palette[entries++] = c;
	return entries - 1;
}

// -----------------------------------------------------------
// Get diffuse color for point on polygon
// -----------------------------------------------------------
unsigned int GetDiffuse( Primitive* p, float u, float v )
{
	const Material* m = p->GetMaterial();
	if (!m->GetTexture())
	{
		float3 c = m->GetDiffuse();
		int r = (int)(255.0f * c.x);
		int g = (int)(255.0f * c.y);
		int b = (int)(255.0f * c.z);
		return (b << 16) + (g << 8) + r;
	}
	else
	{
		int tw = m->GetTexture()->GetWidth();
		int th = m->GetTexture()->GetHeight();
		int itu = ((int)u + (tw * 10000)) % tw;
		int itv = ((int)v + (th * 10000)) % th;
		unsigned int c = m->GetTexture()->m_B32[itu + itv * tw];
		int r = (c >> 16) & 255;
		int g = (c >> 8) & 255;
		int b = (c & 255);
		return (b << 16) + (g << 8) + r;
	}
}

// -----------------------------------------------------------
// Voxelization
// -----------------------------------------------------------
void Voxelize()
{
	for (int i = 0; i < 256; i++) xl[i] = 255.0f, xr[i] = 0.0f;
	for (unsigned int i = 0; i < scene.m_Primitives; i++)
	{
		// rasterize primitive
		Primitive* p = &scene.m_Prim[i];
		// detect degenerate primitives (happens for legocar!)
		if (isnan( p->m_N.x )) continue;
		// determine bounds of primitive
		float3 pmin = p->GetVertex( 0 )->m_Pos;
		float3 pmax = pmin;
		for (int v = 1; v < 3; v++)
		{
			pmin.x = min( pmin.x, p->GetVertex( v )->m_Pos.x );
			pmax.x = max( pmax.x, p->GetVertex( v )->m_Pos.x );
			pmin.y = min( pmin.y, p->GetVertex( v )->m_Pos.y );
			pmax.y = max( pmax.y, p->GetVertex( v )->m_Pos.y );
			pmin.z = min( pmin.z, p->GetVertex( v )->m_Pos.z );
			pmax.z = max( pmax.z, p->GetVertex( v )->m_Pos.z );
		}
		int3 ipmin = make_int3( pmin ), ipmax = make_int3( pmax );
		// determine barycentre
		float3 bc = (p->m_Vertex[0]->m_Pos + p->m_Vertex[1]->m_Pos + p->m_Vertex[2]->m_Pos) * 0.33333f;
		// determine dominant axis of normal
		int zaxis = 0;
		if (fabs( p->m_N.y ) > fabs( p->m_N.x )) zaxis = 1;
		if (fabs( p->m_N.z ) > fabs( p->m_N.e[zaxis] )) zaxis = 2;
		int xaxis = (zaxis + 1) % 3, yaxis = (zaxis + 2) % 3;
		for (int shell = 0; shell < 3; shell++)
		{
			float3 localOffset = ((float)shell - 1) * p->m_N * 0.15f;
			// proceed as if 'u' is x, 'v' is y
			int maxy = 0, miny = 255;
			for (int j = 0; j < 3; j++)
			{
				Vertex* v0 = p->m_Vertex[j];
				Vertex* v1 = p->m_Vertex[(j + 1) % 3];
				if (v0->m_Pos.e[yaxis] > v1->m_Pos.e[yaxis]) v0 = v1, v1 = p->m_Vertex[j];
				float3 p0 = v0->m_Pos + normalize( v0->m_Pos - bc ) * 0.55f + localOffset;
				float3 p1 = v1->m_Pos + normalize( v1->m_Pos - bc ) * 0.55f + localOffset;
				float iyr = 1.0f / (p1.e[yaxis] - p0.e[yaxis]);
				float x = p0.e[xaxis], dx = (p1.e[xaxis] - x) * iyr;
				float z = p0.e[zaxis], dz = (p1.e[zaxis] - z) * iyr;
				float u = v0->m_U, du = (v1->m_U - u) * iyr;
				float v = v0->m_V, dv = (v1->m_V - v) * iyr;
				int iy0 = max( 0, min( 255, (int)p0.e[yaxis] + 1 ) );
				int iy1 = max( 0, min( 255, (int)p1.e[yaxis] ) );
				float fix = iy0 - p0.e[yaxis];
				x += fix * dx, u += fix * du;
				v += fix * dv, z += fix * dz;
				if (iy0 < miny) miny = iy0;
				if (iy1 > maxy) maxy = iy1;
				for (int i = iy0; i <= iy1; i++)
				{
					if (x < xl[i]) xl[i] = x, zl[i] = z, ul[i] = u, vl[i] = v;
					if (x > xr[i]) xr[i] = x, zr[i] = z, ur[i] = u, vr[i] = v;
					x += dx, z += dz, u += du, v += dv;
				}
			}
			for (int y = miny; y <= maxy; y++)
			{
				int x0 = (int)xl[y] + 1, x1 = (int)xr[y];
				float ixr = 1.0f / (xr[y] - xl[y]);
				float fix = x0 - xl[y];
				float z0 = zl[y], z1 = zr[y], dz = (z1 - z0) * ixr; z0 += fix * dz;
				float u0 = ul[y], u1 = ur[y], du = (u1 - u0) * ixr; u0 += fix * du;
				float v0 = vl[y], v1 = vr[y], dv = (v1 - v0) * ixr; v0 += fix * dv;
				for (int x = x0; x <= x1; x++, z0 += dz, u0 += du, v0 += dv)
				{
					int ipos[3];
					ipos[xaxis] = (int)x, ipos[yaxis] = y, ipos[zaxis] = (int)z0;
					ipos[0] = max( 0, min( maxSize - 1, ipos[0] ) );
					ipos[1] = max( 0, min( maxSize - 1, ipos[1] ) );
					ipos[2] = max( 0, min( maxSize - 1, ipos[2] ) );
					if ((ipos[0] >= ipmin.x) && (ipos[0] <= ipmax.x) &&
						(ipos[1] >= ipmin.y) && (ipos[1] <= ipmax.y) &&
						(ipos[2] >= ipmin.z) && (ipos[2] <= ipmax.z))
					{
						unsigned int c = GetDiffuse( p, u0, v0 );
						dat[ipos[0] + ipos[1] * maxSize + ipos[2] * maxSize * maxSize] = c;
					}
				}
				xl[y] = 255.0f, xr[y] = 0.0f;
			}
		}
	}
}

// -----------------------------------------------------------
// Store voxel data
// -----------------------------------------------------------
void SaveFrames( int frames )
{
	char fname[1024];
	strcpy( fname, scene.path );
	strcat( fname, scene.noext );
	strcat( fname, ".vox" );
	FILE* f = fopen( fname, "wb" );
	// header
	char chunkID[5] = "VOX ";
	int version = 150, zero = 0;
	fwrite( chunkID, 4, 1, f );
	fwrite( &version, 4, 1, f );
	// main chunk
	strcpy( chunkID, "MAIN" );
	fwrite( chunkID, 4, 1, f );
	fwrite( &zero, 4, 1, f );
	int totalSize = 0x7fffff; // it doesn't care in practice
	fwrite( &totalSize, 4, 1, f );
	// pack chunk
	strcpy( chunkID, "PACK" );
	fwrite( chunkID, 4, 1, f );
	int cs = 4, dummy = 0;
	fwrite( &cs, 1, 4, f );
	fwrite( &dummy, 1, 4, f );
	fwrite( &frames, 1, 4, f );
	// export frame(s)
	for (int j = 0; j < frames; j++)
	{
		// size chunk
		strcpy( chunkID, "SIZE" );
		fwrite( chunkID, 4, 1, f );
		int chunkSize = 12, childChunks = 0;
		fwrite( &chunkSize, 4, 1, f );
		fwrite( &childChunks, 4, 1, f );
		int sizex = maxSize, sizey = maxSize, sizez = maxSize;
		fwrite( &sizex, 4, 1, f );
		fwrite( &sizey, 4, 1, f );
		fwrite( &sizez, 4, 1, f );
		// voxel data
		strcpy( chunkID, "XYZI" );
		fwrite( chunkID, 4, 1, f );
		int voxelCount = 0;
		for (int i = 0; i < (maxSize * maxSize * maxSize); i++) if (dats[i + j * maxSize * maxSize * maxSize]) voxelCount++;
		chunkSize = voxelCount * 4 + 4;
		fwrite( &chunkSize, 4, 1, f );
		fwrite( &childChunks, 4, 1, f );
		fwrite( &voxelCount, 4, 1, f );
		for (int z = 0; z < maxSize; z++) for (int y = 0; y < maxSize; y++) for (int x = 0; x < maxSize; x++)
		{
			int idx = x + y * maxSize + z * maxSize * maxSize;
			if (dats[idx + j * maxSize * maxSize * maxSize])
			{
				unsigned int pal = GetPaletteIdx( dats[idx + j * maxSize * maxSize * maxSize] );
				unsigned int voxel = ((pal + 1) << 24) + ((maxSize - 1 - z) << 8) + (y << 16) + x;
				fwrite( &voxel, 4, 1, f );
			}
		}
	}
	// palette
	strcpy( chunkID, "RGBA" );
	fwrite( chunkID, 4, 1, f );
	int chunkSize = 1024, childChunks = 0;
	fwrite( &chunkSize, 4, 1, f );
	fwrite( &childChunks, 4, 1, f );
	fwrite( palette, 256, 4, f );
	// done
	fclose( f );
}

// -----------------------------------------------------------
// Handle drag & drop: add one file to the queue
// -----------------------------------------------------------
void Game::HandleDrop( const char* path )
{
	char* newFile = new char[strlen( path ) + 1];
	strcpy( newFile, path );
	toDo.push_back( newFile );
}

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void Game::Init()
{
	font = new Font( "assets/jetbrainsmono.png", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-+=/\\|{}[]<>,.?~!@#$%^&*():;" );
	panel = new Surface( "assets/panel.png" );
	yesno = new Surface( "assets/yesno.png" );
	for (int i = 0; i < 16; i++) lines[i] = new char[512], lines[i][0] = 0;
	AddLine( "awaiting your command." );
	// load preferences
	FILE* f = fopen( "settings.bin", "rb" );
	if (f)
	{
		fread( &doRotation, 1, sizeof( doRotation ), f );
		fread( &axis, 1, sizeof( axis ), f );
		fread( &maxSize, 1, sizeof( maxSize ), f );
		fread( &animFrames, 1, sizeof( animFrames ), f );
		fclose( f );
	}
}

// -----------------------------------------------------------
// Voxelize an obj file
// -----------------------------------------------------------
void Game::Process( const char* file )
{
	if (!scene.InitScene( file )) // leaks a little but who cares
	{
		AddLine( "not a valid obj file." );
		return;
	}
	if (!dat)
	{
		dat = new unsigned int[maxSize * maxSize * maxSize];
		dats = new unsigned int[maxSize * maxSize * maxSize * 64];
	}
	memset( dat, 0, maxSize * maxSize * maxSize * 4 );
	memset( palette, 0, 1024 );
	bmin = scene.GetExtends().bmin, bmax = scene.GetExtends().bmax;
	if (doRotation)
	{
		// compute bounds under rotation around object pivot (i.e., origin in object space)
		float3 p[8] = {
			make_float3( bmin.x, bmin.y, bmin.z ), make_float3( bmax.x, bmin.y, bmin.z ),
			make_float3( bmax.x, bmax.y, bmin.z ), make_float3( bmin.x, bmax.y, bmin.z ),
			make_float3( bmin.x, bmin.y, bmax.z ), make_float3( bmax.x, bmin.y, bmax.z ),
			make_float3( bmax.x, bmax.y, bmax.z ), make_float3( bmin.x, bmax.y, bmax.z )
		};
		bmin = make_float3( 1e34f ), bmax = make_float3( -1e34f );
		for (int i = 0; i < animFrames; i++)
		{
			float angle = (2 * PI * i) / (float)animFrames;
			mat4 M = mat4::RotateY( angle );
			for (int j = 0; j < 8; j++)
			{
				float3 q = make_float3( make_float4( p[j], 1 ) * M );
				bmin.x = min( bmin.x, q.x );
				bmin.y = min( bmin.y, q.y );
				bmin.z = min( bmin.z, q.z );
				bmax.x = max( bmax.x, q.x );
				bmax.y = max( bmax.y, q.y );
				bmax.z = max( bmax.z, q.z );
			}
		}
	}
	// scale mesh to bounds
	offset = -bmin;
	float3 size = bmax - bmin;
	scale = min( min( maxSize / size.x, maxSize / size.y ), maxSize / size.z );
	offset.x += ((maxSize - (scale * size.x)) * 0.5f) / scale;
	offset.y += ((maxSize - (scale * size.y)) * 0.5f) / scale;
	offset.z += ((maxSize - (scale * size.z)) * 0.5f) / scale;
	if (doRotation)
	{
		// transform & prescale
		for (unsigned int i = 0; i < scene.m_Primitives; i++)
		{
			for (int v = 0; v < 3; v++)
				scene.m_Prim[i].m_Vertex[v]->m_Orig = scene.m_Prim[i].m_Vertex[v]->m_Pos,
				scene.m_Prim[i].m_Vertex[v]->m_NO = scene.m_Prim[i].m_Vertex[v]->m_N;
			scene.m_Prim[i].m_NO = scene.m_Prim[i].m_N;
		}
		for (int i = 0; i < animFrames; i++)
		{
			float angle = (2 * PI * i) / (float)animFrames;
			mat4 M = mat4::RotateY( angle );
			for (unsigned int i = 0; i < scene.m_Primitives; i++)
			{
				for (int v = 0; v < 3; v++)
				{
					float3& no = scene.m_Prim[i].m_Vertex[v]->m_NO;
					float3& n = scene.m_Prim[i].m_Vertex[v]->m_N;
					float3& o = scene.m_Prim[i].m_Vertex[v]->m_Orig;
					float3& p = scene.m_Prim[i].m_Vertex[v]->m_Pos;
					p = (make_float3( make_float4( o, 1 ) * M ) + offset) * scale;
					n = make_float3( make_float4( no, 0 ) * M );
				}
				scene.m_Prim[i].m_N = make_float3( make_float4( scene.m_Prim[i].m_NO, 0 ) * M );
			}
			// voxelize one frame
			memset( dat, 0, maxSize * maxSize * maxSize * 4 );
			printf( "voxelizing frame %i...\n", i );
			Voxelize();
			// store the frame
			memcpy( dats + i * maxSize * maxSize * maxSize, dat, maxSize * maxSize * maxSize * 4 );
		}
		SaveFrames( animFrames );
	}
	else
	{
		// prescale
		for (unsigned int i = 0; i < scene.m_Primitives; i++) for (int v = 0; v < 3; v++)
		{
			scene.m_Prim[i].m_Vertex[v]->m_Pos += offset;
			scene.m_Prim[i].m_Vertex[v]->m_Pos *= scale;
		}
		scene.m_Extends.bmin = (scene.m_Extends.bmin + offset) * scale;
		scene.m_Extends.bmax = (scene.m_Extends.bmax + offset) * scale;
		// voxelize
		Voxelize();
		memcpy( dats, dat, maxSize * maxSize * maxSize * 4 );
		SaveFrames( 1 );
	}
}

// -----------------------------------------------------------
// Famous last words
// -----------------------------------------------------------
void Game::Shutdown()
{
	// save preferences
	FILE* f = fopen( "settings.bin", "wb" );
	fwrite( &doRotation, 1, sizeof( doRotation ), f );
	fwrite( &axis, 1, sizeof( axis ), f );
	fwrite( &maxSize, 1, sizeof( maxSize ), f );
	fwrite( &animFrames, 1, sizeof( animFrames ), f );
	fclose( f );
}

// -----------------------------------------------------------
// Add a line to the box, with scrolling
// -----------------------------------------------------------
void Game::AddLine( const char* text )
{
	if (lineNr == 11)
		for (int i = 1; i < 11; i++) strcpy( lines[i - 1], lines[i] );
	else
		lineNr++;
	strcpy( lines[lineNr - 1], text );
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Game::Tick( float deltaTime )
{
	// clear the graphics window
	panel->CopyTo( screen, 0, 0 );
	char t[10];
	sprintf( t, "%i", maxSize );
	font->Print( screen, t, 456 - 14 * (int)strlen( t ), 661 );
	sprintf( t, "%i", animFrames );
	font->Print( screen, t, 379 - 14 * (int)strlen( t ), 585 );
	if (doRotation) 
	{
		yesno->CopyTo( screen, 189, 589 );
		if (axis == 0) yesno->CopyTo( screen, 189, 625 );
		if (axis == 1) yesno->CopyTo( screen, 329, 625 );
		if (axis == 2) yesno->CopyTo( screen, 470, 625 );
	}
	// process queue
	if (toProcess != 0)
	{
		Process( toProcess );
		toProcess = 0;
		if (toDo.size() == 0) AddLine( "done." );
	}
	if (toDo.size() > 0)
	{
		const char* item = toDo.back();
		toProcess = item;
		toDo.pop_back();
		char t[512];
		char* f = (char*)toProcess;
		if (strlen( toProcess ) > 62) f += strlen( toProcess ) - 62, f[0] = f[1] = f[2] = '.';
		sprintf( t, "processing %s...", f );
		AddLine( t );
	}
	for (int i = 0; i < 12; i++) font->Print( screen, lines[i], 12, 162 + 30 * i );
	// screen->Line( mx, 0, mx, SCRHEIGHT - 1, 0xff0000 );
	// screen->Line( 0, my, SCRWIDTH - 1, my, 0x00ff00 );
	if (mclick)
	{
		if (my >= 581 && my < 617)
		{
			if (mx >= 181 && mx <= 217)
			{
				// rotation button
				doRotation = !doRotation;
			}
			if (mx >= 387 && mx < 421) animFrames = max( 1, animFrames - 1 );
			if (mx >= 421 && mx < 454) animFrames = min( 32, animFrames + 1 );
		}
		if (my >= 617 && my < 653)
		{
			if (mx >= 181 && mx <= 217)
			{
				// x-axis button
				axis = 0;
			}
			if (mx > 323 && mx < 357)
			{
				// y-axis button
				axis = 1;
			}
			if (mx > 463 && mx < 497)
			{
				// z-axis button
				axis = 2;
			}
		}
		else if (my > 652 && my < 688)
		{
			if (mx >= 487 && mx < 521)
			{
				// decrease size
				maxSize = max( maxSize - 1, 1 );
			}
			else if (mx >= 521 && mx < 554)
			{
				// increase size
				maxSize = min( maxSize + 1, 255 ); // what's the max for magicavoxel?
			}
		}
		mclick = false;
	}
}
