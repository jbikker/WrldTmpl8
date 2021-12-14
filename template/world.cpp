#include "precomp.h"

#define OGT_VOX_IMPLEMENTATION
#include "lib/ogt_vox.h"

// Acknowledgements:
// B&H'21 = Brian Janssen and Hugo Peters, INFOMOV'21 assignment
// CO'21  = Christian Oliveros, INFOMOV'21 assignment
// MB'21  = Maarten van den Berg, INFOMOV '21 assignment

using namespace Tmpl8;

static const uint gridSize = GRIDSIZE * sizeof( uint );
static const uint commitSize = BRICKCOMMITSIZE + gridSize;

// helper defines for inline ray tracing
#define OFFS_X		((bits >> 5) & 1)			// extract grid plane offset over x (0 or 1)
#define OFFS_Y		((bits >> 13) & 1)			// extract grid plane offset over y (0 or 1)
#define OFFS_Z		(bits >> 21)				// extract grid plane offset over z (0 or 1)
#define DIR_X		((bits & 3) - 1)			// ray dir over x (-1 or 1)
#define DIR_Y		(((bits >> 8) & 3) - 1)		// ray dir over y (-1 or 1)
#define DIR_Z		(((bits >> 16) & 3) - 1)	// ray dir over z (-1 or 1)
#define BMSK		(BRICKDIM - 1)
#define BDIM2		(BRICKDIM * BRICKDIM)
#define BPMX		(MAPWIDTH - BRICKDIM)
#define BPMY		(MAPHEIGHT - BRICKDIM)
#define BPMZ		(MAPDEPTH - BRICKDIM)
#define TOPMASK3	(((1023 - BMSK) << 20) + ((1023 - BMSK) << 10) + (1023 - BMSK))
#define SELECT(a,b,c) ((c)?(b):(a))

// helpers for skydome sampling
float3 DiffuseReflectionCosWeighted( const float r0, const float r1, const float3& N )
{
	const float3 T = normalize( cross( N, fabs( N.y ) > 0.99f ? make_float3( 1, 0, 0 ) : make_float3( 0, 1, 0 ) ) );
	const float3 B = cross( T, N );
	const float term1 = TWOPI * r0, term2 = sqrt( 1 - r1 );
	return (cosf( term1 ) * term2 * T) + (sinf( term1 ) * term2) * B + sqrt( r1 ) * N;
}
float SphericalTheta( const float3& v ) { return acosf( clamp( v.z, -1.f, 1.f ) ); }
float SphericalPhi( const float3& v ) { const float p = atan2f( v.y, v.x ); return (p < 0) ? (p + 2 * PI) : p; }

// World Constructor
// ----------------------------------------------------------------------------
World::World( const uint targetID )
{
	// create the commit buffer, used to sync CPU-side changes to the GPU
	if (!Kernel::InitCL()) FATALERROR( "Failed to initialize OpenCL" );
	devmem = clCreateBuffer( Kernel::GetContext(), CL_MEM_READ_ONLY, commitSize, 0, 0 );
	modified = new uint[BRICKCOUNT / 32]; // 1 bit per brick, to track 'dirty' bricks
#if GRID_IN_3DIMAGE == 1
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
#else
	gridMap = clCreateBuffer( Kernel::GetContext(), CL_MEM_READ_WRITE, GRIDWIDTH * GRIDHEIGHT * GRIDDEPTH * 4, 0, 0 );
#endif
	// create brick storage
	brick = (PAYLOAD*)_aligned_malloc( CHUNKCOUNT * CHUNKSIZE, 64 );
	for (int i = 0; i < CHUNKCOUNT; i++)
	{
		brickBuffer[i] = new Buffer( CHUNKSIZE / 4 /* dwords */, Buffer::DEFAULT, (uchar*)brick + CHUNKSIZE * i );
		brickBuffer[i]->CopyToDevice();
	}
	brickInfo = (BrickInfo*)_aligned_malloc( BRICKCOUNT * sizeof( BrickInfo ), 64 );
	// create a cyclic array for unused bricks (all of them, for now)
	trash = (uint*)_aligned_malloc( BRICKCOUNT * 4, 64 );
	memset( trash, 0, BRICKCOUNT * 4 );
	for (uint i = 0; i < BRICKCOUNT; i++) trash[(i * 31 /* prevent false sharing*/) & (BRICKCOUNT - 1)] = i;
	// prepare a test world
	grid = gridOrig = (uint*)_aligned_malloc( GRIDWIDTH * GRIDHEIGHT * GRIDDEPTH * 4, 64 );
	memset( grid, 0, GRIDWIDTH * GRIDHEIGHT * GRIDDEPTH * sizeof( uint ) );
	DummyWorld();
	ClearMarks(); // clear 'modified' bit array
	// report memory usage
	printf( "Allocated %iMB on CPU and GPU for the top-level grid.\n", (int)(gridSize >> 20) );
	printf( "Allocated %iMB on CPU and GPU for %ik bricks.\n", (int)((BRICKCOUNT * BRICKSIZE) >> 20), (int)(BRICKCOUNT >> 10) );
	printf( "Allocated %iKB on CPU for bitfield.\n", (int)(BRICKCOUNT >> 15) );
	printf( "Allocated %iMB on CPU for brickInfo.\n", (int)((BRICKCOUNT * sizeof( BrickInfo )) >> 20) );
	// initialize kernels
	paramBuffer = new Buffer( sizeof( RenderParams ) / 4, Buffer::DEFAULT | Buffer::READONLY, &params );
	history[0] = new Buffer( 4 * SCRWIDTH * SCRHEIGHT );
	history[1] = new Buffer( 4 * SCRWIDTH * SCRHEIGHT );
	tmpFrame = new Buffer( 4 * SCRWIDTH * SCRHEIGHT );
	renderer = new Kernel( "cl/kernels.cl", "render" );
	finalizer = new Kernel( renderer->GetProgram(), "finalize" );
	unsharpen = new Kernel( renderer->GetProgram(), "unsharpen" );
	committer = new Kernel( renderer->GetProgram(), "commit" );
	batchTracer = new Kernel( renderer->GetProgram(), "traceBatch" );
	batchToVoidTracer = new Kernel( renderer->GetProgram(), "traceBatchToVoid" );
#if CELLSKIPPING == 1
	hermitFinder = new Kernel( renderer->GetProgram(), "findHermits" );
	hermitFinder->SetArgument( 0, &devmem );
#endif
#if THIRDLEVEL == 1
	uberGrid = clCreateBuffer( Kernel::GetContext(), CL_MEM_READ_WRITE, UBERWIDTH * UBERHEIGHT * UBERDEPTH, 0, 0 );
	uberGridUpdater = new Kernel( renderer->GetProgram(), "updateUberGrid" );
	uberGridUpdater->SetArgument( 0, &devmem );
	uberGridUpdater->SetArgument( 1, &uberGrid );
#endif
	targetTextureID = targetID;
	committer->SetArgument( 1, &devmem );
	committer->SetArgument( 2, brickBuffer[0] );
	committer->SetArgument( 3, brickBuffer[1] );
	committer->SetArgument( 4, brickBuffer[2] );
	committer->SetArgument( 5, brickBuffer[3] );
	batchTracer->SetArgument( 0, &gridMap );
	batchTracer->SetArgument( 1, brickBuffer[0] );
	batchTracer->SetArgument( 2, brickBuffer[1] );
	batchTracer->SetArgument( 3, brickBuffer[2] );
	batchTracer->SetArgument( 4, brickBuffer[3] );
	batchToVoidTracer->SetArgument( 0, &gridMap );
	batchToVoidTracer->SetArgument( 1, brickBuffer[0] );
	batchToVoidTracer->SetArgument( 2, brickBuffer[1] );
	batchToVoidTracer->SetArgument( 3, brickBuffer[2] );
	batchToVoidTracer->SetArgument( 4, brickBuffer[3] );
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
	delete[] data32;
	// load a bitmap font for the print command
	font = new Surface( "assets/font.png" );
}

// World Destructor
// ----------------------------------------------------------------------------
World::~World()
{
	cl_program sharedProgram = renderer->GetProgram();
	delete committer;
	delete renderer;
	_aligned_free( gridOrig ); // grid itself gets changed after allocation
	_aligned_free( brick );
	for (int i = 0; i < 4; i++) delete brickBuffer[i];
	_aligned_free( brickInfo );
	_aligned_free( trash );
	delete screen;
	delete paramBuffer;
	delete sky;
	delete blueNoise;
	delete font;
	clReleaseProgram( sharedProgram );
}

// World::ForceSyncAllBricks: send brick array to GPU
// ----------------------------------------------------------------------------
void World::ForceSyncAllBricks()
{
	for (int i = 0; i < CHUNKCOUNT; i++) brickBuffer[i]->CopyToDevice();
}

// World::DummyWorld: box
// ----------------------------------------------------------------------------
void World::DummyWorld()
{
	Clear();
	for (int y = 0; y < 256; y++) for (int z = 0; z < 1024; z++)
	{
		Set( 6, y, z, WHITE );
		Set( 1017, y, z, WHITE );
	}
	for (int x = 0; x < 1024; x++) for (int z = 0; z < 1024; z++)
	{
		Set( x, 6, z, WHITE );
		Set( x, 256, z, WHITE );
	}
	for (int x = 0; x < 1024; x++) for (int y = 0; y < 256; y++)
	{
		Set( x, y, 6, WHITE );
		Set( x, y, 1017, WHITE );
	}
	// performance note: these coords prevent exessive brick traversal.
}

// World::Clear
// ----------------------------------------------------------------------------
void World::Clear()
{
	// easiest top just clear the top-level grid and recycle all bricks
	memset( grid, 0, GRIDWIDTH * GRIDHEIGHT * GRIDDEPTH * sizeof( uint ) );
	memset( trash, 0, BRICKCOUNT * 4 );
	for (uint i = 0; i < BRICKCOUNT; i++) trash[(i * 31 /* prevent false sharing*/) & (BRICKCOUNT - 1)] = i;
	trashHead = BRICKCOUNT, trashTail = 0;
	ClearMarks();
}

