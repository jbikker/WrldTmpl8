#pragma once

#define THREADSAFEWORLD 1
#define SQR(x) ((x)*(x))
#define TILESIZE	8
#define TILESIZE2	(TILESIZE * TILESIZE)

namespace Tmpl8
{

struct BrickInfo { uint zeroes; /* , location; */ };

// Sprite system overview:
// The world contains a set of 0 or more sprites, typically loaded from .vox files.
// Sprites act like classic homecomputer sprites: they do not affect the world in
// any way, they just get displayed. Internally, this works as follows:
// 1. Before Game::Tick is executed:
//    - the world gets rendered by the GPU
//    - the sprites are then removed from the world
// 2. Game::Tick is now executed on a world that does not contain the sprites.
// 3. After Game::Tick completes:
//    - each sprite makes a backup of the voxels it overlaps
//    - the sprites are added back to the world
//    - the world is synchronized with the GPU for rendering in step 1.
// Consequence of this system is that even stationary sprites take time to process.

class SpriteFrame
{
	// fast sprite system: B&H'21
public:
	~SpriteFrame() { _aligned_free( buffer ); }
	uchar* buffer = 0;					// full frame buffer (width * height * depth)
	int3 size = make_int3( 0 );			// size of the sprite over x, y and z
	uchar4* drawList;					// compacted list of opaque sprite voxels
	uint drawListSize;					// number of voxels in buffer2
};

class Sprite
{
public:
	vector<SpriteFrame*> frame;			// sprite frames
	SpriteFrame* backup;				// backup of pixels that the sprite overwrote
	int3 lastPos = make_int3( -9999 );	// location where the backup will be restored to
	int3 currPos = make_int3( -9999 );	// location where the sprite will be drawn
	int currFrame = 0;					// frame to draw
	bool hasShadow = false;				// set to true to enable a drop shadow
	uint4* preShadow = 0;				// room for backup of voxels overwritten by shadow
	uint shadowVoxels = 0;				// size of backup voxel array
};

class Particles
{
public:
	Particles( int N )					// constructor
	{
		count = N, voxel = new uint4[N], backup = new uint4[N];
		memset( voxel, 0, N * sizeof( uint4 ) );
		memset( backup, 0, N * sizeof( uint4 ) );
		voxel[0].x = -9999; // inactive by default
	}
	uint4* voxel = 0;					// particle positions & color
	uint4* backup = 0;					// backup of voxels overlapped by particles
	uint count = 0;						// particle count for the set
};

// Tile system overview:
// The top-level grid / brick layout of the world (see below) fits well with the 
// classic concept of tiled graphics. A tile is simply an 8x8x8 or 16x16x16 chunk of 
// voxel data, which can be placed in the world at locations that are a multiple of 8 
// over x, y and z. Drawing a tile will thus simply overwrite the contents of a brick 
// (or 8 bricks, when using the larger 16x16x16 tiles).

class Tile
{
public:
	Tile() = default;
	Tile( const char* voxFile );
	uchar voxels[BRICKSIZE];			// tile voxel data
	uint zeroes;						// number of transparent voxels in the tile
};

class BigTile
{
public:
	BigTile() = default;
	BigTile( const char* voxFile );
	Tile tile[8];						// a big tile is just 2x2x2 tiles stored together
};

// Voxel world data structure:
// The world consists of a 128x128x128 top-level grid. Each cell in this grid can
// either store a solid color, or the index of an 8x8x8 brick. Filling all cells with 
// brick indices yields the maximum world resolution of 1024x1024x01024.
// Voxels are 8-bit values. '0' is an empy voxel; all other colors are opaque. Voxel
// colors are 3-3-2 rgb values. Note that black voxels do not exist in this scheme.
// The data structure is mirrored to the GPU, with a delay of 1 frame (i.e., the GPU
// always renders the previous frame). 
// Furthermore, since the full dataset is a bit over 1GB, only changes are synced.
// the CPU to GPU communication consists of 8MB for the 128x128x128 top-level ints,
// plus up to 8192 changed bricks. If more changes are made per frame, these will
// be postponed to the next frame.

class World
{
public:
	// constructor / destructor
	World( const uint targetID );
	~World();
	// initialization
	void Clear();
	void DummyWorld();
	void LoadSky( const char* filename, const char* bin_name );
	void ForceSyncAllBricks();
	// camera
	void SetCameraMatrix( const mat4& m ) { camMat = m; }
	float3 GetCameraViewDir() { return make_float3( camMat[2], camMat[6], camMat[10] ); }
	mat4& GetCameraMatrix() { return camMat; }
	// render flow
	void Commit();
	void Render();
	// high-level voxel access
	void Sphere( const float x, const float y, const float z, const float r, const uint c );
	void HDisc( const float x, const float y, const float z, const float r, const uint c );
	void Print( const char* text, const uint x, const uint y, const uint z, const uint c );
	uint LoadSprite( const char* voxFile, bool palShift = true );
	uint CloneSprite( const uint idx );
	uint CreateSprite( const int3 pos, const int3 size, const int frames );
	uint SpriteFrameCount( const uint idx );
	void MoveSpriteTo( const uint idx, const uint x, const uint y, const uint z );
	void RemoveSprite( const uint idx );
	void SetSpriteFrame( const uint idx, const uint frame );
	bool SpriteHit( const uint A, const uint B );
	uint CreateParticles( const uint count );
	void SetParticle( const uint set, const uint idx, const uint3 pos, const uint v );
	uint LoadTile( const char* voxFile );
	uint LoadBigTile( const char* voxFile );
	void DrawTile( const uint idx, const uint x, const uint y, const uint z );
	void DrawTiles( const char* tileString, const uint x, const uint y, const uint z );
	void DrawBigTile( const uint idx, const uint x, const uint y, const uint z );
	void DrawBigTiles( const char* tileString, const uint x, const uint y, const uint z );
	// inline ray tracing / cpu-only ray tracing
	uint TraceRay( float4 A, const float4 B, float& dist, float3& N, int steps );
	// block scrolling
	void ScrollX( const int offset );
	void ScrollY( const int offset );
	void ScrollZ( const int offset );
private:
	// internal methods
	void EraseSprite( const uint idx );
	void DrawSprite( const uint idx );
	void DrawSpriteShadow( const uint idx );
	void RemoveSpriteShadow( const uint idx );
	void EraseParticles( const uint set );
	void DrawParticles( const uint set );
	void DrawTileVoxels( const uint cellIdx, const uchar* voxels, const uint zeroes );
public:
	// low-level voxel access
	__forceinline uint Get( const uint x, const uint y, const uint z )
	{
		// calculate brick location in top-level grid
		const uint bx = (x / BRICKDIM) & (GRIDWIDTH - 1);
		const uint by = (y / BRICKDIM) & (GRIDHEIGHT - 1);
		const uint bz = (z / BRICKDIM) & (GRIDDEPTH - 1);
		const uint cellIdx = bx + bz * GRIDWIDTH + by * GRIDWIDTH * GRIDDEPTH;
		const uint g = grid[cellIdx];
		if ((g & 1) == 0 /* this is currently a 'solid' grid cell */) return g >> 1;
		// calculate the position of the voxel inside the brick
		const uint lx = x & (BRICKDIM - 1), ly = y & (BRICKDIM - 1), lz = z & (BRICKDIM - 1);
		return brick[(g >> 1) * BRICKSIZE + lx + ly * BRICKDIM + lz * BRICKDIM * BRICKDIM];
	}
	__forceinline void Set( const uint x, const uint y, const uint z, const uint v /* actually an 8-bit value */ )
	{
		// calculate brick location in top-level grid
		const uint bx = x / BRICKDIM;
		const uint by = y / BRICKDIM;
		const uint bz = z / BRICKDIM;
		if (bx >= GRIDWIDTH || by >= GRIDHEIGHT || bz >= GRIDDEPTH) return;
		const uint cellIdx = bx + bz * GRIDWIDTH + by * GRIDWIDTH * GRIDDEPTH;
		// obtain current brick identifier from top-level grid
		uint g = grid[cellIdx], g1 = g >> 1;
		if ((g & 1) == 0 /* this is currently a 'solid' grid cell */)
		{
			if (g1 == v) return; // about to set the same value; we're done here
			const uint newIdx = NewBrick();
		#if BRICKDIM == 8
			// fully unrolled loop for writing the 512 bytes needed for a single brick, faster than memset
			const __m256i zero8 = _mm256_set1_epi8( static_cast<char>(g1) );
			__m256i* d8 = (__m256i*)(brick + newIdx * BRICKSIZE);
			d8[0] = zero8, d8[1] = zero8, d8[2] = zero8, d8[3] = zero8;
			d8[4] = zero8, d8[5] = zero8, d8[6] = zero8, d8[7] = zero8;
			d8[8] = zero8, d8[9] = zero8, d8[10] = zero8, d8[11] = zero8;
			d8[12] = zero8, d8[13] = zero8, d8[14] = zero8, d8[15] = zero8;
		#else
			// let's keep the memset in case we want to experiment with other brick sizes
			memset( brick + newIdx * BRICKSIZE, g1, BRICKSIZE ); // copy solid value to brick
		#endif
			// we keep track of the number of zeroes, so we can remove fully zeroed bricks
			brickInfo[newIdx].zeroes = g == 0 ? BRICKSIZE : 0;
			g1 = newIdx, grid[cellIdx] = g = (newIdx << 1) | 1;
			// brickInfo[newIdx].location = cellIdx; // not used yet
		}
		// calculate the position of the voxel inside the brick
		const uint lx = x & (BRICKDIM - 1), ly = y & (BRICKDIM - 1), lz = z & (BRICKDIM - 1);
		const uint voxelIdx = g1 * BRICKSIZE + lx + ly * BRICKDIM + lz * BRICKDIM * BRICKDIM;
		const uint cv = brick[voxelIdx];
		if ((brickInfo[g1].zeroes += (cv != 0 && v == 0) - (cv == 0 && v != 0)) < BRICKSIZE)
		{
			brick[voxelIdx] = v;
			Mark( g1 ); // tag to be synced with GPU
			return;
		}
		grid[cellIdx] = 0;	// brick just became completely zeroed; recycle
		UnMark( g1 );		// no need to send it to GPU anymore
		FreeBrick( g1 );
	}
private:
	uint NewBrick()
	{
	#if THREADSAFEWORLD
		// get a fresh brick from the circular list in a thread-safe manner and without false sharing
		const uint trashItem = InterlockedAdd( &trashTail, 31 ) - 31;
		return trash[trashItem & (BRICKCOUNT - 1)];
	#else
		// slightly faster to not prevent false sharing if we're doing single core updates only
		return trash[trashTail++ & (BRICKCOUNT - 1)];
	#endif
	}
	void FreeBrick( const uint idx )
	{
	#if THREADSAFEWORLD
		// thread-safe access of the circular list
		const uint trashItem = InterlockedAdd( &trashHead, 31 ) - 31;
		trash[trashItem & (BRICKCOUNT - 1)] = idx;
	#else
		// for single-threaded code, a stepsize of 1 maximizes cache coherence.
		trash[trashHead++ & (BRICKCOUNT - 1)] = idx;
	#endif
	}
	void Mark( const uint idx )
	{
	#if THREADSAFEWORLD
		// be careful, setting a bit in an array is not thread-safe without _interlockedbittestandset
		_interlockedbittestandset( (LONG*)modified + (idx >> 5), idx & 31 );
	#else
		modified[idx >> 5] |= 1 << (idx & 31);
	#endif
	}
	void UnMark( const uint idx )
	{
	#if THREADSAFEWORLD
		// be careful, resetting a bit in an array is not thread-safe without _interlockedbittestandreset
		_interlockedbittestandreset( (LONG*)modified + (idx >> 5), idx & 31 );
	#else
		modified[idx >> 5] &= 0xffffffffu - (1 << (idx & 31));
	#endif
	}
	bool IsDirty( const uint idx ) { return (modified[idx >> 5] & (1 << (idx & 31))) > 0; }
	bool IsDirty32( const uint idx ) { return modified[idx] != 0; }
	void ClearMarks32( const uint idx ) { modified[idx] = 0; }
	void ClearMarks() { memset( modified, 0, (BRICKCOUNT / 32) * 4 ); }
	// helpers
#if 1
	// this version: CO'21
	__forceinline static void StreamCopy( __m256i* dst, const __m256i* src, const uint bytes )
	{
		// https://stackoverflow.com/questions/2963898/faster-alternative-to-memcpy
		assert( (bytes & 31) == 0 );
		// AVX2
		uint N = bytes / 32;
		constexpr uint registers = 8;
		uint unalignedStep = N % registers;
		for (; N > 0 && unalignedStep > 0; N--, unalignedStep--, src++, dst++)
		{
			const __m256i d = _mm256_stream_load_si256( src );
			_mm256_stream_si256( dst, d );
		}
		static_assert(registers == 8);
		for (; N > 0; N -= registers)
		{
			// Based on https://stackoverflow.com/questions/62419256/how-can-i-determine-how-many-avx-registers-my-processor-has
			const __m256i d0 = _mm256_stream_load_si256( src++ );
			_mm256_stream_si256( dst++, d0 );
			const __m256i d1 = _mm256_stream_load_si256( src++ );
			_mm256_stream_si256( dst++, d1 );
			const __m256i d2 = _mm256_stream_load_si256( src++ );
			_mm256_stream_si256( dst++, d2 );
			const __m256i d3 = _mm256_stream_load_si256( src++ );
			_mm256_stream_si256( dst++, d3 );
			const __m256i d4 = _mm256_stream_load_si256( src++ );
			_mm256_stream_si256( dst++, d4 );
			const __m256i d5 = _mm256_stream_load_si256( src++ );
			_mm256_stream_si256( dst++, d5 );
			const __m256i d6 = _mm256_stream_load_si256( src++ );
			_mm256_stream_si256( dst++, d6 );
			const __m256i d7 = _mm256_stream_load_si256( src++ );
			_mm256_stream_si256( dst++, d7 );
		}
	}
#else
	static void StreamCopy( __m256i* dst, const __m256i* src, const uint bytes )
	{
		// https://stackoverflow.com/questions/2963898/faster-alternative-to-memcpy
		assert( (bytes & 31) == 0 );
	#if 0
		// SSE 4.2
		uint N = bytes / 16;
		const __m128i* src4 = (__m128i*)src;
		__m128i* dst4 = (__m128i*)dst;
		for (; N > 0; N--, src4++, dst4++)
		{
			const __m128i d = _mm_stream_load_si128( src4 );
			_mm_stream_si128( dst4, d );
		}
	#else
		// AVX2
		uint N = bytes / 32;
		for (; N > 0; N--, src++, dst++)
		{
			const __m256i d = _mm256_stream_load_si256( src );
			_mm256_stream_si256( dst, d );
		}
	#endif
	}
#endif
	void StreamCopyMT( __m256i* dst, __m256i* src, const uint bytes );
	// helper class for multithreaded memcpy
	class CopyJob : public Job
	{
	public:
		void Main() { World::StreamCopy( dst, src, N * 32 ); }
		__m256i* dst, * src;
		uint N;
	};
	// data members
	mat4 camMat;						// camera matrix to be used for rendering
	uint* grid = 0, *gridOrig = 0;		// pointer to host-side copy of the top-level grid
	Buffer* brickBuffer[4];				// OpenCL buffer for the bricks
	uchar* brick = 0;					// pointer to host-side copy of the bricks
	uint* modified = 0;					// bitfield to mark bricks for synchronization
	BrickInfo* brickInfo = 0;			// maintenance data for bricks: zeroes, location
	volatile inline static LONG trashHead = BRICKCOUNT;	// thrash circular buffer tail
	volatile inline static LONG trashTail = 0;	// thrash circular buffer tail
	uint* trash = 0;					// indices of recycled bricks
	Buffer* screen = 0;					// OpenCL buffer that encapsulates the target OpenGL texture
	uint targetTextureID = 0;			// OpenGL render target
	int prevFrameIdx = 0;				// index of the previous frame buffer that will be used for TAA
	Buffer* paramBuffer = 0;			// OpenCL buffer that stores renderer parameters
	Buffer* sky = 0;					// OpenCL buffer for a HDR skydome
	Buffer* blueNoise = 0;				// blue noise data
	int2 skySize;						// size of the skydome bitmap
	RenderParams params;				// CPU-side copy of the renderer parameters
	Kernel* renderer, * committer;		// render kernel and commit kernel
	cl_event copyDone, commitDone;		// events for queue synchronization
	cl_event renderDone;				// event used for profiling
	uint tasks = 0;						// number of changed bricks, to be passed to commit kernel
	bool copyInFlight = false;			// flag for skipping async copy on first iteration
	bool commitInFlight = false;		// flag to make next commit wait for previous to complete
	cl_mem devmem = 0;					// device-side commit buffer
	cl_mem gridMap;						// host-side 3D image for top-level
	Surface* font;						// bitmap font for print command
	bool firstFrame = true;				// for doing things in the first frame
public: // TODO: protected
	vector<Sprite*> sprite;				// list of loaded sprites
	vector<Particles*> particles;		// list of particle sets
	vector<Tile*> tile;					// list of loaded tiles
	vector<BigTile*> bigTile;			// list of loaded big tiles
private:
	struct RayPacket { float4 Nt[TILESIZE2]; uint c[TILESIZE2]; };
};

} // namespace Tmpl8