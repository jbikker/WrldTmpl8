#pragma once

#define THREADSAFEWORLD 0
#define SQR(x) ((x)*(x))
#define TILESIZE	8
#define TILESIZE2	(TILESIZE * TILESIZE)

namespace Tmpl8
{
struct BrickInfo // new BrickInfo: B&H'21
{
private:
	uint data;
public:
	// 1 bit (0 = solid)
	inline bool isSolid() const { return (data & 1) == 0; }
	inline bool isAir() const { return data == 0; }
	inline void makeAir() { data = 0; }
	// 8 bits, unioned with Zeroes
	inline uchar getColor() const { assert( isSolid() ); return static_cast<uchar>(data >> 1); }
	inline void setColor( const uchar color ) { assert( isSolid() ); data = (static_cast<uint>(color) << 1); }
	// 9 bits, unioned with Color
	inline uint getZeroes() const { assert( !isSolid() ); return data >> 1; }
	inline void setZeroes( const uint zeros ) { data = (zeros << 1) | 1; }
	inline void addZeroes( const uint toAdd ) { assert( !isSolid() ); data += toAdd << 1; }
	inline uint getData() const { return data; };
};

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
	int3 currPos = make_int3( -9999 );	// location where the sprite will be drawn
	int currFrame = 0;					// frame to draw
	bool hasShadow = false;				// set to true to enable a drop shadow
	inline SpriteFrame* GetFrame() { return frame[currFrame]; }
};

