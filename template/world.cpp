#include "precomp.h"
#include "bluenoise.h"

using namespace Tmpl8;

static const uint gridSize = GRIDSIZE;
static const uint commitSize = BRICKCOMMITSIZE / 4 + gridSize;

// World Constructor
// ----------------------------------------------------------------------------
World::World( const uint targetID )
{
	// create the commit buffer, used to sync CPU-side changes to the GPU
	commit = (uint*)_aligned_malloc( commitSize * 4, 64 );
	commitBuffer = new Buffer( commitSize, Buffer::READONLY, commit );
	modified = new uint[BRICKCOUNT / 32]; // 1 bit per brick, to track 'dirty' bricks
	// have a pinned buffer for faster transfer
	pinned = clCreateBuffer( Kernel::GetContext(), CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, commitSize * 8, 0, 0 );
	devmem = clCreateBuffer( Kernel::GetContext(), CL_MEM_READ_ONLY, commitSize * 4, 0, 0 );
	// store top-level grid in a 3D texture
	cl_image_format fmt;
	fmt.image_channel_order = CL_R;
	fmt.image_channel_data_type = CL_UNSIGNED_INT32;
	cl_image_desc desc;
	memset( &desc, 0, sizeof( cl_image_desc ) );
	desc.image_type = CL_MEM_OBJECT_IMAGE3D;
	desc.image_width = MAPWIDTH / BRICKDIM;
	desc.image_height = MAPHEIGHT / BRICKDIM;
	desc.image_depth = MAPDEPTH / BRICKDIM;
	gridMap = clCreateImage( Kernel::GetContext(), CL_MEM_HOST_NO_ACCESS, &fmt, &desc, 0, 0 );
	// create brick storage
	brick = (uchar*)_aligned_malloc( BRICKCOUNT * BRICKSIZE, 64 );
	brickBuffer = new Buffer( (BRICKCOUNT * BRICKSIZE) / 4, Buffer::DEFAULT, brick );
	brickInfo = new BrickInfo[BRICKCOUNT];
	// create a cyclic array for unused bricks (all of them, for now)
	trash = new uint[BRICKCOUNT];
	memset( trash, 0, BRICKCOUNT * 4 );
	for (uint i = 0; i < BRICKCOUNT; i++) trash[(i * 31 /* prevent false sharing*/) & (BRICKCOUNT - 1)] = i;
	// prepare a test world
	grid = commit; // for efficiency, the top-level grid resides in the commit buffer
	memset( grid, 0, GRIDWIDTH * GRIDHEIGHT * GRIDDEPTH * sizeof( uint ) );
	DummyWorld();
	LoadSky( "assets/sky_15.hdr", "assets/sky_15.bin" );
	brickBuffer->CopyToDevice();
	ClearMarks(); // clear 'modified' bit array
	// report memory usage
	printf( "Allocated %iMB on CPU and GPU for the top-level grid.\n", (int)((gridSize * sizeof( uint )) >> 20) );
	printf( "Allocated %iMB on CPU and GPU for %ik bricks.\n", (int)((BRICKCOUNT * BRICKSIZE) >> 20), (int)(BRICKCOUNT >> 10) );
	printf( "Allocated %iMB on CPU and GPU for commits.\n", (int)(commitBuffer->size >> 18) );
	printf( "Allocated %iKB on CPU for bitfield.\n", (int)(BRICKCOUNT >> 15) );
	printf( "Allocated %iMB on CPU for brickInfo.\n", (int)((BRICKCOUNT * sizeof( BrickInfo )) >> 20) );
	// initialize kernels
	renderer = new Kernel( "cl/kernels.cl", "render" );
	committer = new Kernel( renderer->GetProgram(), "commit" );
	screen = new Buffer( targetID, Buffer::TARGET );
	paramBuffer = new Buffer( sizeof( RenderParams ) / 4, Buffer::DEFAULT | Buffer::READONLY, &params );
	renderer->SetArgument( 0, screen );
	renderer->SetArgument( 1, paramBuffer );
	renderer->SetArgument( 2, &gridMap );
	renderer->SetArgument( 3, brickBuffer );
	renderer->SetArgument( 4, sky );
	committer->SetArgument( 1, &devmem );
	committer->SetArgument( 2, brickBuffer );
	// prepare the bluenoise data
	const uchar* data8 = (const uchar*)sob256_64; // tables are 8 bit per entry
	uint* data32 = new uint[65536 * 5]; // we want a full uint per entry
	for (int i = 0; i < 65536; i++) data32[i] = data8[i]; // convert
	data8 = (uchar*)scr256_64;
	for (int i = 0; i < (128 * 128 * 8); i++) data32[i + 65536] = data8[i];
	data8 = (uchar*)rnk256_64;
	for (int i = 0; i < (128 * 128 * 8); i++) data32[i + 3 * 65536] = data8[i];
	blueNoise = new Buffer( 65536 * 5, Buffer::READONLY, data32 );
	blueNoise->CopyToDevice();
	delete data32;
	renderer->SetArgument( 5, blueNoise );
	// load a bitmap font for the print command
	font = new Surface( "assets/font.png" );
}

