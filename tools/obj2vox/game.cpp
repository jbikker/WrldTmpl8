#include "precomp.h"

// #define CONSERVATIVE

#include "scene.h"

Scene scene;

float3 bmin, bmax, offset;
float scale;

#define VOXELSX		127
#define VOXELSY		127
#define VOXELSZ		127

unsigned int* dat = 0;
float xl[256], xr[256], zl[256], zr[256];
float ul[256], ur[256], vl[256], vr[256];

unsigned int palette[256];
unsigned int entries = 0;

extern char* fileName;
extern int maxSize;

// -----------------------------------------------------------
// Find a color in the palette
// -----------------------------------------------------------
unsigned int GetPaletteIdx( unsigned int c )
{
	int bestDist = 10000000, best = 10;
	for( unsigned int i = 0; i < entries; i++ )
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
	for( int i = 0; i < 256; i++ ) xl[i] = 255.0f, xr[i] = 0.0f;
	for( unsigned int i = 0; i < scene.m_Primitives; i++ )
	{
		// rasterize primitive
		Primitive* p = &scene.m_Prim[i];
		// detect degenerate primitives (happens for legocar!)
		if (isnan( p->m_N.x )) continue;
		// determine bounds of primitive
		float3 pmin = p->GetVertex( 0 )->m_Pos;
		float3 pmax = pmin;
		for( int v = 1; v < 3; v++ )
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
		if (fabs( p->m_N.y ) > fabs( p->m_N.x)) zaxis = 1;
		if (fabs( p->m_N.z ) > fabs( p->m_N.e[zaxis])) zaxis = 2;
		int xaxis = (zaxis + 1) % 3, yaxis = (zaxis + 2) % 3;
		for( int shell = 0; shell < 3; shell++ )
		{
			float3 localOffset = ((float)shell - 1) * p->m_N * 0.15f;
			// proceed as if 'u' is x, 'v' is y
			int maxy = 0, miny = 255;
			for( int j = 0; j < 3; j++ )
			{
				Vertex* v0 = p->m_Vertex[j];
				Vertex* v1 = p->m_Vertex[(j + 1) % 3];
				if (v0->m_Pos.e[yaxis] > v1->m_Pos.e[yaxis]) v0 = v1, v1 = p->m_Vertex[j];
				float3 p0 = v0->m_Pos + normalize( v0->m_Pos - bc ) * 0.55f + localOffset;
				float3 p1 = v1->m_Pos + normalize( v1->m_Pos - bc ) * 0.55f + localOffset;
				float iyr = 1.0f / (p1.e[yaxis]- p0.e[yaxis]);
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
				for( int i = iy0; i <= iy1; i++ )
				{
					if (x < xl[i]) xl[i] = x, zl[i] = z, ul[i] = u, vl[i] = v;
					if (x > xr[i]) xr[i] = x, zr[i] = z, ur[i] = u, vr[i] = v;
					x += dx, z += dz, u += du, v += dv;
				}
			}
			for( int y = miny; y <= maxy; y++ )
			{
				int x0 = (int)xl[y] + 1, x1 = (int)xr[y];
				float ixr = 1.0f / (xr[y] - xl[y]);
				float fix = x0 - xl[y];
				float z0 = zl[y], z1 = zr[y], dz = (z1 - z0) * ixr; z0 += fix * dz;
				float u0 = ul[y], u1 = ur[y], du = (u1 - u0) * ixr; u0 += fix * du;
				float v0 = vl[y], v1 = vr[y], dv = (v1 - v0) * ixr; v0 += fix * dv;
				for( int x = x0; x <= x1; x++, z0 += dz, u0 += du, v0 += dv )
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
void Save()
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
	int voxelCount = 0;
	for( int i = 0; i < (maxSize * maxSize * maxSize); i++ ) if (dat[i]) voxelCount++;
	strcpy( chunkID, "MAIN" );
	fwrite( chunkID, 4, 1, f );
	fwrite( &zero, 4, 1, f );
	int totalSize = voxelCount * 4 + 1076;
	fwrite( &totalSize, 4, 1, f );
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
	chunkSize = voxelCount * 4 + 4;
	fwrite( &chunkSize, 4, 1, f );
	fwrite( &childChunks, 4, 1, f );
	fwrite( &voxelCount, 4, 1, f );
	for( int z = 0; z < maxSize; z++ ) for( int y = 0; y < maxSize; y++ ) for( int x = 0; x < maxSize; x++ )
	{
		int idx = x + y * maxSize + z * maxSize * maxSize;
		if (dat[idx])
		{
			unsigned int pal = GetPaletteIdx( dat[idx] );
			unsigned int voxel = ((pal + 1) << 24) + ((maxSize - 1 - z) << 8) + (y << 16) + x;
			fwrite( &voxel, 4, 1, f );
		}
	}
	// palette
	strcpy( chunkID, "RGBA" );
	fwrite( chunkID, 4, 1, f );
	chunkSize = 1024;
	fwrite( &chunkSize, 4, 1, f );
	fwrite( &childChunks, 4, 1, f );
	fwrite( palette, 256, 4, f );
	// done
	fclose( f );
}

// -----------------------------------------------------------
// Initialize the game
// -----------------------------------------------------------
void Game::Init()
{
	if (fileName) scene.InitScene( fileName ); else 
	{
		// scene.InitScene( "data/toad/toad.obj" );
		scene.InitScene( "data/lego/legocar.obj" );
	}
	dat = new unsigned int[maxSize * maxSize * maxSize];
	memset( dat, 0, maxSize * maxSize * maxSize * 4 );
	memset( palette, 0, 1024 );
}

// -----------------------------------------------------------
// Main game tick function
// -----------------------------------------------------------
void Game::Tick( float _DT )
{
	// prepare voxelization
	bmin = scene.GetExtends().bmin,	bmax = scene.GetExtends().bmax;
	offset = -bmin;
	float3 size = bmax - bmin;
	scale = min( min( maxSize / size.x, maxSize / size.y ), maxSize / size.z );
	offset.x += ((maxSize - (scale * size.x)) * 0.5f) / scale;
	offset.y += ((maxSize - (scale * size.y)) * 0.5f) / scale;
	offset.z += ((maxSize - (scale * size.z)) * 0.5f) / scale;
	// prescale
	for( unsigned int i = 0; i < scene.m_Primitives; i++ ) for( int v = 0; v < 3; v++ )
	{
		scene.m_Prim[i].m_Vertex[v]->m_Pos += offset;
		scene.m_Prim[i].m_Vertex[v]->m_Pos *= scale;
	}
	scene.m_Extends.bmin = (scene.m_Extends.bmin + offset) * scale;
	scene.m_Extends.bmax = (scene.m_Extends.bmax + offset) * scale;
	// voxelize
	Voxelize();
	Save();
	exit( 0 );
}

// EOF