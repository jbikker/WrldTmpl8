#include "precomp.h"
#include "mcviewer.h"
#include "enkimi.h"

Game* game = new MCViewer();

/*
Notes on the data structure used in this application:

- The world is 8192 x 256 x 8192 voxels in size, 1 byte per voxel,
  so a total of 16GB of raw data.
- This data is subdivided in 8x8x8 bricks. There are thus
  1024 x 32 x 1024 bricks in the world.
- A brick may be used multiple times, to save disk space. Therefore,
  a top-level grid contains 1024 x 32 x 1024 brick indices. For the
  test world, these are all unique, for now.
- The actual brick data is stored in 256 'block000.bin' files. Each
  of these contains 131072 bricks, for a total of 64MB. Brick x
  can be found in block file x / 131072.

Special note:

- Brick idx 0 is a special one: this denotes an empty 8x8x8 area in
  the map. An obvious optimization of the data set would be to 
  replace any empty 8x8x8 area with a zero index, to save space,
  and processing time.
*/

static int brickCount = 1024 * 1024 * 32; // all bricks are unique when we start
static int deletedBricks = 0;

uint GetBrickIndex( int x, int y, int z )
{
	// calculate position of voxel in top level grid
	int tlidx = x + z * 1024 + y * 1024 * 1024;
	// read the brick index for the specified voxel from the index file
	FILE* f = fopen( "assets/index.bin", "rb" );
	fseek( f, tlidx * 4, SEEK_SET );
	uint brickIdx;
	fread( &brickIdx, 1, 4, f );
	fclose( f );
	return brickIdx;
}

void SetBrickIndex( int x, int y, int z, uint brickIdx )
{
	// calculate position of voxel in top level grid
	int tlidx = x + z * 1024 + y * 1024 * 1024;
	// write the brick index for the specified location to the index file
	FILE* f = fopen( "assets/index.bin", "r+b" /* open for updating */ );
	fseek( f, tlidx * 4, SEEK_SET );
	fwrite( &brickIdx, 1, 4, f );
	fflush( f );
	fclose( f );
}

void CopyBrick( uint dst, uint src )
{
	int srcRegionIdx = src / 131072;
	int dstRegionIdx = dst / 131072;
	char srcBinFile[128];
	sprintf( srcBinFile, "assets/block%03i.bin", srcRegionIdx );
	char dstBinFile[128];
	sprintf( dstBinFile, "assets/block%03i.bin", dstRegionIdx );
	FILE* s = fopen( srcBinFile, "rb" ); // TODO: will this work if they are the same? Probably yes.
	FILE* d = fopen( dstBinFile, "r+b" /* open for updating */ ); 
	fseek( s, (src & 131071) * 512, SEEK_SET );
	fseek( d, (dst & 131071) * 512, SEEK_SET );
	uchar brickData[512];
	fread( brickData, 1, 512, s );
	fwrite( brickData, 1, 512, d );
	fclose( s );
	fflush( d );
	fclose( d );
}

uchar ReadVoxel( int x, int y, int z )
{
	// read the brick index from the index file
	uint brickIdx = GetBrickIndex( x / 8, y / 8, z / 8 );
	// handle the special 'empty' brick
	if (brickIdx == 0) return 0;
	// find the region file that contains the specified brick
	int regionIdx = brickIdx / 131072;
	char binFile[128];
	sprintf( binFile, "assets/block%03i.bin", regionIdx );
	// find the brick in the region file
	FILE* r = fopen( binFile, "rb" );
	fseek( r, (brickIdx & 131071) * 512, SEEK_SET );
	uchar brick[512];
	fread( brick, 1, 512, r );
	fclose( r );
	// find the specified voxel in the brick
	int localx = x & 7, localy = y & 7, localz = z & 7;
	return brick[localx + localy * 8 + localz * 8 * 8];
}

void WriteVoxel( int x, int y, int z, uchar v )
{
	// read the brick index from the index file
	uint brickIdx = GetBrickIndex( x / 8, y / 8, z / 8 );
	// handle the special 'empty' brick
	if (brickIdx == 0) 
	{
		// brick is empty; claim a fresh one at the end
		brickIdx = brickCount++;
		// update the new brick index in the index file
		SetBrickIndex( x / 8, y / 8, z / 8, brickIdx );
		// we can now use this new brick index.
	}
	// find the region file that contains the specified brick
	int regionIdx = brickIdx / 131072;
	char binFile[128];
	sprintf( binFile, "assets/block%03i.bin", regionIdx );
	// write the voxel in the brick in the region file
	int localx = x & 7, localy = y & 7, localz = z & 7;
	int posInBrick = localx + localy * 8 + localz * 8 * 8;
	FILE* r = fopen( binFile, "rb" );
	fseek( r, (brickIdx & 131071) * 512 + posInBrick, SEEK_SET );
	fwrite( &v, 1, 1, r );
	fclose( r );
}