// World Destructor
// ----------------------------------------------------------------------------
World::~World()
{
	_aligned_free( commit );
	_aligned_free( brick );
}

// World::DummyWorld: box
// ----------------------------------------------------------------------------
void World::DummyWorld()
{
	Clear();
	for (int y = 0; y < 256; y++) for (int z = 0; z < 1024; z++)
	{
		Set( 0, y, z, 255 );
		Set( 1023, y, z, 255 );
	}
	for (int x = 0; x < 1024; x++) for (int z = 0; z < 1024; z++)
	{
		Set( x, 0, z, 255 );
		Set( x, 255, z, 255 );
	}
	for (int x = 0; x < 1024; x++) for (int y = 0; y < 256; y++)
	{
		Set( x, y, 0, 255 );
		Set( x, y, 1023, 255 );
	}
}

// World::Clear
// ----------------------------------------------------------------------------
void World::Clear()
{
	for (int z = 0; z < GRIDDEPTH; z++) for (int y = 0; y < GRIDHEIGHT; y++) for (int x = 0; x < GRIDWIDTH; x++)
	{
		const uint cellIdx = x + z * GRIDWIDTH + y * GRIDWIDTH * GRIDDEPTH;
		const uint g = grid[cellIdx];
		if (g & 1)
		{
		#if THREADSAFEWORLD
			const uint trashItem = InterlockedAdd( &trashHead, 31 ) - 31;
			trash[trashItem & (BRICKCOUNT - 1)] = g >> 1;
		#else
			trash[trashHead++ & (BRICKCOUNT - 1)] = g >> 1;
		#endif
		}
	}
	memset( grid, 0, GRIDWIDTH * GRIDHEIGHT * GRIDDEPTH * sizeof( uint ) );
}

// World::LoadSky
// ----------------------------------------------------------------------------
void World::LoadSky( const char* filename, const char* bin_name )
{
	// attempt to load skydome from binary file
	ifstream f( bin_name, ios::binary );
	float* pixels = 0;
	if (f)
	{
		printf( "loading cached hdr data... " );
		f.read( (char*)&skySize.x, sizeof( skySize.x ) );
		f.read( (char*)&skySize.y, sizeof( skySize.y ) );
		// TODO: Mmap
		pixels = (float*)MALLOC64( skySize.x * skySize.y * sizeof( float ) * 3 );
		f.read( (char*)pixels, sizeof( float ) * 3 * skySize.x * skySize.y );
	}
	if (!pixels)
	{
		// load skydome from original .hdr file
		printf( "loading original hdr data... " );
		FREE_IMAGE_FORMAT fif = FIF_UNKNOWN;
		fif = FreeImage_GetFileType( filename, 0 );
		if (fif == FIF_UNKNOWN) fif = FreeImage_GetFIFFromFilename( filename );
		FIBITMAP* dib = FreeImage_Load( fif, filename );
		if (!dib) return;
		skySize.x = FreeImage_GetWidth( dib );
		skySize.y = FreeImage_GetHeight( dib );
		uint pitch = FreeImage_GetPitch( dib );
		uint bpp = FreeImage_GetBPP( dib );
		printf( "Skydome %dx%d, pitch %d @%dbpp\n", skySize.x, skySize.y, pitch, bpp );
		pixels = (float*)MALLOC64( skySize.x * skySize.y * sizeof( float ) * 3 );
		for (int y = 0; y < skySize.y; y++)
		{
			uint* src = (uint*)FreeImage_GetScanLine( dib, skySize.y - 1 - y );
			float* dst = (float*)pixels + y * skySize.x * 3;
			if (bpp == 96) memcpy( dst, src, skySize.x * sizeof( float ) * 3 ); else
				if (bpp == 128) for (int x = 0; x < skySize.x; x++)
					memcpy( (float*)dst + 3 * x, (float*)src + 4 * x * sizeof( float ), sizeof( float ) * 4 );
				else
					FATALERROR( "Reading a skydome with %dbpp is not implemented!", bpp );
		}
		FreeImage_Unload( dib );
		// save skydome to binary file, .hdr is slow to load
		ofstream f( bin_name, ios::binary );
		f.write( (char*)&skySize.x, sizeof( skySize.x ) );
		f.write( (char*)&skySize.y, sizeof( skySize.y ) );
		f.write( (char*)pixels, sizeof( float ) * 3 * skySize.x * skySize.y );
	}
	// convert to float4
	float4* pixel4 = new float4[skySize.x * skySize.y];
	for (int y = 0; y < skySize.y; y++) for (int x = 0; x < skySize.x; x++)
		pixel4[x + y * skySize.x] = make_float4(
			pixels[x * 3 + y * 3 * skySize.x],
			pixels[x * 3 + 1 + y * 3 * skySize.x],
			pixels[x * 3 + 2 + y * 3 * skySize.x],
			1
		);
	FREE64( pixels );
	// load a sky dome
	sky = new Buffer( skySize.x * skySize.y * 4, Buffer::READONLY, pixel4 );
	sky->CopyToDevice();
}