// World::Fill
// ----------------------------------------------------------------------------
void World::Fill( const uint c )
{
	// fill the top-level grid and recycle all bricks
	for (int y = 0; y < GRIDHEIGHT; y++) for (int z = 0; z < GRIDDEPTH; z++) for (int x = 0; x < GRIDWIDTH; x++)
		grid[x + z * GRIDWIDTH + y * GRIDWIDTH * GRIDDEPTH] = c << 1;
	memset( trash, 0, BRICKCOUNT * 4 );
	for (uint i = 0; i < BRICKCOUNT; i++) trash[(i * 31 /* prevent false sharing*/) & (BRICKCOUNT - 1)] = i;
	trashHead = BRICKCOUNT, trashTail = 0;
	ClearMarks();
}

// World::ScrollX
// ----------------------------------------------------------------------------
void World::ScrollX( const int offset )
{
	if (offset % BRICKDIM != 0) FatalError( "ScrollX( %i ):\nCan only scroll by multiples of %i.", offset, BRICKDIM );
	const int o = abs( offset / BRICKDIM );
	for (uint z = 0; z < GRIDDEPTH; z++) for (uint y = 0; y < GRIDHEIGHT; y++)
	{
		uint* line = grid + z * GRIDWIDTH + y * GRIDWIDTH * GRIDDEPTH;
		uint backup[GRIDWIDTH];
		if (offset < 0)
		{
			for (int x = 0; x < o; x++) backup[x] = line[x];
			for (int x = 0; x < GRIDWIDTH - o; x++) line[x] = line[x + o];
			for (int x = 0; x < o; x++) line[GRIDWIDTH - o + x] = backup[x];
		}
		else
		{
			for (int x = 0; x < o; x++) backup[x] = line[GRIDWIDTH - o + x];
			for (int x = GRIDWIDTH - 1; x >= o; x--) line[x] = line[x - o];
			for (int x = 0; x < o; x++) line[x] = backup[x];
		}
	}
}

// World::ScrollX
// ----------------------------------------------------------------------------
void World::ScrollY( const int offset )
{
	if (offset % BRICKDIM != 0) FatalError( "ScrollY( %i ):\nCan only scroll by multiples of %i.", offset, BRICKDIM );
	// TODO
}

// World::ScrollX
// ----------------------------------------------------------------------------
void World::ScrollZ( const int offset )
{
	if (offset % BRICKDIM != 0) FatalError( "ScrollZ( %i ):\nCan only scroll by multiples of %i.", offset, BRICKDIM );
	// TODO
}

// World::LoadSky
// ----------------------------------------------------------------------------
void World::LoadSky( const char* filename, const float scale )
{
	// attempt to load skydome from compressed binary file
	Timer t;
	printf( "loading hdr data... " );
	int bpp; // bytes per pixel
	float* pixels = stbi_loadf( filename, &skySize.x, &skySize.y, &bpp, 0 );
	printf( " completed in %5.2fms\n", t.elapsed() * 1000.0f );
	// add a checkerboard. Note: could really use a blur.
	if (Game::checkerBoard)
	{
		int y2 = skySize.y, y1 = y2 / 2, x2 = skySize.x;
		float w = (float)skySize.x, h = (float)skySize.y;
		for (int y = y1; y < y2; y++) for (int x = 0; x < x2; x++)
		{
			const float t = (x - 0.5f * w) / w * 2 * PI;
			const float p = -(y - 0.5f * h) / h * PI;
			const float3 D = make_float3( cosf( p ) * cosf( t ), sinf( p ), cosf( p ) * sinf( t ) );
			const float3 O = make_float3( 0, 20, 0 ), P = O - 20.0f / D.y * D;
			const int u = (int)(P.x * 0.02f + 100000);
			const int v = (int)(P.z * 0.02f + 100000);
			const float d = ((u + v) & 1) ? 0.7f : 0.05f;
			for (int i = 0; i < 3; i++) pixels[x * 3 + y * x2 * 3 + i] = d;
		}
	}
	if (Game::debugLights)
	{
		// rgb test
		float w = (float)skySize.x, h = (float)skySize.y;
		for (int y = 0; y < skySize.y; y++) for (int x = 0; x < skySize.x; x++)
		{
			const float t = (x - 0.5f * w) / w * 2 * PI, p = -(y - 0.5f * h) / h * PI;
			const float3 D = make_float3( cosf( p ) * cosf( t ), sinf( p ), cosf( p ) * sinf( t ) );
			if (-D.x > 0.97f)
				pixels[x * 3 + y * 3 * skySize.x] = 10,
				pixels[x * 3 + y * 3 * skySize.x + 1] = 0,
				pixels[x * 3 + y * 3 * skySize.x + 2] = 0;
			else if (-D.z > 0.97f)
				pixels[x * 3 + y * 3 * skySize.x] = 0,
				pixels[x * 3 + y * 3 * skySize.x + 1] = 10,
				pixels[x * 3 + y * 3 * skySize.x + 2] = 0;
		}
	}
	if (Game::artificialSun)
	{
		float w = (float)skySize.x, h = (float)skySize.y;
		const float3 L = normalize( make_float3( 2, 1.5f, 3 ) );
		for (int y = 0; y < skySize.y; y++) for (int x = 0; x < skySize.x; x++)
		{
			const float t = (x - 0.5f * w) / w * 2 * PI, p = -(y - 0.5f * h) / h * PI;
			const float3 D = make_float3( cosf( p ) * cosf( t ), sinf( p ), cosf( p ) * sinf( t ) );
			if (dot( D, L ) > 0.999f)
				pixels[x * 3 + y * 3 * skySize.x] = 24,
				pixels[x * 3 + y * 3 * skySize.x + 1] = 24,
				pixels[x * 3 + y * 3 * skySize.x + 2] = 18;
		}
	}
	// convert to float4
	float4* pixel4 = new float4[skySize.x * skySize.y];
	float* src = pixels;
	for (int y = 0; y < skySize.y; y++) for (int x = 0; x < skySize.x; x++, src += 3)
	{
		float3 p3 = make_float3( src[0], src[1], src[2] );
		pixel4[x + y * skySize.x] = scale * make_float4( p3, 1 );
	}
	delete pixels;
	// make the final buffer
	sky = new Buffer( skySize.x * skySize.y * 4, Buffer::READONLY, pixel4 );
	sky->CopyToDevice();
	// update the sky lights
	UpdateSkylights();
}

// World::SampleSky
// ----------------------------------------------------------------------------
float3 World::SampleSky( const float3& D )
{
	const int iw = skySize.x, ih = skySize.y;
	const float w = (float)iw, h = (float)ih;
	const float u = w * SphericalPhi( D ) * INV2PI - 0.5f;
	const float v = h * SphericalTheta( D ) * INVPI - 0.5f;
	const float fu = u - floorf( u ), fv = v - floorf( v );
	const int iu = (int)u, iv = (int)v;
	const uint idx1 = (iu + iv * iw) % (iw * ih);
	const uint idx2 = (iu + 1 + iv * iw) % (iw * ih);
	const uint idx3 = (iu + (iv + 1) * iw) % (iw * ih);
	const uint idx4 = (iu + 1 + (iv + 1) * iw) % (iw * ih);
	const float4 s =
		((float4*)sky->hostBuffer)[idx1] * (1 - fu) * (1 - fv) +
		((float4*)sky->hostBuffer)[idx2] * fu * (1 - fv) +
		((float4*)sky->hostBuffer)[idx3] * (1 - fu) * fv +
		((float4*)sky->hostBuffer)[idx4] * fu * fv;
	return make_float3( s );
}

// World::UpdateSkylights
// ----------------------------------------------------------------------------
void World::UpdateSkylights()
{
	float3 N[6] = {
		make_float3( -1, 0, 0 ), make_float3( 1, 0, 0 ),
		make_float3( 0, 0, -1 ), make_float3( 0, 0, 1 ),
		make_float3( 0, -1, 0 ), make_float3( 0, 1, 0 ),
	};
	Timer t;
	printf( "Updating skylights... " );
	for (int i = 0; i < 6; i++)
	{
		// spawn a large number of random directions around the normal (cosine distributed)
		float3 sum = make_float3( 0 );
		for (int j = 0; j < 4096; j++)
		{
			float sx = (j & 15) * 0.0625f, sy = ((j >> 4) & 15) * 0.0625f; // 256 strata
			const float r0 = RandomFloat() * 0.0625f + sx, r1 = RandomFloat() * 0.0625f + sy;
			const float3 R = DiffuseReflectionCosWeighted( r0, r1, N[i] );
			sum += SampleSky( R );
		}
		skyLight[i] = make_float4( sum * (1.0f / 4096.0f), 0 );
	}
	printf( "done, in %5.2fms.\n", t.elapsed() * 1000.0f ); // ~5ms on a single thread.
}

/*
Some primitives for drawing in the 3D voxel world.
Note that these rely on World::Set to actually set/reset voxels.
*/