class Particles
{
public:
	Particles( int N )					// constructor
	{
		count = N, voxel = new uint4[N];
		memset( voxel, 0, N * sizeof( uint4 ) );
		voxel[0].x = -9999; // inactive by default
	}
	uint4* voxel = 0;					// particle positions & color
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

struct BitArray
{
private:
	uint bitfields[BRICKCOUNT / sizeof( uint )]; // 1 bit per brick, to track 'dirty' bricks
public:
	void Mark( const uint idx )
	{
	#if THREADSAFEWORLD
		// be careful, setting a bit in an array is not thread-safe without _interlockedbittestandset
		_interlockedbittestandset( (LONG*)bitfields + (idx >> 5), idx & 31 );
	#else
		bitfields[idx >> 5] |= 1 << (idx & 31);
	#endif
	}
	void UnMark( const uint idx )
	{
	#if THREADSAFEWORLD
		// be careful, resetting a bit in an array is not thread-safe without _interlockedbittestandreset
		_interlockedbittestandreset( (LONG*)bitfields + (idx >> 5), idx & 31 );
	#else
		bitfields[idx >> 5] &= 0xffffffffu - (1 << (idx & 31));
	#endif
	}
	bool IsDirty( const uint idx ) { return (bitfields[idx >> 5] & (1 << (idx & 31))) > 0; }
	bool IsDirty32( const uint idx ) { return bitfields[idx] != 0; }
	void ClearMarks32( const uint idx ) { bitfields[idx] = 0; }
	void ClearMarks() { memset( bitfields, 0, (BRICKCOUNT / 32) * 4 ); }
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
	void Draw();
	void Commit();
	void Erase();
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
	void DrawSprite( const uint idx );
	void DrawSpriteShadow( const uint idx );
	void DrawParticles( const uint set );
	void DrawTileVoxels( const uint cellIdx, const uchar* voxels, const uint zeroes );
	void BackupBrick( const uint cellIdx )
	{
		if (modifiedBackup.IsDirty( cellIdx )) return;
		modifiedBackup.Mark( cellIdx );
		const auto& info = brickInfo[cellIdx];
		brickInfoBackup[cellIdx] = info;
		if (info.isSolid()) return; // this is a 'solid' grid cell
		const uint brickOffset = cellIdx * BRICKSIZE;
		memcpy( brickBackup + brickOffset, brick + brickOffset, BRICKSIZE );
	}
public:
	__forceinline uint GetBrickIdx( const uint x, const uint y, const uint z )
	{
		const uint bx = (x / BRICKDIM - params.scroll.x) & (GRIDWIDTH - 1);
		const uint by = (y / BRICKDIM - params.scroll.y) & (GRIDHEIGHT - 1);
		const uint bz = (z / BRICKDIM - params.scroll.z) & (GRIDDEPTH - 1);
		return bx + bz * GRIDWIDTH + by * GRIDWIDTH * GRIDDEPTH;
	}
	// low-level voxel access
	__forceinline uchar Get( const uint x, const uint y, const uint z )
	{
		const uint cellIdx = GetBrickIdx( x, y, z );
		const auto& info = brickInfo[cellIdx];
		if (info.isSolid() /* this is currently a 'solid' grid cell */) return info.getColor();
		// calculate the position of the voxel inside the brick
		const uint lx = x & (BRICKDIM - 1), ly = y & (BRICKDIM - 1), lz = z & (BRICKDIM - 1);
		return brick[cellIdx * BRICKSIZE + lx + ly * BRICKDIM + lz * BRICKDIM * BRICKDIM];
	}
	__forceinline void Set( const uint x, const uint y, const uint z, const uint v /* actually an 8-bit value */ )
	{
		if (x >= MAPWIDTH || y >= MAPHEIGHT || z >= MAPDEPTH) return;
		const uint cellIdx = GetBrickIdx( x, y, z );
		if (createBackup) BackupBrick( cellIdx );
		// obtain current brick identifier from top-level grid
		auto& info = brickInfo[cellIdx];
		if (info.isSolid() /* this is currently a 'solid' grid cell */)
		{
			if (info.getColor() == v) return; // about to set the same value; we're done here
			FillBrick( cellIdx, info.getColor() );
			// we keep track of the number of zeroes, so we can remove fully zeroed bricks
			// (also triggers the info to be not solid anymore)
			info.setZeroes( info.getColor() == 0 ? BRICKSIZE : 0 );
		}
		// calculate the position of the voxel inside the brick
		const uint lx = x & (BRICKDIM - 1), ly = y & (BRICKDIM - 1), lz = z & (BRICKDIM - 1);
		const uint voxelIdx = cellIdx * BRICKSIZE + lx + ly * BRICKDIM + lz * BRICKDIM * BRICKDIM;
		const uchar cv = brick[voxelIdx];
		// add or remove a zero depending on whether we add or remove air
		info.addZeroes( (cv != 0 && v == 0) - (cv == 0 && v != 0) );
		modified.Mark( cellIdx ); // tag to be synced with GPU
		if (info.getZeroes() < BRICKSIZE) brick[voxelIdx] = v; else info.makeAir();
	}
private:
	__forceinline void FillBrick( uint idx, char g )
	{
	#if BRICKDIM == 8
		// fully unrolled loop for writing the 512 bytes needed for a single brick, faster than memset
		const __m256i zero8 = _mm256_set1_epi8( g );
		__m256i* d8 = (__m256i*)(brick + idx * BRICKSIZE);
		d8[0] = zero8, d8[1] = zero8, d8[2] = zero8, d8[3] = zero8;
		d8[4] = zero8, d8[5] = zero8, d8[6] = zero8, d8[7] = zero8;
		d8[8] = zero8, d8[9] = zero8, d8[10] = zero8, d8[11] = zero8;
		d8[12] = zero8, d8[13] = zero8, d8[14] = zero8, d8[15] = zero8;
	#else
		// let's keep the memset in case we want to experiment with other brick sizes
		memset( brick + newIdx * BRICKSIZE, g1, BRICKSIZE ); // copy solid value to brick
	#endif
	}
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
	Buffer* brickBuffer[4];				// OpenCL buffers for the bricks, 4x256MB=1GB
	bool createBackup = false;
	uchar* brick = 0, * brickBackup = 0;				// pointer to host-side copy of the bricks
	BitArray modified, modifiedBackup;					// bitfield to mark bricks for synchronization
	BrickInfo* brickInfo = 0, * brickInfoBackup = 0;	// maintenance data for bricks: zeroes, location
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
	uint* pinnedMemPtr = 0;				// Commit buffer for GPU
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