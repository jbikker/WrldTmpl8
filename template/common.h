// default screen resolution
#define SCRWIDTH	1280
#define SCRHEIGHT	720

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

// renderer performance setting: set to 0 for slower devices, up to 8 for fast GPUs
#define GIRAYS		4

// Panini projection, http://tksharpless.net/vedutismo/Pannini/panini.pdf via https://www.shadertoy.com/view/Wt3fzB
#define PANINI		1

// Set to CPUONLY to 1 to force cpu-only rendering (UNDER CONSTRUCTION)
#define CPUONLY		0

// hardware
// #define CPU_HAS_BMI2		// cpu supports BMI2 instructions; Haswell (2013) / AMD Excavator (2015)

// some useful color names
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

// renderer
struct RenderParams
{
	float2 oneOverRes;
	float3 E, p0, p1, p2;
	uint R0, frame;
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
#define BRICKCOUNT	((MAPWIDTH / BRICKDIM) * (MAPHEIGHT / BRICKDIM) * (MAPDEPTH / BRICKDIM))
#define BRICKCOMMITSIZE	(MAXCOMMITS * BRICKSIZE + MAXCOMMITS * 4 /* bytes */)

// constants
#define PI			3.14159265358979323846264f
#define INVPI		0.31830988618379067153777f
#define INV2PI		0.15915494309189533576888f
#define TWOPI		6.28318530717958647692528f
#define SQRT_PI_INV	0.56418958355f
#define LARGE_FLOAT	1e34f