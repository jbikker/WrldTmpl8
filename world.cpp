#include "template.h"
#include "bluenoise.h"

using namespace Tmpl8;

// World Constructor
// ----------------------------------------------------------------------------
World::World( const uint targetID )
{
	// create the commit buffer, used to sync CPU-side changes to the GPU
	const uint gridSize = GRIDWIDTH * GRIDHEIGHT * GRIDDEPTH;
	const uint commitSize = BRICKCOMMITSIZE / 4 + gridSize;
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
		for( int i = 0; i < strlen( f ); i++ ) r[f[i]] = i;
	}
	for( int i = 0; i < strlen( text ); i++ ) if (text[i] > 32)
	{
		const int t = r[text[i]], cx = t % 13, cy = t / 13;
		uint* a = font->buffer + cx * 6 + cy * 10 * 78;
		for( int v = 0; v < 10; v++ ) for( int u = 0; u < 6; u++ )
			if ((a[u + v * 78] & 0xffffff) == 0) Set( x + u + i * 6, y + (9 - v), z, c );
	}
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
	static const uint gridSize = GRIDWIDTH * GRIDHEIGHT * GRIDDEPTH;
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
	if (tasks > 0)
	{
		static const uint commitSize = BRICKCOMMITSIZE / 4 + gridSize;
		static uint* dst = 0;
		if (dst == 0)
		{
			dst = (uint*)clEnqueueMapBuffer( Kernel::GetQueue(), pinned, 1, CL_MAP_WRITE, 0, commitSize * 8, 0, 0, 0, 0 );
			StreamCopy( (__m256i*)(dst + commitSize), (__m256i*)commit, commitSize / 8 );
			commit = dst + commitSize, grid = commit; // top-level grid resides at the start of the commit buffer
		}
		if (commitInFlight) /* wait for the previous commit to complete */ clWaitForEvents( 1, &commitDone );
		StreamCopyMT( (__m256i*)dst, (__m256i*)commit, commitSize / 8 );
		clEnqueueWriteBuffer( Kernel::GetQueue2(), devmem, 0, 0, commitSize * 4, dst, 0, 0, 0 );
		// copy the top-level grid to a 3D OpenCL image buffer (vram to vram)
		size_t origin[3] = { 0, 0, 0 };
		size_t region[3] = { GRIDWIDTH, GRIDDEPTH, GRIDHEIGHT };
		clEnqueueCopyBufferToImage( Kernel::GetQueue2(), devmem, gridMap, 0, origin, region, 0, 0, &copyDone );
		copyInFlight = true;
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