// World::Sphere
// ----------------------------------------------------------------------------
void World::Sphere( const float x, const float y, const float z, const float r, const uint c )
{
	const uint x1 = (uint)max( 0.f, x - r ), x2 = (uint)min( MAPWIDTH - 1.f, x + r + 1 );
	const uint y1 = (uint)max( 0.f, y - r ), y2 = (uint)min( MAPHEIGHT - 1.f, y + r + 1 );
	const uint z1 = (uint)max( 0.f, z - r ), z2 = (uint)min( MAPDEPTH - 1.f, z + r + 1 );
	if (x1 >= MAPWIDTH || y1 >= MAPHEIGHT || z1 >= MAPDEPTH ||
		x2 >= MAPWIDTH || y2 >= MAPHEIGHT || z2 >= MAPDEPTH) return;
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
	const uint x1 = (uint)max( 0.f, x - r ), x2 = (uint)min( MAPWIDTH - 1.f, x + r + 1 );
	const uint z1 = (uint)max( 0.f, z - r ), z2 = (uint)min( MAPDEPTH - 1.f, z + r + 1 );
	if (x1 >= MAPWIDTH || y >= MAPHEIGHT || z1 >= MAPDEPTH ||
		x2 >= MAPWIDTH || z2 >= MAPDEPTH) return;
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

// SpriteManager::LoadSprite
// ----------------------------------------------------------------------------
uint SpriteManager::LoadSprite( const char* voxFile, bool largeModel )
{
	if (strstr( voxFile, ".vx" ))
	{
		// load it from our custom file format
		Sprite* newSprite = new Sprite();
		gzFile f = gzopen( voxFile, "rb" );
		int frames, pls;
		gzread( f, &pls, 4 );
		gzread( f, &frames, 4 );
		if (pls != PAYLOADSIZE) FatalError( "File was saved with a different voxel size." );
		int3 maxSize = make_int3( 0 );
		for (int i = 0; i < frames; i++)
		{
			SpriteFrame* sf = new SpriteFrame();
			newSprite->frame.push_back( sf );
			gzread( f, &sf->size, 12 );
			gzread( f, &sf->drawListSize, 4 );
			sf->drawPos = new uint[sf->drawListSize];
			sf->drawVal = new PAYLOAD[sf->drawListSize];
			gzread( f, sf->drawPos, sf->drawListSize * sizeof( uint ) );
			gzread( f, sf->drawVal, sf->drawListSize * PAYLOADSIZE );
			maxSize.x = max( maxSize.x, sf->size.x );
			maxSize.y = max( maxSize.y, sf->size.y );
			maxSize.z = max( maxSize.z, sf->size.z );
		}
		gzclose( f );
		// create the backup frame for sprite movement
		SpriteFrame* backupFrame = new SpriteFrame();
		backupFrame->size = maxSize;
		backupFrame->buffer = new PAYLOAD[maxSize.x * maxSize.y * maxSize.z];
		newSprite->backup = backupFrame;
		sprite.push_back( newSprite );
	}
	else if (largeModel)
	{
		// load via opengametools .vox file reader
		FILE* fp = fopen( voxFile, "rb" );
		uint32_t buffer_size = _filelength( _fileno( fp ) );
		uint8_t* buffer = new uint8_t[buffer_size];
		fread( buffer, buffer_size, 1, fp );
		fclose( fp );
		const ogt_vox_scene* scene = ogt_vox_read_scene( buffer, buffer_size );
		delete[] buffer;
		// find bounds of voxel scene
		int3 bmin = make_int3( 0xffffff ), bmax = make_int3( -0xffffff );
		for (uint i = 0; i < scene->num_instances; i++)
		{
			const ogt_vox_instance& instance = scene->instances[i];
			const mat4 M = mat4::FromColumnMajor( *(mat4*)&instance.transform );
			int3 pos = make_int3( M.GetTranslation() );
			const ogt_vox_model* model = scene->models[instance.model_index];
			int3 size = make_int3( M * make_float4( make_int3( model->size_x, model->size_y, model->size_z ), 0 ) );
			bmin = min( bmin, min( pos, pos + size ) );
			bmax = max( bmax, max( pos, pos + size ) );
		}
		// create sprite
		Sprite* newSprite = new Sprite();
		SpriteFrame* frame = new SpriteFrame();
		frame->size = (bmax + make_int3( 1, 1, 1 )) - bmin;
		swap( frame->size.y, frame->size.z );
		int3 s = frame->size;
		frame->buffer = (PAYLOAD*)_aligned_malloc( s.x * s.y * s.z * PAYLOADSIZE, 64 ); // B&H'21
		memset( frame->buffer, 0, s.x * s.y * s.z * PAYLOADSIZE );
		// store the data in the frame
		for (uint i = 0; i < scene->num_instances; i++)
		{
			// get instance model data
			// note: magicavoxel author is evil; voxel data may be transformed.
			const ogt_vox_instance& instance = scene->instances[i];
			const mat4 M = mat4::FromColumnMajor( *(mat4*)&instance.transform );
			const int3 pos = make_int3( M.GetTranslation() ) - bmin;
			const ogt_vox_model* model = scene->models[instance.model_index];
			const int3 size = make_int3( model->size_x, model->size_y, model->size_z );
			const int3 MX = make_int3( make_float3( M.cell[0], M.cell[1], M.cell[2] ) );
			const int3 MY = make_int3( make_float3( M.cell[4], M.cell[5], M.cell[6] ) );
			const int3 MZ = make_int3( make_float3( M.cell[8], M.cell[9], M.cell[10] ) );
			// store actual voxels
			for (int i = 0, z = 0; z < size.z; z++) for (int y = 0; y < size.y; y++) for (int x = 0; x < size.x; x++, i++)
			{
				const uint8_t v = model->voxel_data[i];
				if (!v) continue;
				const int3 l = make_int3( x, y, z );
				const int3 p = make_int3( dot( MX, l ), dot( MY, l ), dot( MZ, l ) ) + pos;
				frame->buffer[p.x + p.z * frame->size.x + p.y * frame->size.x * frame->size.y] = v;
			}
		}
		// finalize frame
		vector<uint> drawList;
		vector<PAYLOAD> voxel;
		for (int i = 0, z = 0; z < frame->size.z; z++)
		{
			for (int y = 0; y < frame->size.y; y++)
			{
				for (int x = 0; x < frame->size.x; x++, i++)
				{
					if (frame->buffer[i])
					{
						const ogt_vox_rgba c = scene->palette.color[frame->buffer[i]];
					#if PAYLOADSIZE == 1
						const uint blue = min( 3u, (uint)(c.r + 31) >> 6 );
						const uint green = min( 7u, (uint)(c.g + 15) >> 5 );
						const uint red = min( 7u, (uint)c.b >> 5 );
						const uint p = (red << 5) + (green << 2) + blue;
						const PAYLOAD v = (p == 0) ? (1 << 5) : p;
					#else
						const uint blue = min( 15u, (uint)(c.r + 7) >> 4 );
						const uint green = min( 15u, (uint)(c.g + 7) >> 4 );
						const uint red = min( 15u, (uint)(c.b + 7) >> 4 );
						const uint p = (red << 8) + (green << 4) + blue;
						const PAYLOAD v = (p == 0) ? 1 : p;
					#endif
						frame->buffer[i] = v;
						drawList.push_back( x + (y << 10) + (z << 20) );
						voxel.push_back( frame->buffer[i] );
					}
				}
			}
		}
		frame->drawPos = (uint*)_aligned_malloc( drawList.size() * sizeof( uint ), 64 );
		frame->drawVal = (PAYLOAD*)_aligned_malloc( drawList.size() * PAYLOADSIZE, 64 );
		for (size_t s = drawList.size(), i = 0; i < s; i++)
			frame->drawPos[i] = drawList[i],
			frame->drawVal[i] = voxel[i];
		frame->drawListSize = (uint)drawList.size();
		newSprite->frame.push_back( frame );
		// create the backup frame for sprite movement
		SpriteFrame* backupFrame = new SpriteFrame();
		backupFrame->size = frame->size;
		backupFrame->buffer = new PAYLOAD[frame->size.x * frame->size.y * frame->size.z];
		newSprite->backup = backupFrame;
		sprite.push_back( newSprite );
		// clear ogt_vox_scene
		ogt_vox_destroy_scene( scene );
	}
	else
	{
		// attempt to load the .vox file
		FILE* file = fopen( voxFile, "rb" );
		if (!file) FatalError( "LoadSprite( %s ):\nFile does not exist.", voxFile );
		static struct ChunkHeader { char name[4]; int N; union { int M; char mainName[4]; }; } header;
		fread( &header, 1, sizeof( ChunkHeader ), file );
		if (strncmp( header.name, "VOX ", 4 )) FatalError( "LoadSprite( %s ):\nBad header.", voxFile );
		if (header.N != 150) FatalError( "LoadSprite( %s ):\nBad version (%i).", voxFile, header.N );
		if (strncmp( header.mainName, "MAIN", 4 )) FatalError( "LoadSprite( %s ):\nNo MAIN chunk.", voxFile );
		fread( &header.N, 1, 4, file ); // eat MAIN chunk num bytes of chunk content (N)
		fread( &header.N, 1, 4, file ); // eat MAIN chunk num bytes of children chunks (M)
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
		Sprite* newSprite = new Sprite();
		SpriteFrame* frame = 0;
		int frameCount = 1; // will be overwritten if we encounter a 'PACK' chunk
		// load chunks
		while (1)
		{
			memset( &header, 0, sizeof( ChunkHeader ) );
			fread( &header, 1, sizeof( ChunkHeader ), file );
			if (feof( file ) || header.name[0] == 0) break; // assume end of file
			else if (!strncmp( header.name, "PACK", 4 ))
			{
				fread( &frameCount, 1, 4, file );
			}
			else if (!strncmp( header.name, "SIZE", 4 ))
			{
				if (!frame) frame = new SpriteFrame();
				fread( &frame->size, 1, 12, file );
				swap( frame->size.y, frame->size.z ); // sorry Magica, z=up is just wrong
			}
			else if (!strncmp( header.name, "XYZI", 4 ))
			{
				struct { uchar x, y, z, i; } xyzi;
				uint N;
				int3 s = frame->size;
				fread( &N, 1, 4, file );
				frame->buffer = (PAYLOAD*)_aligned_malloc( s.x * s.y * s.z * PAYLOADSIZE, 64 ); // B&H'21
				memset( frame->buffer, 0, s.x * s.y * s.z * PAYLOADSIZE );
				for (uint i = 0; i < N; i++)
				{
					fread( &xyzi, 1, 4, file );
					frame->buffer[xyzi.x + xyzi.z * s.x + xyzi.y * s.x * s.y] = xyzi.i;
				}
				if (newSprite->frame.size() == frameCount) FatalError( "LoadSprite( %s ):\nBad frame count.", voxFile );
				newSprite->frame.push_back( frame );
				frame = 0;
			}
			else if (!strncmp( header.name, "RGBA", 4 ))
			{
				fread( palette, 4, 256, file );
			}
			else if (!strncmp( header.name, "MATT", 4 ))
			{
				int dummy[8192]; // we are not supporting materials for now.
				fread( dummy, 1, header.N, file );
			}
			else if (!strncmp( header.name, "MATL", 4 ))
			{
				int dummy[8192]; // we are not supporting materials for now.
				fread( dummy, 1, header.N, file );
			}
			else if (!strncmp( header.name, "nTRN", 4 ))
			{
				int dummy[128]; // we are not supporting the extended world hierarchy for now.
				fread( dummy, 1, header.N, file );
			}
			else if (!strncmp( header.name, "nGRP", 4 ))
			{
				int dummy[128]; // we are not supporting the extended world hierarchy for now.
				fread( dummy, 1, header.N, file );
			}
			else if (!strncmp( header.name, "nSHP", 4 ))
			{
				int dummy[128]; // we are not supporting the extended world hierarchy for now.
				fread( dummy, 1, header.N, file );
			}
			else if (!strncmp( header.name, "LAYR", 4 ))
			{
				int dummy[128]; // we are not supporting the extended world hierarchy for now.
				fread( dummy, 1, header.N, file );
			}
		}
		fclose( file );
		// finalize new sprite
		int3 maxSize = make_int3( 0 );
		const uint offset = 1; // palShift ? 1 : 0;
		for (int f = 0; f < frameCount; f++)
		{
			// efficient sprite rendering: B&H'21
			SpriteFrame* frame = newSprite->frame[f];
			vector<uint> drawList;
			vector<PAYLOAD> voxel;
			for (int i = 0, z = 0; z < frame->size.z; z++)
			{
				for (int y = 0; y < frame->size.y; y++)
				{
					for (int x = 0; x < frame->size.x; x++, i++)
					{
						if (frame->buffer[i])
						{
							const uint c = palette[frame->buffer[i] - offset];
						#if PAYLOADSIZE == 1
							const uint blue = min( 3u, (((c >> 16) & 255) + 31) >> 6 );
							const uint green = min( 7u, (((c >> 8) & 255) + 15) >> 5 );
							const uint red = min( 7u, ((c & 255) + 15) >> 5 );
							const uint p = (red << 5) + (green << 2) + blue;
							const PAYLOAD v = (p == 0) ? (1 << 5) : p;
						#else
							const uint blue = min( 15u, (((c >> 16) & 255) + 7) >> 4 );
							const uint green = min( 15u, (((c >> 8) & 255) + 7) >> 4 );
							const uint red = min( 15u, ((c & 255) + 7) >> 4 );
							const uint p = (red << 8) + (green << 4) + blue;
							const PAYLOAD v = (p == 0) ? 1 : p;
						#endif
							frame->buffer[i] = v;
							drawList.push_back( x + (y << 10) + (z << 20) );
							voxel.push_back( frame->buffer[i] );
						}
					}
				}
			}
			maxSize.x = max( maxSize.x, frame->size.x );
			maxSize.y = max( maxSize.y, frame->size.y );
			maxSize.z = max( maxSize.z, frame->size.z );
			frame->drawPos = (uint*)_aligned_malloc( drawList.size() * sizeof( uint ), 64 );
			frame->drawVal = (PAYLOAD*)_aligned_malloc( drawList.size() * PAYLOADSIZE, 64 );
			for (size_t s = drawList.size(), i = 0; i < s; i++)
				frame->drawPos[i] = drawList[i],
				frame->drawVal[i] = voxel[i];
			frame->drawListSize = (uint)drawList.size();
		}
		// create the backup frame for sprite movement
		SpriteFrame* backupFrame = new SpriteFrame();
		backupFrame->size = maxSize;
		backupFrame->buffer = new PAYLOAD[maxSize.x * maxSize.y * maxSize.z];
		newSprite->backup = backupFrame;
		sprite.push_back( newSprite );
	}
	// all done, return sprite index
	return (uint)sprite.size() - 1;
}

// SpriteManager::SaveSprite
// ----------------------------------------------------------------------------
void SpriteManager::SaveSprite( const uint idx, const char* vxFile )
{
	if (idx >= sprite.size()) return;
	// save the sprite to custom file format
	Sprite* s = sprite[idx];
	gzFile f = gzopen( vxFile, "wb" );
	int frames = (int)s->frame.size(), pls = PAYLOADSIZE;
	gzwrite( f, &pls, 4 );
	gzwrite( f, &frames, 4 );
	for (int i = 0; i < frames; i++)
	{
		SpriteFrame* sf = s->frame[i];
		gzwrite( f, &sf->size, 12 );
		gzwrite( f, &sf->drawListSize, 4 );
		gzwrite( f, sf->drawPos, sf->drawListSize * sizeof( uint ) );
		gzwrite( f, sf->drawVal, sf->drawListSize * PAYLOADSIZE );
	}
	gzclose( f );
}

// SpriteManager::CloneSprite
// ----------------------------------------------------------------------------
uint SpriteManager::CloneSprite( const uint idx )
{
	if (idx >= sprite.size()) return 0;
	// clone frame data, wich contain pointers to shared frame data
	Sprite* newSprite = new Sprite();
	newSprite->frame = sprite[idx]->frame;
	newSprite->hasShadow = sprite[idx]->hasShadow;
	// clone backup frame, which will be unique per instance
	SpriteFrame* backupFrame = new SpriteFrame();
	backupFrame->size = sprite[idx]->backup->size;
	backupFrame->buffer = new PAYLOAD[backupFrame->size.x * backupFrame->size.y * backupFrame->size.z];
	newSprite->backup = backupFrame;
	sprite.push_back( newSprite );
	return (uint)sprite.size() - 1;
}

// World::CreateSprite
// ----------------------------------------------------------------------------
uint World::CreateSprite( const int3 pos, const int3 size, const int frames )
{
	// create a sprite from voxel information the world
	Sprite* newSprite = new Sprite();
	uint voxelsPerFrame = size.x * size.y * size.z;
	for (int i = 0; i < frames; i++)
	{
		SpriteFrame* frame = new SpriteFrame();
		frame->size = size;
		vector<uint> drawList( voxelsPerFrame / 4 /* estimate */ );
		vector<PAYLOAD> voxel( voxelsPerFrame / 4 );
		frame->buffer = (PAYLOAD*)_aligned_malloc( voxelsPerFrame * PAYLOADSIZE, 64 );
		for (int x = 0; x < size.x; x++) for (int y = 0; y < size.y; y++) for (int z = 0; z < size.z; z++)
		{
			uint v = Get( i * size.x + x + pos.x, y + pos.y, z + pos.z );
			frame->buffer[x + y * size.x + z * size.x * size.y] = v;
			if (v != 0)
			{
				drawList.push_back( x + (y << 10) + (z << 20) );
				voxel.push_back( v );
			}
		}
		frame->drawListSize = (uint)drawList.size();
		frame->drawPos = (uint*)_aligned_malloc( drawList.size() * sizeof( uint ), 64 );
		frame->drawVal = (PAYLOAD*)_aligned_malloc( drawList.size() * PAYLOADSIZE, 64 );
		for (size_t s = drawList.size(), i = 0; i < s; i++)
			frame->drawPos[i] = drawList[i],
			frame->drawVal[i] = voxel[i];
		newSprite->frame.push_back( frame );
	}
	// create the backup frame for sprite movement
	SpriteFrame* backupFrame = new SpriteFrame();
	backupFrame->size = size;
	backupFrame->buffer = new PAYLOAD[voxelsPerFrame];
	newSprite->backup = backupFrame;
	auto& sprite = GetSpriteList();
	sprite.push_back( newSprite );
	// all done, return sprite index
	return (uint)sprite.size() - 1;
}

// World::SpriteFrameCount
// ----------------------------------------------------------------------------
uint World::SpriteFrameCount( const uint idx )
{
	auto& sprite = GetSpriteList();
	// out of bounds checks
	if (idx >= sprite.size()) return 0;
	// return frame count
	return (uint)sprite[idx]->frame.size();
}

// World::EraseSprite (private; called from World::Commit)
// ----------------------------------------------------------------------------
void World::EraseSprite( const uint idx )
{
	// restore pixels occupied by sprite at previous location
	auto& sprite = GetSpriteList();
	const int3 lastPos = sprite[idx]->lastPos;
	if (lastPos.x == OUTOFRANGE) return;
	const SpriteFrame* backup = sprite[idx]->backup;
	const SpriteFrame* frame = sprite[idx]->frame[sprite[idx]->currFrame];
	const uint* localPos = frame->drawPos;
	for (uint i = 0; i < frame->drawListSize; i++)
	{
		const uint v = localPos[i];
		const uint vx = v & 1023, vy = (v >> 10) & 1023, vz = (v >> 20) & 1023;
		Set( vx + lastPos.x, vy + lastPos.y, vz + lastPos.z, backup->buffer[i] );
	}
}

// World::DrawSprite (private; called from World::Commit)
// ----------------------------------------------------------------------------
void World::DrawSprite( const uint idx )
{
	// draw sprite at new location
	auto& sprite = GetSpriteList();
	const int3& pos = sprite[idx]->currPos;
	if (pos.x != OUTOFRANGE)
	{
		mat4 M = sprite[idx]->transform;
		if (M.cell[0] == 1 && M.cell[5] == 1 && M.cell[10] == 1)
		{
			// no rotation / scaling; use regular rendering code
			const SpriteFrame* frame = sprite[idx]->frame[sprite[idx]->currFrame];
			SpriteFrame* backup = sprite[idx]->backup;
			const uint* localPos = frame->drawPos;
			const PAYLOAD* val = frame->drawVal;
			for (uint i = 0; i < frame->drawListSize; i++)
			{
				const uint v = localPos[i];
				const uint vx = v & 1023, vy = (v >> 10) & 1023, vz = (v >> 20) & 1023;
				backup->buffer[i] = Get( vx + pos.x, vy + pos.y, vz + pos.z );
				Set( vx + pos.x, vy + pos.y, vz + pos.z, val[i] );
			}
		}
		else
		{
			// draw sprite with specified transform
			const SpriteFrame* frame = sprite[idx]->frame[sprite[idx]->currFrame];
			float a = frame->size.x * 0.5f, b = frame->size.y * 0.5f, c = frame->size.z * 0.5f;
			float3 p[8] = {
				M.TransformVector( make_float3( -a, -b, -c ) ), M.TransformVector( make_float3( a, -b, -c ) ),
				M.TransformVector( make_float3( a, b, -c ) ), M.TransformVector( make_float3( -a, b, -c ) ),
				M.TransformVector( make_float3( -a, -b, c ) ), M.TransformVector( make_float3( a, -b, c ) ),
				M.TransformVector( make_float3( a, b, c ) ), M.TransformVector( make_float3( -a, b, c ) )
			};
			aabb bounds;
			for (int i = 0; i < 8; i++) bounds.Grow( p[i] );
			int3 A = make_int3( bounds.bmin3 );
			int3 B = make_int3( bounds.bmax3 );
			mat4 iM = M.Inverted();
			for (int z = A.z; z < B.z; z++) for (int y = A.y; y < B.y; y++) for (int x = A.x; x < B.x; x++)
			{
				// TODO
			}
		}
	}
	// store this location so we can remove the sprite later
	sprite[idx]->lastPos = sprite[idx]->currPos;
}

// World::DrawSpriteShadow
// ----------------------------------------------------------------------------
void World::DrawSpriteShadow( const uint idx )
{
	auto& sprite = GetSpriteList();
	const int3& pos = sprite[idx]->currPos;
	int x0 = pos.x - 12, x1 = pos.x + 12;
	int z0 = pos.z - 12, z1 = pos.z + 12;
	sprite[idx]->shadowVoxels = 0;
	if (!sprite[idx]->preShadow) sprite[idx]->preShadow = new uint4[1024];
	for (int z = z0; z < z1; z++) for (int x = x0; x < x1; x++)
	{
		// shadow outline
		int d2 = (z - pos.z) * (z - pos.z) * 3 + (x - pos.x) * (x - pos.x) * 2;
		if (d2 < 360)
		{
			// shadow intensity
			int i = (d2 / 3) + 150 + 16 * ((x + z) & 1);
			// find height
			for (int v, y = pos.y; y >= 0; y--) if (v = Read( x, y, z ))
			{
				sprite[idx]->preShadow[sprite[idx]->shadowVoxels++] = make_uint4( x, y, z, v );
				int r = (((v >> 5) & 7) * i) >> 8;
				int g = (((v >> 2) & 7) * i) >> 8;
				int b = ((v & 3) * i) >> 8;
				Plot( x, y, z, max( 1, (r << 5) + (g << 2) + b ) );
				break;
			}
		}
	}
}

// World::RemoveSpriteShadow
// ----------------------------------------------------------------------------
void World::RemoveSpriteShadow( const uint idx )
{
	// restore voxels affected by shadow
	auto& sprite = GetSpriteList();
	for (uint i = 0; i < sprite[idx]->shadowVoxels; i++)
	{
		uint4 s = sprite[idx]->preShadow[i];
		Plot( s.x, s.y, s.z, s.w );
	}
}

// World::MoveSpriteTo
// ----------------------------------------------------------------------------
void World::MoveSpriteTo( const uint idx, const uint x, const uint y, const uint z )
{
	// out of bounds checks
	auto& sprite = GetSpriteList();
	if (idx >= sprite.size()) return;
	// set new sprite location
	sprite[idx]->currPos = make_int3( x, y, z );
}

// World::TransformSprite
// ----------------------------------------------------------------------------
void World::TransformSprite( const uint idx, mat4 transform )
{
	// out of bounds checks
	auto& sprite = GetSpriteList();
	if (idx >= sprite.size()) return;
	// set new sprite transform
	sprite[idx]->transform = transform;
}

// World::RemoveSprite
// ----------------------------------------------------------------------------
void World::RemoveSprite( const uint idx )
{
	// move sprite to OUTOFRANGE (-99999) to keep it from taking CPU cycles
	auto& sprite = GetSpriteList();
	sprite[idx]->currPos.x = OUTOFRANGE;
}

// World::StampSpriteTo
// ----------------------------------------------------------------------------
void World::StampSpriteTo( const uint idx, const uint x, const uint y, const uint z )
{
	// out of bounds checks
	auto& sprite = GetSpriteList();
	if (idx >= sprite.size()) return;
	// stamp sprite frame to specified location
	const int3& pos = make_int3( x, y, z );
	if (pos.x == OUTOFRANGE) return;
	const SpriteFrame* frame = sprite[idx]->frame[sprite[idx]->currFrame];
	const uint* localPos = frame->drawPos;
	const PAYLOAD* val = frame->drawVal;
	for (uint i = 0; i < frame->drawListSize; i++)
	{
		const uint v = localPos[i];
		const uint vx = v & 1023, vy = (v >> 10) & 1023, vz = (v >> 20) & 1023;
		Set( vx + pos.x, vy + pos.y, vz + pos.z, val[i] );
	}
}

// World::SetSpriteFrame
// ----------------------------------------------------------------------------
void World::SetSpriteFrame( const uint idx, const uint frame )
{
	auto& sprite = GetSpriteList();
	if (idx >= sprite.size()) return;
	if (frame >= sprite[idx]->frame.size()) return;
	sprite[idx]->currFrame = frame;
}

// World::SpriteHit
// ----------------------------------------------------------------------------
bool World::SpriteHit( const uint A, const uint B )
{
	// get the bounding boxes for the two sprites
	auto& sprite = GetSpriteList();
	int3 b1A = sprite[A]->currPos, b1B = sprite[B]->currPos;
	if (b1A.x == OUTOFRANGE || b1B.x == OUTOFRANGE) return false; // early out: at least one is inactive
	SpriteFrame* frameA = sprite[A]->frame[sprite[A]->currFrame];
	SpriteFrame* frameB = sprite[B]->frame[sprite[B]->currFrame];
	int3 b2A = b1A + frameA->size, b2B = b1B + frameB->size;
	// get the intersection of the bounds
	int3 b1 = make_int3( max( b1A.x, b1B.x ), max( b1A.y, b1B.y ), max( b1A.z, b1B.z ) );
	int3 b2 = make_int3( min( b2A.x, b2B.x ), min( b2A.y, b2B.y ), min( b2A.z, b2B.z ) );
	if (b1.x > b2.x || b1.y > b2.y || b1.z > b2.z) return false;
	// check individual voxels
	int3 pA = b1 - b1A, pB = b1 - b1B;
	int3 sizeA = frameA->size, sizeB = frameB->size, sizeI = b2 - b1;
	for (int z = 0; z < sizeI.z; z++)
	{
		PAYLOAD* dA = frameA->buffer + pA.x + pA.y * sizeA.x + (pA.z + z) * sizeA.x * sizeA.y;
		PAYLOAD* dB = frameB->buffer + pB.x + pB.y * sizeB.x + (pB.z + z) * sizeB.x * sizeB.y;
		for (int y = 0; y < sizeI.y; y++)
		{
			for (int x = 0; x < sizeI.x; x++) if (dA[x] * dB[x]) return true;
			dA += sizeA.x, dB += sizeB.x;
		}
	}
	return false;
}

// ParticlesManager::CreateParticles
// ----------------------------------------------------------------------------
uint ParticlesManager::CreateParticles( const uint count )
{
	Particles* p = new Particles( count );
	particles.push_back( p );
	return (uint)(particles.size() - 1);
}

// World::SetParticle
// ----------------------------------------------------------------------------
void World::SetParticle( const uint set, const uint idx, const uint3 pos, const uint v )
{
	auto& particles = GetParticlesList();
	particles[set]->voxel[idx] = make_uint4( pos, v );
}

// World::DrawParticles
// ----------------------------------------------------------------------------
void World::DrawParticles( const uint set )
{
	auto& particles = GetParticlesList();
	if (particles[set]->count == 0 || particles[set]->voxel[0].x == OUTOFRANGE) return; // inactive
	for (uint s = particles[set]->count, i = 0; i < s; i++)
	{
		const uint4 v = particles[set]->voxel[i];
		particles[set]->backup[i] = make_uint4( v.x, v.y, v.z, Get( v.x, v.y, v.z ) );
		Set( v.x, v.y, v.z, v.w );
	}
}

// World::EraseParticles
// ----------------------------------------------------------------------------
void World::EraseParticles( const uint set )
{
	auto& particles = GetParticlesList();
	for (int s = (int)particles[set]->count, i = s - 1; i >= 0; i--)
	{
		const uint4 v = particles[set]->backup[i];
		Set( v.x, v.y, v.z, v.w );
	}
}

// TileManager::LoadTile
// ----------------------------------------------------------------------------
uint TileManager::LoadTile( const char* voxFile )
{
	tile.push_back( new Tile( voxFile ) );
	return (uint)tile.size() - 1;
}

// World::DrawTile / DrawTileVoxels
// ----------------------------------------------------------------------------
void World::DrawTile( const uint idx, const uint x, const uint y, const uint z )
{
	auto& tile = GetTileList();
	if (x >= GRIDWIDTH || y >= GRIDHEIGHT || z > GRIDDEPTH) return;
	const uint cellIdx = x + z * GRIDWIDTH + y * GRIDWIDTH * GRIDDEPTH;
	DrawTileVoxels( cellIdx, tile[idx]->voxels, tile[idx]->zeroes );
}
void World::DrawTileVoxels( const uint cellIdx, const PAYLOAD* voxels, const uint zeroes )
{
	const uint g = grid[cellIdx];
	uint brickIdx;
	if ((g & 1) == 1) brickIdx = g >> 1; else brickIdx = NewBrick(), grid[cellIdx] = (brickIdx << 1) | 1;
	// copy tile data to brick
	memcpy( brick + brickIdx * BRICKSIZE, voxels, BRICKSIZE * PAYLOADSIZE );
	Mark( brickIdx );
	brickInfo[brickIdx].zeroes = zeroes;
}

// World::DrawTiles
// ----------------------------------------------------------------------------
void World::DrawTiles( const char* tileString, const uint x, const uint y, const uint z )
{
	for (uint s = (uint)strlen( tileString ), i = 0; i < s; i++)
	{
		const char t = tileString[i];
		DrawTile( tileString[i] - '0', x + i, y, z );
	}
}

// TileManager::LoadBigTile
// ----------------------------------------------------------------------------
uint TileManager::LoadBigTile( const char* voxFile )
{
	bigTile.push_back( new BigTile( voxFile ) );
	return (uint)bigTile.size() - 1;
}

// World::DrawBigTile
// ----------------------------------------------------------------------------
void World::DrawBigTile( const uint idx, const uint x, const uint y, const uint z )
{
	auto& bigTile = GetBigTileList();
	if (x >= GRIDWIDTH / 2 || y >= GRIDHEIGHT / 2 || z > GRIDDEPTH / 2) return;
	const uint cellIdx = x * 2 + z * 2 * GRIDWIDTH + y * 2 * GRIDWIDTH * GRIDDEPTH;
	DrawTileVoxels( cellIdx, bigTile[idx]->tile[0].voxels, bigTile[idx]->tile[0].zeroes );
	DrawTileVoxels( cellIdx + 1, bigTile[idx]->tile[1].voxels, bigTile[idx]->tile[1].zeroes );
	DrawTileVoxels( cellIdx + GRIDWIDTH * GRIDDEPTH, bigTile[idx]->tile[2].voxels, bigTile[idx]->tile[2].zeroes );
	DrawTileVoxels( cellIdx + GRIDWIDTH * GRIDDEPTH + 1, bigTile[idx]->tile[3].voxels, bigTile[idx]->tile[3].zeroes );
	DrawTileVoxels( cellIdx + GRIDWIDTH, bigTile[idx]->tile[4].voxels, bigTile[idx]->tile[4].zeroes );
	DrawTileVoxels( cellIdx + GRIDWIDTH + 1, bigTile[idx]->tile[5].voxels, bigTile[idx]->tile[5].zeroes );
	DrawTileVoxels( cellIdx + GRIDWIDTH + GRIDWIDTH * GRIDDEPTH, bigTile[idx]->tile[6].voxels, bigTile[idx]->tile[6].zeroes );
	DrawTileVoxels( cellIdx + GRIDWIDTH + GRIDWIDTH * GRIDDEPTH + 1, bigTile[idx]->tile[7].voxels, bigTile[idx]->tile[7].zeroes );
}

// World::DrawBigTiles
// ----------------------------------------------------------------------------
void World::DrawBigTiles( const char* tileString, const uint x, const uint y, const uint z )
{
	for (uint s = (uint)strlen( tileString ), i = 0; i < s; i++)
	{
		const char t = tileString[i];
		if (t != ' ') DrawBigTile( tileString[i] - '0', x + i, y, z );
	}
}

// World::TraceRay
// ----------------------------------------------------------------------------
float4 FixZeroDeltas( float4 V )
{
	if (fabs( V.x ) < 1e-8f) V.x = V.x < 0 ? -1e-8f : 1e-8f;
	if (fabs( V.y ) < 1e-8f) V.y = V.y < 0 ? -1e-8f : 1e-8f;
	if (fabs( V.z ) < 1e-8f) V.z = V.z < 0 ? -1e-8f : 1e-8f;
	return V;
}
uint World::TraceRay( float4 A, const float4 B, float& dist, float3& N, int steps )
{
	const float4 V = FixZeroDeltas( B ), rV = make_float4( 1 / V.x, 1 / V.y, 1 / V.z, 1 );
	const bool originOutsideGrid = A.x < 0 || A.y < 0 || A.z < 0 || A.x > MAPWIDTH || A.y > MAPHEIGHT || A.z > MAPDEPTH;
	float to = 0; // distance to travel to get into grid
	if (steps == 999999 && originOutsideGrid)
	{
		// use slab test to clip ray origin against scene AABB
		const float tx1 = -A.x * rV.x, tx2 = (MAPWIDTH - A.x) * rV.x;
		float tmin = min( tx1, tx2 ), tmax = max( tx1, tx2 );
		const float ty1 = -A.y * rV.y, ty2 = (MAPHEIGHT - A.y) * rV.y;
		tmin = max( tmin, min( ty1, ty2 ) ), tmax = min( tmax, max( ty1, ty2 ) );
		const float tz1 = -A.z * rV.z, tz2 = (MAPDEPTH - A.z) * rV.z;
		tmin = max( tmin, min( tz1, tz2 ) ), tmax = min( tmax, max( tz1, tz2 ) );
		if (tmax < tmin || tmax <= 0) return 0; /* ray misses scene */ else A += tmin * V; // new ray entry point
		to = tmin;
	}
	uint tp = (clamp( (uint)A.x >> 3, 0u, 127u ) << 20) + (clamp( (uint)A.y >> 3, 0u, 127u ) << 10) +
		clamp( (uint)A.z >> 3, 0u, 127u );
	const int bits = SELECT( 4, 34, V.x > 0 ) + SELECT( 3072, 10752, V.y > 0 ) + SELECT( 1310720, 3276800, V.z > 0 ); // magic
	float4 tm = (make_float4( (float)(((tp >> 20) & 127) + ((bits >> 5) & 1)), (float)(((tp >> 10) & 127) + ((bits >> 13) & 1)),
		(float)((tp & 127) + ((bits >> 21) & 1)), 0 ) - A * 0.125f) * rV;
	float t = 0;
	const float4 td = make_float4( (float)DIR_X, (float)DIR_Y, (float)DIR_Z, 0 ) * rV;
	uint last = 0;
	do
	{
		// fetch brick from top grid
		uint o = grid[(tp >> 20) + ((tp & 127) << 7) + (((tp >> 10) & 127) << 14)];
		if (!--steps) break;
		if (o != 0) if ((o & 1) == 0) /* solid */
		{
			dist = (t + to) * 8.0f;
			N = make_float3( (float)((last == 0) * DIR_X), (float)((last == 1) * DIR_Y), (float)((last == 2) * DIR_Z) ) * -1.0f;
			return o >> 1;
		}
		else // brick
		{
			// backup top-grid traversal state
			const float4 tm_ = tm;
			// intialize brick traversal
			tm = A + V * (t *= 8); // abusing tm for I to save registers
			uint p = (clamp( (uint)tm.x, tp >> 17, (tp >> 17) + 7 ) << 20) +
				(clamp( (uint)tm.y, (tp >> 7) & 1023, ((tp >> 7) & 1023) + 7 ) << 10) +
				clamp( (uint)tm.z, (tp << 3) & 1023, ((tp << 3) & 1023) + 7 ), lp = ~1;
			tm = (make_float4( (float)((p >> 20) + OFFS_X), (float)(((p >> 10) & 1023) + OFFS_Y), (float)((p & 1023) + OFFS_Z), 0 ) - A) * rV;
			p &= 7 + (7 << 10) + (7 << 20), o = (o >> 1) * BRICKSIZE;
			do // traverse brick
			{
				const uint v = brick[o + (p >> 20) + ((p >> 7) & (BMSK * BRICKDIM)) + (p & BMSK) * BDIM2];
				if (v)
				{
					dist = t + to;
					N = make_float3( (float)((last == 0) * DIR_X), (float)((last == 1) * DIR_Y), (float)((last == 2) * DIR_Z) ) * -1.0f;
					return v;
				}
				t = min( tm.x, min( tm.y, tm.z ) );
				if (t == tm.x) tm.x += td.x, p += DIR_X << 20, last = 0;
				else if (t == tm.y) tm.y += td.y, p += ((bits << 2) & 3072) - 1024, last = 1;
				else if (t == tm.z) tm.z += td.z, p += DIR_Z, last = 2;
			} while (!(p & TOPMASK3));
			tm = tm_; // restore top-grid traversal state
		}
		t = min( tm.x, min( tm.y, tm.z ) );
		if (t == tm.x) tm.x += td.x, tp += DIR_X << 20, last = 0;
		else if (t == tm.y) tm.y += td.y, tp += DIR_Y << 10, last = 1;
		else if (t == tm.z) tm.z += td.z, tp += DIR_Z, last = 2;
	} while (!(tp & 0xf80e0380));
	return 0U;
}
void World::TraceRayToVoid( float4 A, const float4 B, float& dist, float3& N )
{
	// find the first empty voxel
	const float4 V = FixZeroDeltas( B ), rV = make_float4( 1 / V.x, 1 / V.y, 1 / V.z, 1 );
	if (A.x < 0 || A.y < 0 || A.z < 0 || A.x > MAPWIDTH || A.y > MAPHEIGHT || A.z > MAPDEPTH)
	{
		dist = 0; // we start outside the grid, and thus in empty space: don't do that
		return;
	}
	uint tp = (clamp( (uint)A.x >> 3, 0u, 127u ) << 20) + (clamp( (uint)A.y >> 3, 0u, 127u ) << 10) +
		clamp( (uint)A.z >> 3, 0u, 127u );
	const int bits = SELECT( 4, 34, V.x > 0 ) + SELECT( 3072, 10752, V.y > 0 ) + SELECT( 1310720, 3276800, V.z > 0 ); // magic
	float4 tm = (make_float4( (float)(((tp >> 20) & 127) + ((bits >> 5) & 1)), (float)(((tp >> 10) & 127) + ((bits >> 13) & 1)),
		(float)((tp & 127) + ((bits >> 21) & 1)), 0 ) - A * 0.125f) * rV;
	float t = 0;
	const float4 td = make_float4( (float)DIR_X, (float)DIR_Y, (float)DIR_Z, 0 ) * rV;
	uint last = 0;
	do
	{
		// fetch brick from top grid
		uint o = grid[(tp >> 20) + ((tp & 127) << 7) + (((tp >> 10) & 127) << 14)];
		if (o == 0) /* empty brick: done */
		{
			dist = t * 8.0f;
			N = make_float3( (float)((last == 0) * DIR_X), (float)((last == 1) * DIR_Y), (float)((last == 2) * DIR_Z) ) * -1.0f;
			return;
		}
		else if ((o & 1) == 1) /* non-empty brick */
		{
			// backup top-grid traversal state
			const float4 tm_ = tm;
			// intialize brick traversal
			tm = A + V * (t *= 8); // abusing tm for I to save registers
			uint p = (clamp( (uint)tm.x, tp >> 17, (tp >> 17) + 7 ) << 20) +
				(clamp( (uint)tm.y, (tp >> 7) & 1023, ((tp >> 7) & 1023) + 7 ) << 10) +
				clamp( (uint)tm.z, (tp << 3) & 1023, ((tp << 3) & 1023) + 7 ), lp = ~1;
			tm = (make_float4( (float)((p >> 20) + OFFS_X), (float)(((p >> 10) & 1023) + OFFS_Y), (float)((p & 1023) + OFFS_Z), 0 ) - A) * rV;
			p &= 7 + (7 << 10) + (7 << 20), o = (o >> 1) * BRICKSIZE;
			do // traverse brick
			{
				if (!brick[o + (p >> 20) + ((p >> 7) & (BMSK * BRICKDIM)) + (p & BMSK) * BDIM2])
				{
					dist = t;
					N = make_float3( (float)((last == 0) * DIR_X), (float)((last == 1) * DIR_Y), (float)((last == 2) * DIR_Z) ) * -1.0f;
					return;
				}
				t = min( tm.x, min( tm.y, tm.z ) );
				if (t == tm.x) tm.x += td.x, p += DIR_X << 20, last = 0;
				else if (t == tm.y) tm.y += td.y, p += ((bits << 2) & 3072) - 1024, last = 1;
				else if (t == tm.z) tm.z += td.z, p += DIR_Z, last = 2;
			} while (!(p & TOPMASK3));
			tm = tm_; // restore top-grid traversal state
		}
		t = min( tm.x, min( tm.y, tm.z ) );
		if (t == tm.x) tm.x += td.x, tp += DIR_X << 20, last = 0;
		else if (t == tm.y) tm.y += td.y, tp += DIR_Y << 10, last = 1;
		else if (t == tm.z) tm.z += td.z, tp += DIR_Z, last = 2;
	} while (!(tp & 0xf80e0380));
}

static Buffer* rayBatchBuffer = 0;
static Buffer* rayBatchResult = 0;

Ray* World::GetBatchBuffer()
{
	if (!rayBatchBuffer)
	{
		uint* hostBuffer = new uint[SCRWIDTH * SCRHEIGHT * sizeof( Ray ) / 4];
		rayBatchBuffer = new Buffer( SCRWIDTH * SCRHEIGHT * sizeof( Ray ) / 4, Buffer::DEFAULT, hostBuffer );
		uint* hostResults = new uint[SCRWIDTH * SCRHEIGHT * sizeof( Intersection ) / 4];
		rayBatchResult = new Buffer( SCRWIDTH * SCRHEIGHT * sizeof( Intersection ) / 4, Buffer::DEFAULT, hostResults );
		// now that we have the buffers, we can pass them to the kernel (just once)
		batchTracer->SetArgument( 6, rayBatchBuffer );
		batchTracer->SetArgument( 7, rayBatchResult );
		batchToVoidTracer->SetArgument( 6, rayBatchBuffer );
		batchToVoidTracer->SetArgument( 7, rayBatchResult );
	#if THIRDLEVEL == 1
		batchTracer->SetArgument( 8, &uberGrid );
		batchToVoidTracer->SetArgument( 8, &uberGrid );
	#endif
	}
	return (Ray*)rayBatchBuffer->hostBuffer;
}

Intersection* World::TraceBatch( const uint batchSize )
{
	// sanity checks
	if (!rayBatchBuffer) FatalError( "TraceBatch: Batch not yet created." );
	if (batchSize > SCRWIDTH * SCRHEIGHT) FatalError( "TraceBatch: batch is too large." );
	if (batchSize > 0)
	{
		// copy the ray batch to the GPU
		rayBatchBuffer->CopyToDevice();
		// invoke ray tracing kernel
		batchTracer->SetArgument( 5, (int)batchSize );
		batchTracer->Run( batchSize );
		// get results back from GPU
		rayBatchResult->CopyFromDevice( true /* blocking */ );
	}
	// return host buffer with ray tracing results
	return (Intersection*)rayBatchResult->hostBuffer;
}

Intersection* World::TraceBatchToVoid( const uint batchSize )
{
	// sanity checks
	if (!rayBatchBuffer) FatalError( "TraceBatchToVoid: Batch not yet created." );
	if (batchSize > SCRWIDTH * SCRHEIGHT) FatalError( "TraceBatchToVoid: batch is too large." );
	if (batchSize > 0)
	{
		// copy the ray batch to the GPU
		rayBatchBuffer->CopyToDevice();
		// invoke ray tracing kernel
		batchToVoidTracer->SetArgument( 5, (int)batchSize );
		batchToVoidTracer->Run( batchSize );
		// get results back from GPU
		rayBatchResult->CopyFromDevice( true /* blocking */ );
	}
	// return host buffer with ray tracing results
	return (Intersection*)rayBatchResult->hostBuffer;
}

/*
Render flow:
1. GLFW application loop in template.cpp calls World::Render:
   - If changes were made to the scene during the previous frame,
	 these will now be committed (moved in vram to their final location).
   - The render kernel is invoked. It will run in the background.
	 The render kernel stores final pixels in an OpenGL texture.
   - Sprites (if any) are removed from the world, so Game::Tick operates as
	 if their voxels do not exist.
2. GLFW application loop calls Game::Tick:
   - Game::Tick executes game logic on the CPU.
   - Via the world object, changes are placed in the commit buffer.
3. GLFW application loop calls World::Commit:
   - Sprites are added back, to be displayed.
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
	// copy scene changes from staging buffer to final destination on GPU
	// Note: even if game->autoRendering is false, we still keep the scene in sync
	// with this mechanism.
	if (copyInFlight)
	{
		if (tasks > 0)
		{
			committer->SetArgument( 0, (int)tasks );
			committer->Run( (tasks + 63) & (65536 - 32), 4, &copyDone, &commitDone );
			commitInFlight = true;
		}
		copyInFlight = false;
	}
	if (Game::autoRendering)
	{
		// backup previous frame data
		float3 prevE = params.E;
		float3 prevP0 = params.p0;
		float3 prevP1 = params.p1;
		float3 prevP2 = params.p2;
		float3 prevP3 = prevP1 + (prevP2 - prevP0);
		// prepare for rendering
		const mat4 M = camMat;
		const float aspectRatio = (float)SCRWIDTH / SCRHEIGHT;
		params.E = TransformPosition( make_float3( 0 ), M );
		params.oneOverRes = make_float2( 1.0f / SCRWIDTH, 1.0f / SCRHEIGHT );
		params.p0 = TransformPosition( make_float3( aspectRatio, 1, 2.2f ), M );
		params.p1 = TransformPosition( make_float3( -aspectRatio, 1, 2.2f ), M );
		params.p2 = TransformPosition( make_float3( aspectRatio, -1, 2.2f ), M );
		// reprojection data
		static bool firstFrame = true;
		if (firstFrame)
		{
			prevE = params.E, prevP0 = params.p0;
			prevP1 = params.p1, prevP2 = params.p2;
			firstFrame = false;
		}
		params.Nleft = make_float4( normalize( cross( prevP0 - prevE, prevP2 - prevE ) ), 0 );
		params.Nleft.w = dot( prevE, make_float3( params.Nleft ) );
		params.Nright = make_float4( normalize( cross( prevP3 - prevE, prevP1 - prevE ) ), 0 );
		params.Nright.w = dot( prevE, make_float3( params.Nright ) );
		params.Ntop = make_float4( normalize( cross( prevP0 - prevE, prevP1 - prevE ) ), 0 );
		params.Ntop.w = dot( prevE, make_float3( params.Ntop ) );
		params.Nbottom = make_float4( normalize( cross( prevP3 - prevE, prevP2 - prevE ) ), 0 );
		params.Nbottom.w = dot( prevE, make_float3( params.Nbottom ) );
		params.prevRight = make_float4( normalize( prevP1 - prevP0 ), 0 );
		params.prevDown = make_float4( normalize( prevP2 - prevP0 ), 0 );
		params.prevP0 = make_float4( prevP0, 0 );
		params.prevP1 = make_float4( prevP1, 0 );
		params.prevP2 = make_float4( prevP2, 0 );
		params.prevP3 = make_float4( prevP3, 0 );
		// finalize params
		params.R0 = RandomUInt();
		params.skyWidth = skySize.x;
		params.skyHeight = skySize.y;
		static uint frame = 0;
		params.frame = frame++ & 255;
		for (int i = 0; i < 6; i++) params.skyLight[i] = skyLight[i];
		params.skyLightScale = Game::skyDomeLightScale;
		// get render parameters to GPU and invoke kernel asynchronously
		paramBuffer->CopyToDevice( false );
		if (!screen)
		{
			screen = new Buffer( targetTextureID, Buffer::TARGET );
			renderer->SetArgument( 0, screen );
			renderer->SetArgument( 1, paramBuffer );
			renderer->SetArgument( 2, tmpFrame );
			renderer->SetArgument( 3, &gridMap );
			renderer->SetArgument( 4, brickBuffer[0] );
			renderer->SetArgument( 5, brickBuffer[1] );
			renderer->SetArgument( 6, brickBuffer[2] );
			renderer->SetArgument( 7, brickBuffer[3] );
			renderer->SetArgument( 8, sky );
			renderer->SetArgument( 9, blueNoise );
		#if THIRDLEVEL
			renderer->SetArgument( 10, &uberGrid );
		#endif
		}
		static int histIn = 0, histOut = 1;
	#if TAA == 0
		renderer->Run( screen, make_int2( 8, 16 ), 0, &renderDone );
	#else
		renderer->Run( screen, make_int2( 8, 16 ) );
		// renderer->Run2D( make_int2( SCRWIDTH, SCRHEIGHT ), make_int2( 8, 16 ) );
		finalizer->SetArgument( 0, history[histIn] );
		finalizer->SetArgument( 1, history[histOut] );
		finalizer->SetArgument( 2, tmpFrame );
		finalizer->SetArgument( 3, paramBuffer );
		finalizer->Run2D( make_int2( SCRWIDTH, SCRHEIGHT ), make_int2( 8, 16 ) );
		unsharpen->SetArgument( 0, screen );
		unsharpen->SetArgument( 1, history[histOut] );
		unsharpen->Run( screen, make_int2( 8, 16 ), 0, &renderDone );
		// swap
		swap( histIn, histOut );
	#endif
	}
}

// World::Commit
// ----------------------------------------------------------------------------
void World::Commit()
{
	// add the sprites and particles to the world
	auto& sprite = GetSpriteList();
	for (int s = (int)sprite.size(), i = 0; i < s; i++)
	{
		if (sprite[i]->hasShadow) DrawSpriteShadow( i );
		DrawSprite( i );
	}
	auto& particles = GetParticlesList();
	for (int s = (int)particles.size(), i = 0; i < s; i++) DrawParticles( i );
	// make sure the previous commit completed
	if (commitInFlight)
	{
		clWaitForEvents( 1, &commitDone );
		commitInFlight = false;
	}
	// replace the initial commit buffer by a double-sized buffer in pinned memory
	static uint* pinnedMemPtr = 0;
	if (pinnedMemPtr == 0)
	{
		const uint pinnedSize = commitSize + gridSize;
		cl_mem pinned = clCreateBuffer( Kernel::GetContext(), CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, commitSize * 2, 0, 0 );
		pinnedMemPtr = (uint*)clEnqueueMapBuffer( Kernel::GetQueue(), pinned, 1, CL_MAP_WRITE, 0, pinnedSize, 0, 0, 0, 0 );
		StreamCopy( (__m256i*)(pinnedMemPtr + commitSize / 4), (__m256i*)grid, gridSize );
		grid = pinnedMemPtr + commitSize / 4; // top-level grid resides at the start of the commit buffer
	}
	// gather changed bricks
	tasks = 0;
	uint* brickIndices = pinnedMemPtr + gridSize / 4;
	uchar* changedBricks = (uchar*)(brickIndices + MAXCOMMITS);
	for (uint j = 0; j < BRICKCOUNT / 32; j++) if (IsDirty32( j ) /* if not dirty: skip 32 bits at once */)
	{
		for (uint k = 0; k < 32; k++)
		{
			const uint i = j * 32 + k;
			if (!IsDirty( i )) continue;
			*brickIndices++ = i; // store index of modified brick at start of commit buffer
			StreamCopy( (__m256i*)changedBricks, (__m256i*)(brick + i * BRICKSIZE), BRICKSIZE * PAYLOADSIZE );
			changedBricks += BRICKSIZE * PAYLOADSIZE, tasks++;
		}
		ClearMarks32( j );
		if (tasks + 32 >= MAXCOMMITS) break; // we have too many commits; postpone
	}
	// asynchroneously copy the CPU data to the GPU via the commit buffer
	if (tasks > 0 || firstFrame)
	{
		// copy top-level grid to start of pinned buffer in preparation of final transfer
		StreamCopyMT( (__m256i*)pinnedMemPtr, (__m256i*)grid, gridSize );
		// enqueue (on queue 2) memcopy of pinned buffer to staging buffer on GPU
		const uint copySize = firstFrame ? commitSize : (gridSize + MAXCOMMITS * 4 + tasks * BRICKSIZE * PAYLOADSIZE);
		clEnqueueWriteBuffer( Kernel::GetQueue2(), devmem, 0, 0, copySize, pinnedMemPtr, 0, 0, 0 );
	#if CELLSKIPPING == 1
		// find cells surrounded by empty cells
		const size_t ws = 128 * 128 * 128;
		const size_t ls = 128;
		clEnqueueNDRangeKernel( Kernel::GetQueue2(), hermitFinder->GetKernel(), 1, 0, &ws, &ls, 0, 0, &hermitDone );
	#if 0
		clWaitForEvents( 1, &hermitDone );
		cl_ulong hermitStart = 0, hermitEnd = 0;
		clGetEventProfilingInfo( hermitDone, CL_PROFILING_COMMAND_START, sizeof( cl_ulong ), &hermitStart, 0 );
		clGetEventProfilingInfo( hermitDone, CL_PROFILING_COMMAND_END, sizeof( cl_ulong ), &hermitEnd, 0 );
		unsigned long duration = (unsigned long)(hermitEnd - hermitStart); // in nanoseconds
		printf( "hermits detected in %5.2fms\n", duration / 1000000000.0f );
	#endif
	#endif
	#if THIRDLEVEL == 1
		const size_t ws = UBERWIDTH * UBERHEIGHT * UBERDEPTH;
		const size_t ls = 16;
		clEnqueueNDRangeKernel( Kernel::GetQueue2(), uberGridUpdater->GetKernel(), 1, 0, &ws, &ls, 0, 0, &ubergridDone );
	#if 1
		clWaitForEvents( 1, &ubergridDone );
		cl_ulong uberStart = 0, uberEnd = 0;
		clGetEventProfilingInfo( ubergridDone, CL_PROFILING_COMMAND_START, sizeof( cl_ulong ), &uberStart, 0 );
		clGetEventProfilingInfo( ubergridDone, CL_PROFILING_COMMAND_END, sizeof( cl_ulong ), &uberEnd, 0 );
		unsigned long duration = (unsigned long)(uberEnd - uberStart); // in nanoseconds
		printf( "ubergrid constructed in %10.7fms\n", duration / 1000000.0f );
	#endif
	#endif
	#if GRID_IN_3DIMAGE == 1
		// enqueue (on queue 2) vram-to-vram copy of the top-level grid to a 3D OpenCL image buffer
		size_t origin[3] = { 0, 0, 0 };
		size_t region[3] = { GRIDWIDTH, GRIDDEPTH, GRIDHEIGHT };
		clEnqueueCopyBufferToImage( Kernel::GetQueue2(), devmem, gridMap, 0, origin, region, 0, 0, &copyDone );
	#else
		clEnqueueCopyBuffer( Kernel::GetQueue2(), devmem, gridMap, 0, 0, GRIDWIDTH * GRIDHEIGHT * GRIDDEPTH * 4, 0, 0, &copyDone );
	#endif
		copyInFlight = true;	// next render should wait for this commit to complete
		firstFrame = false;		// next frame is not the first frame
	}
	// bricks and top-level grid have been moved to the final host-side commit buffer; remove sprites and particles
	// NOTE: this must explicitly happen in reverse order.
	for (int s = (int)particles.size(), i = s - 1; i >= 0; i--) EraseParticles( i );
	for (int s = (int)sprite.size(), i = s - 1; i >= 0; i--)
	{
		EraseSprite( i );
		if (sprite[i]->hasShadow) RemoveSpriteShadow( i );
	}
	// at this point, rendering *must* be done; let's make sure
	if (Game::autoRendering)
	{
		clWaitForEvents( 1, &renderDone );
		// profiling: https://stackoverflow.com/questions/23272170/opencl-measure-kernels-time
		cl_ulong renderStart = 0;
		cl_ulong renderEnd = 0;
		clGetEventProfilingInfo( renderDone, CL_PROFILING_COMMAND_START, sizeof( cl_ulong ), &renderStart, 0 );
		clGetEventProfilingInfo( renderDone, CL_PROFILING_COMMAND_END, sizeof( cl_ulong ), &renderEnd, 0 );
		unsigned long duration = (unsigned long)(renderEnd - renderStart); // in nanoseconds
		renderTime = duration / 1000000000.0f;
	}
}