/*
Some primitives for drawing in the 3D voxel world.
Note that these rely on World::Set to actually set/reset voxels.
*/

// World::Sphere
// ----------------------------------------------------------------------------
void World::Sphere( const float x, const float y, const float z, const float r, const uint c )
{
	const uint x1 = (uint)max( 0.0f, x - r ), x2 = (uint)min( (float)MAPWIDTH, x + r + 1 );
	const uint y1 = (uint)max( 0.0f, y - r ), y2 = (uint)min( (float)MAPHEIGHT, y + r + 1 );
	const uint z1 = (uint)max( 0.0f, z - r ), z2 = (uint)min( (float)MAPDEPTH, z + r + 1 );
	const float r2 = r * r, m2 = (r - 2) * (r - 2);
	for (uint u = x1; u < x2; u++) for (uint v = y1; v < y2; v++) for (uint w = z1; w < z2; w++)
	{
		float d2 = SQR( (float)u - x ) + SQR( (float)v - y ) + SQR( (float)w - z );
		if (d2 < r2 /* && d2 > m2 */) Set( u, v, w, c );
	}
}

// World::HDisc
// ----------------------------------------------------------------------------
void World::HDisc( const float x, const float y, const float z, const float r, const uint c )
{
	const uint x1 = (uint)max( 0.0f, x - r ), x2 = (uint)min( (float)MAPWIDTH, x + r + 1 );
	const uint z1 = (uint)max( 0.0f, z - r ), z2 = (uint)min( (float)MAPDEPTH, z + r + 1 );
	const float r2 = r * r;
	for (uint u = x1; u < x2; u++) for (uint w = z1; w < z2; w++)
	{
		float d2 = SQR( (float)u - x ) + SQR( (float)w - z );
		if (d2 < r2) Set( u, (uint)y, w, c );
	}
}

