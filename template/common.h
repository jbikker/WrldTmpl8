// default screen resolution
#define SCRWIDTH	1600
#define SCRHEIGHT	1024

// IMPORTANT NOTE ON OPENCL COMPATIBILITY ON OLDER LAPTOPS:
// Without a GPU, a laptop needs at least a 'Broadwell' Intel CPU (5th gen, 2015):
// Intel's OpenCL implementation 'NEO' is not available on older devices.
// Same is true for Vulkan, OpenGL 4.0 and beyond, as well as DX11 and DX12.

// TODO:
// - If we have enough bricks for the full map, each brick pos can point to a fixed brick.
// - An irregular grid may be faster.
// - Add TAA and GI. :)

// settings shared between c/c++ and OpenCL
#define MAPWIDTH	1024	// total world size, x-axis
#define MAPHEIGHT	1024	// total world height
#define MAPDEPTH	1024	// total world size, z-axis
#define BRICKDIM	8		// brick dimensions
#define BDIMLOG2	3		// must be log2(BRICKDIM)
#define MAXCOMMITS	8192	// maximum number of bricks that can be committed per frame
#if 0
// 8-bit voxels: RGB332
#define VOXEL8
#define PAYLOAD unsigned char
#define PAYLOADSIZE	1
#else
// 16-bit voxels: MRGB4444, where M=material index
#define VOXEL16
#define PAYLOAD unsigned short
#define PAYLOADSIZE 2
#endif

// renderer performance setting: set to 0 for slower devices, up to 8 for fast GPUs
#define GIRAYS		6

// Panini projection, http://tksharpless.net/vedutismo/Pannini/panini.pdf via https://www.shadertoy.com/view/Wt3fzB
#define PANINI		0	// 0 to disable, 1 to enable

// TAA, as in https://www.shadertoy.com/view/3sfBWs
#define TAA			1	// 0 to disable, 1 to enable

// MSAA
#define AA_SAMPLES	1	// 1 to disable, 2..4 to enable. Note: will be squared.

// some useful color names
#ifdef VOXEL8
#define BLACK		(1<<5)	// actually: dark red; black itself is transparent
#define GREEN		(7<<2)
#define BLUE		3
#define RED			(7<<5)
#define YELLOW		(RED+GREEN)
#define WHITE		(255)
#define GREY		((3<<5)+(3<<2)+1)
#define ORANGE		((7<<5)+(5<<2))
#define LIGHTBLUE	(3+(4<<2)+(4<<5))
#define BROWN		((3<<5)+(1<<2))
#define LIGHTRED	(7<<5)+(2<<2)+1
#else
#define BLACK		0x001	// actually: dark blue; black itself is transparent
#define GREEN		0x0F0
#define BLUE		0x00F
#define RED			0xF00
#define YELLOW		0xFF0
#define WHITE		0xFFF
#define GREY		0x777
#define ORANGE		0xF70
#define LIGHTBLUE	0x77F
#define BROWN		0x720
#define LIGHTRED	0xF55
#endif

// renderer
struct RenderParams
{
	float2 oneOverRes;
	float3 E, p0, p1, p2;
	uint R0, frame;
	uint skyWidth, skyHeight;
	float4 skyLight[6];
	float skyLightScale, dummy1, dummy2, dummy3;
	// reprojection data
	float4 Nleft, Nright, Ntop, Nbottom;
	float4 prevRight, prevDown;
	float4 prevP0, prevP1, prevP2, prevP3;
};

// lighting for 6 normals for sky15.hdr
#define NX0			0.550f, 0.497f, 0.428f		// N = (-1, 0, 0)
#define NX1			0.399f, 0.352f, 0.299f		// N = ( 1, 0, 0)
#define NY0			0.470f, 0.428f, 0.373f		// N = ( 0,-1, 0)
#define NY1			0.370f, 0.346f, 0.312f		// N = ( 0, 1, 0)
#define NZ0			0.245f, 0.176f, 0.108f		// N = ( 0, 0,-1)
#define NZ1			0.499f, 0.542f, 0.564f		// N = ( 0, 0, 1)

// derived
#define GRIDWIDTH	(MAPWIDTH / BRICKDIM)
#define GRIDHEIGHT	(MAPHEIGHT / BRICKDIM)
#define GRIDDEPTH	(MAPDEPTH / BRICKDIM)
#define GRIDSIZE	(GRIDWIDTH * GRIDHEIGHT * GRIDWIDTH)
#define BRICKSIZE	(BRICKDIM * BRICKDIM * BRICKDIM)
#define UBERWIDTH	(GRIDWIDTH / 4)
#define UBERHEIGHT	(GRIDHEIGHT / 4)
#define UBERDEPTH	(GRIDDEPTH / 4)
// note: we reserve 50% of the theoretical peak; a normal scene shouldn't come close to
// using that many unique (non-empty!) bricks.
#define BRICKCOUNT	((((MAPWIDTH / BRICKDIM) * (MAPHEIGHT / BRICKDIM) * (MAPDEPTH / BRICKDIM))) / 2)
#define BRICKCOMMITSIZE	(MAXCOMMITS * BRICKSIZE * PAYLOADSIZE + MAXCOMMITS * 4 /* bytes */)
#define CHUNKCOUNT	4
#define CHUNKSIZE	((BRICKCOUNT * BRICKSIZE * PAYLOADSIZE) / CHUNKCOUNT)

// experimental
#define ONEBRICKBUFFER	1 // use a single (large) brick buffer; set to 0 on low mem devices
#define MORTONBRICKS	0 // store bricks in morton order to improve data locality (slower)

// constants
#define PI			3.14159265358979323846264f
#define INVPI		0.31830988618379067153777f
#define INV2PI		0.15915494309189533576888f
#define TWOPI		6.28318530717958647692528f
#define SQRT_PI_INV	0.56418958355f
#define LARGE_FLOAT	1e34f