void OptimizeWorld()
{
	// loop over all bricks in the world, searching for 8x8x8 empty voxels
#if 1
	// optimize just a small window, for testing
	for( int x = 512; x < 516; x++ ) for( int z = 512; z < 516; z++ ) 
	{
		printf( "Optimizing %i,%i\n", x, z );
#else
	// optimize the full world, will take ages
	for( int x = 0; x < 1024; x++ ) for( int z = 0; z < 1024; z++ ) 
	{
#endif
		// operate on columns of bricks, starting in the sky for a quick win
		for( int y = 31; y >= 0; y-- )
		{
			// get the index of the brick we are about to recycle
			uint brickIdx = GetBrickIndex( x, y, z );
			// skip the brick if it was already cleared
			if (brickIdx == 0) continue;
			// scan the brick for 8x8x8 zeroes
			bool allEmpty = true; // until proven otherwise
			for( int u = 0; u < 8; u++ ) for( int v = 0; v < 8; v++ ) for( int w = 0; w < 8; w++ )
				if (ReadVoxel( x * 8 + u, y * 8 + v, z * 8 + w ) != 0) { allEmpty = false; break; }
			if (allEmpty)
			{
				// this brick is now empty, yay!
				SetBrickIndex( x, y, z, 0 );
				// move the last brick over this brick's data
				CopyBrick( brickIdx, --brickCount );
				SetBrickIndex( brickCount, 0, 0, brickIdx );
				deletedBricks++;
				// once we have removed 131072 bricks, we can kill one region file
				if (deletedBricks == 131072)
				{
					printf( "killed a region file.\n" );
					deletedBricks = 0;
					// TODO: do the actual killing.
				}
			}
		}
	}
}

// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void MCViewer::Init()
{
#if 0
	// import a minecraft region folder world from assets/scene using enkiMI
	ClearWorld();
	uchar* regionData = new uchar[512 * 512 * 256]; // 64MB raw voxel data
	uchar* brickData = new uchar[512 * 512 * 256]; // 64MB raw voxel data
	uint* palette = enkiGetMineCraftPalette();
	for (int x = -8; x < 8; x++) for (int y = -8; y < 8; y++) // that's 8192 x 8192, 256 blocks in height
	{
		char mcaFile[128];
		sprintf( mcaFile, "assets/scene/region/r.%i.%i.mca", x, y );
		FILE* f = fopen( mcaFile, "rb" );
		if (f)
		{
			memset( regionData, 0, 512 * 512 * 256 );
			memset( brickData, 0, 512 * 512 * 256 );
			enkiRegionFile regionFile = enkiRegionFileLoad( f );
			for (int i = 0; i < 1024; i++)
			{
				enkiNBTDataStream stream;
				enkiInitNBTDataStreamForChunk( regionFile, i, &stream );
				if (stream.dataLength)
				{
					enkiChunkBlockData aChunk = enkiNBTReadChunk( &stream );
					enkiMICoordinate p, chunkOriginPos = enkiGetChunkOrigin( &aChunk ); // y always 0
					for (int section = 0; section < 16; ++section) if (aChunk.sections[section])
					{
						enkiMICoordinate so = enkiGetChunkSectionOrigin( &aChunk, section );
						for (int z, x, y = 0; y < 16; y++) for (z = 0; z < 16; z++) for (x = 0; x < 16; x++)
						{
							p.x = x, p.y = y, p.z = z;
							uint32_t voxel = enkiGetChunkSectionVoxel( &aChunk, section, p );
							uint voxelColor = palette[voxel];
							uint vc8r = ((voxelColor >> 16) & 255) >> 5;
							uint vc8g = ((voxelColor >> 8) & 255) >> 5;
							uint vc8b = (voxelColor & 255) >> 6;
							uint vc8 = (vc8r << 5) + (vc8g << 2) + vc8b;
							if (voxel != 0 && vc8 == 0) vc8 = 1 << 5;
							Plot( so.x + x, so.y + y, so.z + z, vc8 );
							uint rx = (so.x + x) & 511;	// position in region
							uint ry = so.y + y;			// y is already in the range 0..255
							uint rz = (so.z + z) & 511;	// position in region
							regionData[rx + rz * 512 + ry * 512 * 512] = vc8;
						}
					}
					enkiNBTRewind( &stream );
				}
				enkiNBTFreeAllocations( &stream );
			}
			enkiRegionFileFreeAllocations( &regionFile );
			fclose( f );
			char binFile[128];
			sprintf( binFile,  "assets/block%03i.bin", (x + 8) + (y + 8) * 16 );
			FILE* o = fopen( binFile, "wb" );
			// reorganize region to 128k bricks
			for (int x = 0; x < 64; x++) for (int y = 0; y < 32; y++) for (int z = 0; z < 64; z++)
			{
				uint brickIdx = x + z * 64 + y * 64 * 64;
				uchar* dst = brickData + brickIdx * 512;
				uchar* src = regionData + x * 8 + z * 8 * 512 + y * 8 * 512 * 512;
				for (int w = 0; w < 8; w++) for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++)
					*dst++ = src[u + w * 512 + v * 512 * 512];
			}
			// write binary data
			fwrite( brickData, 1, 512 * 512 * 256, o );
			// done, next
			fclose( o );
			printf( "wrote file: %s\n", binFile );
		}
	}
	// write brick indices
	uint* brickIndices = new uint[1024 * 1024 * 32]; // 128MB of indices
	for (int x = 0; x < 1024; x++) for (int y = 0; y < 32; y++) for (int z = 0; z < 1024; z++)
	{
		int regionx = x / 64;
		int regionz = z / 64;
		int regionIndex = regionx + regionz * 16;
		int localx = x & 63;
		int localy = y & 31;
		int localz = z & 63;
		int localIndex = localx + localz * 64 + localy * 64 * 64;
		brickIndices[x + z * 1024 + y * 1024 * 1024] = regionIndex * 131072 + localIndex;
	}
	FILE* i = fopen( "assets/index.bin", "wb" );
	fwrite( brickIndices, 1, 1024 * 1024 * 32 * 4, i );
	fclose( i );
#else
	// load a 1024x256x1024 slice of the binary world
	ClearWorld();
	// test the optimizer
	Timer t;
	t.reset();
	OptimizeWorld();
	printf( "optimization took %1.2fs for 4x4 bricks;\nprojected to 1024x1024: %1.1fmin.\n", t.elapsed(), (t.elapsed() * 65536) / 60 );
#if 0
	// load master brick index
	uint* index = new uint[1024 * 1024 * 32]; // 128MB of indices
	FILE* i = fopen( "assets/index.bin", "rb" );
	fread( index, 1, 1024 * 1024 * 32 * 4, i );
	fclose( i );
	// load brickmap data
#if 1
	for (int x = 60; x < 76; x++) for (int y = 0; y < 32; y++) for (int z = 60; z < 76; z++)
	{
		uchar brickData[512];
		LoadBrick( x + 512, y, z + 512, brickData );
		uchar* src = brickData;
		for (int bz = 0; bz < 8; bz++) for (int by = 0; by < 8; by++) for (int bx = 0; bx < 8; bx++)
			Plot( x * 8 + bx, y * 8 + by, z * 8 + bz, *src++ );
		printf( "." );
	}
#else
	uchar* brickData = new uchar[512 * 512 * 256]; // 64MB raw voxel data
	for (int x = 0; x < 2; x++) for (int z = 0; z < 2; z++)
	{
		// fetch file
		int rx = (x + 8) * 512, rz = (z + 8) * 512;
		char binFile[128];
		sprintf( binFile, "assets/x%04iy%04i.bin", rx, rz );
		FILE* f = fopen( binFile, "rb" );
		fread( brickData, 1, 512 * 512 * 256, f );
		printf( "read file: %s\n", binFile );
		fclose( f );
		// digest data
		for (int u = 0; u < 64; u++) for (int v = 0; v < 32; v++) for (int w = 0; w < 64; w++)
		{
			uint brickIdx = u + w * 64 + v * 64 * 64;
			uchar* src = brickData + brickIdx * 512;
			for (int bz = 0; bz < 8; bz++) for (int by = 0; by < 8; by++) for (int bx = 0; bx < 8; bx++)
				Plot( x * 512 + u * 8 + bx, v * 8 + by, z * 512 + w * 8 + bz, *src++ );
		}
	}
#endif
#endif
#endif
	World* world = GetWorld();
	world->SetCameraMatrix( mat4::LookAt( make_float3( 600, 250, 800 ), make_float3( 452, 10, 212 ) ) );
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void MCViewer::Tick( float deltaTime )
{
	// This function gets called once per frame by the template code.
#if 1
	// visualize a small slice of the landscape
	static int x = 512, y = 0, z = 512;
	for( int i = 0; i < 256; i++ )
	{
		uchar v = ReadVoxel( x + 4000, y, z + 4000 );
		Plot( x, y, z, v );
		if (++y == 256) { y = 0; if (++x == 560) x = 512, z++; }
	}
#else
	// visualize a large slice of the landscape, once the app is faster
	static int x = 128, y = 0, z = 128;
	for( int i = 0; i < 256; i++ )
	{
		uchar v = ReadVoxel( x + 4000, y, z + 4000 );
		Plot( x, y, z, v );
		if (++y == 256) { y = 0; if (++x == 896) x = 128, z++; }
	}
#endif
}