// World::Print
// ----------------------------------------------------------------------------
void World::Print( const char* text, const uint x, const uint y, const uint z, const uint c )
{
	static char f[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890+-=/\\*:;()[]{}<>!?.,'\"&#$_%^|";
	static int* r = 0;
	if (!r)
	{
		r = new int[256];
		memset( r, 0, 1024 );
		for (int i = 0; i < strlen( f ); i++) r[f[i]] = i;
	}
	for (int i = 0; i < strlen( text ); i++) if (text[i] > 32)
	{
		const int t = r[text[i]], cx = t % 13, cy = t / 13;
		uint* a = font->buffer + cx * 6 + cy * 10 * 78;
		for (int v = 0; v < 10; v++) for (int u = 0; u < 6; u++)
			if ((a[u + v * 78] & 0xffffff) == 0) Set( x + u + i * 6, y + (9 - v), z, c );
	}
}

// World::LoadSprite
// ----------------------------------------------------------------------------
uint World::LoadSprite( const char* file )
{
	// attempt to load the .vox file
	FILE* f = fopen( file, "rb" );
	if (!f) FatalError( "LoadSprite( %s ):\nFile does not exist.", file );
	static struct ChunkHeader { char name[4]; int N; union { int M; char mainName[4]; }; } header;
	fread( &header, 1, sizeof( ChunkHeader ), f );
	if (strncmp( header.name, "VOX ", 4 )) FatalError( "LoadSprite( %s ):\nBad header.", file );
	if (header.N != 150) FatalError( "LoadSprite( %s ):\nBad version (%i).", file, header.N );
	if (strncmp( header.mainName, "MAIN", 4 )) FatalError( "LoadSprite( %s ):\nNo MAIN chunk.", file );
	fread( &header.N, 1, 4, f ); // eat MAIN chunk num bytes of chunk content (N)
	fread( &header.N, 1, 4, f ); // eat MAIN chunk num bytes of children chunks (M)
	// initialize the palette to the default palette
	static uint palette[256];
	static uint default_palette[256] = {
		0x00000000, 0xffffffff, 0xffccffff, 0xff99ffff, 0xff66ffff, 0xff33ffff, 0xff00ffff, 0xffffccff, 0xffccccff, 0xff99ccff, 0xff66ccff, 0xff33ccff, 0xff00ccff, 0xffff99ff, 0xffcc99ff, 0xff9999ff,
		0xff6699ff, 0xff3399ff, 0xff0099ff, 0xffff66ff, 0xffcc66ff, 0xff9966ff, 0xff6666ff, 0xff3366ff, 0xff0066ff, 0xffff33ff, 0xffcc33ff, 0xff9933ff, 0xff6633ff, 0xff3333ff, 0xff0033ff, 0xffff00ff,
		0xffcc00ff, 0xff9900ff, 0xff6600ff, 0xff3300ff, 0xff0000ff, 0xffffffcc, 0xffccffcc, 0xff99ffcc, 0xff66ffcc, 0xff33ffcc, 0xff00ffcc, 0xffffcccc, 0xffcccccc, 0xff99cccc, 0xff66cccc, 0xff33cccc,
		0xff00cccc, 0xffff99cc, 0xffcc99cc, 0xff9999cc, 0xff6699cc, 0xff3399cc, 0xff0099cc, 0xffff66cc, 0xffcc66cc, 0xff9966cc, 0xff6666cc, 0xff3366cc, 0xff0066cc, 0xffff33cc, 0xffcc33cc, 0xff9933cc,
		0xff6633cc, 0xff3333cc, 0xff0033cc, 0xffff00cc, 0xffcc00cc, 0xff9900cc, 0xff6600cc, 0xff3300cc, 0xff0000cc, 0xffffff99, 0xffccff99, 0xff99ff99, 0xff66ff99, 0xff33ff99, 0xff00ff99, 0xffffcc99,
		0xffcccc99, 0xff99cc99, 0xff66cc99, 0xff33cc99, 0xff00cc99, 0xffff9999, 0xffcc9999, 0xff999999, 0xff669999, 0xff339999, 0xff009999, 0xffff6699, 0xffcc6699, 0xff996699, 0xff666699, 0xff336699,
		0xff006699, 0xffff3399, 0xffcc3399, 0xff993399, 0xff663399, 0xff333399, 0xff003399, 0xffff0099, 0xffcc0099, 0xff990099, 0xff660099, 0xff330099, 0xff000099, 0xffffff66, 0xffccff66, 0xff99ff66,
		0xff66ff66, 0xff33ff66, 0xff00ff66, 0xffffcc66, 0xffcccc66, 0xff99cc66, 0xff66cc66, 0xff33cc66, 0xff00cc66, 0xffff9966, 0xffcc9966, 0xff999966, 0xff669966, 0xff339966, 0xff009966, 0xffff6666,
		0xffcc6666, 0xff996666, 0xff666666, 0xff336666, 0xff006666, 0xffff3366, 0xffcc3366, 0xff993366, 0xff663366, 0xff333366, 0xff003366, 0xffff0066, 0xffcc0066, 0xff990066, 0xff660066, 0xff330066,
		0xff000066, 0xffffff33, 0xffccff33, 0xff99ff33, 0xff66ff33, 0xff33ff33, 0xff00ff33, 0xffffcc33, 0xffcccc33, 0xff99cc33, 0xff66cc33, 0xff33cc33, 0xff00cc33, 0xffff9933, 0xffcc9933, 0xff999933,
		0xff669933, 0xff339933, 0xff009933, 0xffff6633, 0xffcc6633, 0xff996633, 0xff666633, 0xff336633, 0xff006633, 0xffff3333, 0xffcc3333, 0xff993333, 0xff663333, 0xff333333, 0xff003333, 0xffff0033,
		0xffcc0033, 0xff990033, 0xff660033, 0xff330033, 0xff000033, 0xffffff00, 0xffccff00, 0xff99ff00, 0xff66ff00, 0xff33ff00, 0xff00ff00, 0xffffcc00, 0xffcccc00, 0xff99cc00, 0xff66cc00, 0xff33cc00,
		0xff00cc00, 0xffff9900, 0xffcc9900, 0xff999900, 0xff669900, 0xff339900, 0xff009900, 0xffff6600, 0xffcc6600, 0xff996600, 0xff666600, 0xff336600, 0xff006600, 0xffff3300, 0xffcc3300, 0xff993300,
		0xff663300, 0xff333300, 0xff003300, 0xffff0000, 0xffcc0000, 0xff990000, 0xff660000, 0xff330000, 0xff0000ee, 0xff0000dd, 0xff0000bb, 0xff0000aa, 0xff000088, 0xff000077, 0xff000055, 0xff000044,
		0xff000022, 0xff000011, 0xff00ee00, 0xff00dd00, 0xff00bb00, 0xff00aa00, 0xff008800, 0xff007700, 0xff005500, 0xff004400, 0xff002200, 0xff001100, 0xffee0000, 0xffdd0000, 0xffbb0000, 0xffaa0000,
		0xff880000, 0xff770000, 0xff550000, 0xff440000, 0xff220000, 0xff110000, 0xffeeeeee, 0xffdddddd, 0xffbbbbbb, 0xffaaaaaa, 0xff888888, 0xff777777, 0xff555555, 0xff444444, 0xff222222, 0xff111111
	};
	memcpy( palette, default_palette, 1024 );
	// create the sprite
	Sprite newSprite;
	SpriteFrame frame;
	int frameCount = 1; // will be overwritten if we encounter a 'PACK' chunk
	// load chunks
	while (1)
	{
		memset( &header, 0, sizeof( ChunkHeader ) );
		fread( &header, 1, sizeof( ChunkHeader ), f );
		if (feof( f ) || header.name[0] == 0) break; // assume end of file
		else if (!strncmp( header.name, "PACK", 4 )) fread( &frameCount, 1, 4, f );
		else if (!strncmp( header.name, "SIZE", 4 ))
		{
			fread( &frame.size, 1, 12, f );
			swap( frame.size.y, frame.size.z ); // sorry Magica, z=up is just wrong
		}
		else if (!strncmp( header.name, "XYZI", 4 ))
		{
			uint N, p;
			int3 s = frame.size;
			fread( &N, 1, 4, f );
			frame.buffer = new unsigned char[s.x * s.y * s.z];
			memset( frame.buffer, 0, s.x * s.y * s.z );
			for( uint i = 0; i < N; i++ )
			{
				fread( &p, 1, 4, f );
				frame.buffer[(p & 255) + ((p >> 16) & 255) * s.x + ((p >> 8) & 255) * s.x * s.y] = p >> 24;
			}
			if (newSprite.frame.size() == frameCount) FatalError( "LoadSprite( %s ):\nBad frame count.", file );
			newSprite.frame.push_back( frame );
		}
		else if (!strncmp( header.name, "RGBA", 4 )) 
		{
			fread( palette, 4, 256, f );
		}
		else if (!strncmp( header.name, "MATT", 4 ))
		{
			int dummy[8192]; // we are not supporting materials for now.
			fread( dummy, 1, header.N, f );
		}
		else break; // FatalError( "LoadSprite( %s ):\nUnknown chunk.", file );
	}
	fclose( f );
	// finalize new sprite
	int3 maxSize = make_int3( 0 );
	for( int i = 0; i < frameCount; i++ )
	{
		SpriteFrame& f = newSprite.frame[i];
		for( int s = f.size.x * f.size.y * f.size.z, i = 0; i < s; i++ ) if (f.buffer[i])
		{
			const uint c = palette[f.buffer[i]];
			const uint blue = ((c >> 16) & 255) >> 6, green = ((c >> 8) & 255) >> 5, red = (c & 255) >> 5;
			f.buffer[i] = (red << 5) + (green << 2) + blue;
		}
		maxSize.x = max( maxSize.x, f.size.x );
		maxSize.y = max( maxSize.y, f.size.y );
		maxSize.z = max( maxSize.z, f.size.z );
	}
	// create the backup frame for sprite movement
	SpriteFrame backupFrame;
	backupFrame.size = maxSize;
	backupFrame.buffer = new uchar[maxSize.x * maxSize.y * maxSize.z];
	newSprite.backup = backupFrame;
	sprite.push_back( newSprite );
	// all done, return sprite index
	return (uint)sprite.size() - 1;
}

// World::CloneSprite
// ----------------------------------------------------------------------------
uint World::CloneSprite( const uint idx )
{
	// clone frame data, wich contain pointers to shared frame data
	Sprite newSprite;
	if (idx >= sprite.size()) return 0;
	newSprite.frame = sprite[idx].frame;
	// clone backup frame, which will be unique per instance
	SpriteFrame backupFrame;
	backupFrame.size = sprite[idx].backup.size;
	backupFrame.buffer = new uchar[backupFrame.size.x * backupFrame.size.y * backupFrame.size.z];
	newSprite.backup = backupFrame;
	sprite.push_back( newSprite );
	return (uint)sprite.size() - 1;
}

// World::SpriteFrameCount
// ----------------------------------------------------------------------------
uint World::SpriteFrameCount( const uint idx )
{
	// out of bounds checks
	if (idx >= sprite.size()) return 0;
	// return frame count
	return (uint)sprite[idx].frame.size();
}

// World::MoveSpriteTo
// ----------------------------------------------------------------------------
void World::MoveSpriteTo( const uint idx, const uint x, const uint y, const uint z, const uint frame )
{
	// out of bounds checks
	if (idx >= sprite.size()) return;
	if (frame >= sprite[idx].frame.size()) return;
	// restore pixels at previous location
	const int3 l = sprite[idx].lastPos;
	if (l.x != -9999)
	{
		const SpriteFrame& b = sprite[idx].backup;
		const int3 s = b.size;
		for( int i = 0, w = 0; w < s.z; w++ ) for( int v = 0; v < s.y; v++ ) for( int u = 0; u < s.x; u++, i++ )
		{
			const uint voxel = b.buffer[i];
			Set( l.x + u, l.y + v, l.z + w, b.buffer[i] );
		}
	}
	// draw sprite at new location
	const SpriteFrame& f = sprite[idx].frame[frame];
	SpriteFrame& b = sprite[idx].backup;
	const int3& s = f.size;
	b.size = s;
	for( int i = 0, w = 0; w < s.z; w++ ) for( int v = 0; v < s.y; v++ ) for( int u = 0; u < s.x; u++, i++ )
	{
		const uint voxel = f.buffer[i];
		b.buffer[i] = Get( x + u, y + v, z + w );
		if (voxel != 0) Set( x + u, y + v, z + w, voxel );
	}
	// store last location
	sprite[idx].lastPos = make_int3( x, y, z );
}

/*
Render flow:
1. GLFW application loop in template.cpp calls World::Render:
   - If changes were made to the scene during the previous frame,
	 these will now be committed (moved in vram to their final location).
   - The render kernel is invoked. It will run in the background.
	 The render kernel stores final pixels in an OpenGL texture.
2. GLFW application loop calls Game::Tick:
   - Game::Tick executes game logic on the CPU.
   - Via the world object, changes are placed in the commit buffer.
3. GLFW application loop calls World::Commit:
   - The new top-level grid (128^3 uints, 8MB) is copied into the commit buffer.
   - Changed bricks are detected and added to the commit buffer.
   - An asynchronous copy of the commit buffer (host->device) is started.
4. GLFW application loop draws a full-screen quad, using the texture
   filled by the render kernel.
The Game::Tick CPU code and the asynchronous copy of the commit buffer are
typically hidden completely behind the OpenCL render kernel.
In the next frame, the commit kernel synchronizes with this copy,
to ensure it completes before the commit kernel is executed on this data.
*/

// World::Render
// ----------------------------------------------------------------------------
void World::Render()
{
	// finalize the asynchronous copy by executing the commit kernel
	if (copyInFlight)
	{
		if (tasks > 0)
		{
			committer->SetArgument( 0, (int)tasks );
			committer->Run( (tasks + 63) & (65536 - 32), 4, &copyDone, &commitDone );
		}
		copyInFlight = false, commitInFlight = true;
	}
	// run rendering kernel
	mat4 M = camMat;
	const float aspectRatio = (float)SCRWIDTH / SCRHEIGHT;
	params.E = TransformPosition( make_float3( 0 ), M );
	params.oneOverRes = make_float2( 1.0f / SCRWIDTH, 1.0f / SCRHEIGHT );
	params.p0 = TransformPosition( make_float3( -aspectRatio, 1, 3 ), M );
	params.p1 = TransformPosition( make_float3( aspectRatio, 1, 3 ), M );
	params.p2 = TransformPosition( make_float3( -aspectRatio, -1, 3 ), M );
	params.R0 = RandomUInt();
	static uint frame = 0;
	params.frame = frame++;
	paramBuffer->CopyToDevice( false );
	renderer->Run( screen, make_int2( 32, 4 ) );
}

// World::Commit
// ----------------------------------------------------------------------------
void World::Commit()
{
	Timer t;
	t.reset();
	uint* idx = commit + gridSize;
	uchar* dst = (uchar*)(idx + MAXCOMMITS);
	tasks = 0;
	for (uint j = 0; j < BRICKCOUNT / 32; j++) if (IsDirty32( j )) for (uint k = 0; k < 32; k++)
	{
		const uint i = j * 32 + k;
		if (!IsDirty( i )) continue;
		*idx++ = (uint)i; // store index of modified brick at start of commit buffer
		StreamCopy( (__m256i*)dst, (__m256i*)(brick + i * BRICKSIZE), BRICKSIZE / 8 );
		dst += BRICKSIZE, tasks++;
	}
	// asynchroneously copy the CPU data to the GPU via the commit buffer
	if (tasks > 0 || firstFrame)
	{
		static uint* dst = 0;
		if (dst == 0)
		{
			dst = (uint*)clEnqueueMapBuffer( Kernel::GetQueue(), pinned, 1, CL_MAP_WRITE, 0, commitSize * 8, 0, 0, 0, 0 );
			StreamCopy( (__m256i*)(dst + commitSize), (__m256i*)commit, commitSize / 8 );
			commit = dst + commitSize, grid = commit; // top-level grid resides at the start of the commit buffer
		}
		if (commitInFlight) /* wait for the previous commit to complete */ clWaitForEvents( 1, &commitDone );
		// copy commit buffer to start of pinned buffer for final DMA transfer
		StreamCopyMT( (__m256i*)dst, (__m256i*)commit, commitSize / 8 );
		clEnqueueWriteBuffer( Kernel::GetQueue2(), devmem, 0, 0, commitSize * 4, dst, 0, 0, 0 );
		// copy the top-level grid to a 3D OpenCL image buffer (vram to vram)
		size_t origin[3] = { 0, 0, 0 };
		size_t region[3] = { GRIDWIDTH, GRIDDEPTH, GRIDHEIGHT };
		clEnqueueCopyBufferToImage( Kernel::GetQueue2(), devmem, gridMap, 0, origin, region, 0, 0, &copyDone );
		copyInFlight = true;
		firstFrame = false;
	}
	// reset the bitfield for the next frame
	ClearMarks();
}

// World::StreamCopyMT
// ----------------------------------------------------------------------------
#define COPYTHREADS	4
void World::StreamCopyMT( __m256i* dst, __m256i* src, const int N )
{
	// fast copying of large 32-byte aligned / multiple of 32 sized data blocks:
	// streaming __m256 read/writes, on multiple threads
	static JobManager* jm = JobManager::GetJobManager();
	static CopyJob cj[COPYTHREADS];
	__m256i* s = src, * d = dst;
	int M = (int)N;
	for (int i = 0; i < (COPYTHREADS - 1); i++, s += M / COPYTHREADS, d += M / COPYTHREADS)
	{
		cj[i].dst = d, cj[i].src = s, cj[i].N = M / COPYTHREADS;
		jm->AddJob2( &cj[i] );
	}
	cj[COPYTHREADS - 1].dst = d;
	cj[COPYTHREADS - 1].src = s;
	cj[COPYTHREADS - 1].N = M - (COPYTHREADS - 1) * (M / COPYTHREADS);
	jm->AddJob2( &cj[COPYTHREADS - 1] );
	jm->RunJobs();
}

// EOF