// World::StreamCopyMT
// ----------------------------------------------------------------------------
#define COPYTHREADS	4
void World::StreamCopyMT( __m256i* dst, __m256i* src, const uint bytes )
{
	// fast copying of large 32-byte aligned / multiple of 32 sized data blocks:
	// streaming __m256 read/writes, on multiple threads
	assert( (bytes & 31) == 0 );
	int N = (int)(bytes / 32);
	static JobManager* jm = JobManager::GetJobManager();
	static CopyJob cj[COPYTHREADS];
	__m256i* s = src;
	__m256i* d = dst;
	for (int i = 0; i < (COPYTHREADS - 1); i++, s += N / COPYTHREADS, d += N / COPYTHREADS)
	{
		cj[i].dst = d, cj[i].src = s, cj[i].N = N / COPYTHREADS;
		jm->AddJob2( &cj[i] );
	}
	cj[COPYTHREADS - 1].dst = d;
	cj[COPYTHREADS - 1].src = s;
	cj[COPYTHREADS - 1].N = N - (COPYTHREADS - 1) * (N / COPYTHREADS);
	jm->AddJob2( &cj[COPYTHREADS - 1] );
	jm->RunJobs();
}

// Tile::Tile
// ----------------------------------------------------------------------------
Tile::Tile( const char* voxFile )
{
	// abuse the sprite loader to get the tile in memory
	uint spriteIdx = SpriteManager::GetSpriteManager()->LoadSprite( voxFile );
	Sprite* sprite = SpriteManager::GetSpriteManager()->sprite[spriteIdx];
	// make some assumptions
	if (sprite->frame.size() != 1) FatalError( "LoadTile( \"%s\" ):\nExpected a single frame in the tile file.", voxFile );
	SpriteFrame* frame = sprite->frame[0];
	if (frame->size.x == BRICKDIM * 2) FatalError( "LoadTile( \"%s\" ):\nExpected an 8x8x8 tile; use LoadBigTile for 16x16x16 tiles.", voxFile );
	if (frame->size.x != BRICKDIM) FatalError( "LoadTile( \"%s\" ):\nExpected an 8x8x8 tile.", voxFile );
	// convert data
	uint zeroCount = 0;
	for (int i = 0; i < BRICKSIZE; i++)
	{
		PAYLOAD v = frame->buffer[i];
		voxels[i] = v;
		if (v == 0) zeroCount++;
	}
	zeroes = zeroCount;
	// remove the sprite from the world
	SpriteManager::GetSpriteManager()->sprite.pop_back();
}

// BigTile::BigTile
// ----------------------------------------------------------------------------
BigTile::BigTile( const char* voxFile )
{
	// abuse the sprite loader to get the tile in memory
	uint spriteIdx = SpriteManager::GetSpriteManager()->LoadSprite( voxFile );
	Sprite* sprite = SpriteManager::GetSpriteManager()->sprite[spriteIdx];
	// make some assumptions
	if (sprite->frame.size() != 1) FatalError( "LoadTile( \"%s\" ):\nExpected a single frame in the tile file.", voxFile );
	SpriteFrame* frame = sprite->frame[0];
	if (frame->size.x == BRICKDIM) FatalError( "LoadTile( \"%s\" ):\nExpected a 16x16x16 tile; use LoadTile for 8x8x8 tiles.", voxFile );
	if (frame->size.x != BRICKDIM * 2) FatalError( "LoadTile( \"%s\" ):\nExpected a 16x16x16 tile.", voxFile );
	// convert data: store as 8 bricks for efficient rendering
	for (int subTile = 0; subTile < 8; subTile++)
	{
		int sx = subTile & 1, sy = (subTile >> 1) & 1, sz = (subTile >> 2) & 1;
		uint zeroCount = 0;
		for (int z = 0; z < BRICKDIM; z++) for (int y = 0; y < BRICKDIM; y++) for (int x = 0; x < BRICKDIM; x++)
		{
			PAYLOAD v = frame->buffer[sx * BRICKDIM + x + (sy * BRICKDIM + y) * BRICKDIM * 2 + (sz * BRICKDIM + z) * 4 * BRICKDIM * BRICKDIM];
			tile[subTile].voxels[x + y * BRICKDIM + z * BRICKDIM * BRICKDIM] = v;
			if (v == 0) zeroCount++;
		}
		tile[subTile].zeroes = zeroCount;
	}
	// remove the sprite from the world
	SpriteManager::GetSpriteManager()->sprite.pop_back();
	delete sprite;
}

// EOF
