#include "template/common.h"
#include "cl/trace.cl"
#include "cl/tools.cl"

#if ONEBRICKBUFFER == 1
#define BRICKPARAMS brick0
#else
#define BRICKPARAMS brick0, brick1, brick2, brick3
#endif

float4 render_whitted( const float2 screenPos, __constant struct RenderParams* params,
	__read_only image3d_t grid,
	__global const PAYLOAD* brick0,
#if ONEBRICKBUFFER == 0
	__global const PAYLOAD* brick1, __global const PAYLOAD* brick2, __global const PAYLOAD* brick3,
#endif
	__global float4* sky, __global const uint* blueNoise,
	__global const unsigned char* uberGrid
)
{
	// basic AA
	float3 pixel = (float3)(0);
	float dist; // TAA will use distance of the last sample; it should be the only one.
	for (int u = 0; u < AA_SAMPLES; u++) for (int v = 0; v < AA_SAMPLES; v++)
	{
		// trace primary ray
		uint side = 0;
		const float3 D = GenerateCameraRay( screenPos + (float2)((float)u * (1.0f / AA_SAMPLES), (float)v * (1.0f / AA_SAMPLES)), params );
		const uint voxel = TraceRay( (float4)(params->E, 0), (float4)(D, 1), &dist, &side, grid, uberGrid, BRICKPARAMS, 999999 /* no cap needed */ );
		// simple hardcoded directional lighting using arbitrary unit vector
		if (voxel == 0) return (float4)(SampleSky( (float3)(D.x, D.z, D.y), sky, params->skyWidth, params->skyHeight ), 1e20f);
		{	// scope limiting
			const float3 BRDF1 = INVPI * ToFloatRGB( voxel );
			float4 sky;
			if (side == 0) sky = params->skyLight[D.x > 0 ? 0 : 1];
			if (side == 1) sky = params->skyLight[D.y > 0 ? 2 : 3];
			if (side == 2) sky = params->skyLight[D.z > 0 ? 4 : 5];
			pixel += BRDF1 * params->skyLightScale * sky.xyz;
		}
	}
	return (float4)(pixel * (1.0f / (AA_SAMPLES * AA_SAMPLES)), dist);
}

float4 render_gi( const float2 screenPos, __constant struct RenderParams* params,
	__read_only image3d_t grid,
	__global const PAYLOAD* brick0,
#if ONEBRICKBUFFER == 0
	__global const PAYLOAD* brick1, __global const PAYLOAD* brick2, __global const PAYLOAD* brick3,
#endif
	__global float4* sky, __global const uint* blueNoise,
	__global const unsigned char* uberGrid
)
{
	// trace primary ray
	float dist;
	uint side = 0;
	const float3 D = GenerateCameraRay( screenPos, params );
	const uint voxel = TraceRay( (float4)(params->E, 0), (float4)(D, 1), &dist, &side, grid, uberGrid, BRICKPARAMS, 999999 /* no cap needed */ );
	const float skyLightScale = params->skyLightScale;
	// visualize result: simple hardcoded directional lighting using arbitrary unit vector
	if (voxel == 0) return (float4)(SampleSky( (float3)(D.x, D.z, D.y), sky, params->skyWidth, params->skyHeight ), 1e20f);
	const float3 BRDF1 = INVPI * ToFloatRGB( voxel );
	float3 incoming = (float3)(0, 0, 0);
	const int x = (int)screenPos.x, y = (int)screenPos.y;
	uint seed = WangHash( x * 171 + y * 1773 + params->R0 );
	const float4 I = (float4)(params->E + D * dist, 0);
	for (int i = 0; i < GIRAYS; i++)
	{
		const float r0 = blueNoiseSampler( blueNoise, x, y, i + GIRAYS * params->frame, 0 );
		const float r1 = blueNoiseSampler( blueNoise, x, y, i + GIRAYS * params->frame, 1 );
		const float3 N = VoxelNormal( side, D );
		const float4 R = (float4)(DiffuseReflectionCosWeighted( r0, r1, N ), 1);
		uint side2;
		float dist2;
		const uint voxel2 = TraceRay( I + 0.1f * (float4)(N, 0), R, &dist2, &side2, grid, uberGrid, BRICKPARAMS, GRIDWIDTH / 12 );
		const float3 N2 = VoxelNormal( side2, R.xyz );
		if (0 /* for comparing against ground truth */) // get_global_id( 0 ) % SCRWIDTH < SCRWIDTH / 2)
		{
			if (voxel2 == 0) incoming += skyLightScale * SampleSky( (float3)(R.x, R.z, R.y), sky, params->skyWidth, params->skyHeight ); else /* secondary hit */
			{
				const float4 R2 = (float4)(DiffuseReflectionCosWeighted( r0, r1, N2 ), 1);
				incoming += INVPI * ToFloatRGB( voxel2 ) * skyLightScale * SampleSky( (float3)(R2.x, R2.z, R2.y), sky, params->skyWidth, params->skyHeight );
			}
		}
		else
		{
			float3 toAdd = (float3)skyLightScale, M = N;
			if (voxel2 != 0) toAdd *= INVPI * ToFloatRGB( voxel2 ), M = N2;
			float4 sky;
			if (M.x < -0.9f) sky = params->skyLight[0];
			if (M.x > 0.9f) sky = params->skyLight[1];
			if (M.y < -0.9f) sky = params->skyLight[2];
			if (M.y > 0.9f) sky = params->skyLight[3];
			if (M.z < -0.9f) sky = params->skyLight[4];
			if (M.z > 0.9f) sky = params->skyLight[5];
			incoming += toAdd * sky.xyz;
		}
	}
	return (float4)(BRDF1 * incoming * (1.0f / GIRAYS), dist);
}

// renderTAA: main rendering entry point. Forwards the request to either a basic
// renderer or a path tracer with basic indirect light. 'NoTAA' version below.
__kernel void renderTAA( __global float4* frame, __constant struct RenderParams* params,
	__read_only image3d_t grid, __global float4* sky, __global const uint* blueNoise,
	__global const unsigned char* uberGrid, __global const PAYLOAD* brick0
#if ONEBRICKBUFFER == 0
	, __global const PAYLOAD* brick1, __global const PAYLOAD* brick2, __global const PAYLOAD* brick3
#endif
)
{
	// produce primary ray for pixel
	const int x = get_global_id( 0 ), y = get_global_id( 1 );
#if GIRAYS == 0
	float4 pixel = render_whitted( (float2)(x, y), params, grid, BRICKPARAMS, sky, blueNoise, uberGrid );
#else
	float4 pixel = render_gi( (float2)(x, y), params, grid, BRICKPARAMS, sky, blueNoise, uberGrid );
#endif
	// store pixel in linear color space, to be processed by finalize kernel for TAA
	frame[x + y * SCRWIDTH] = pixel; // depth in w
}

// renderNoTAA: main rendering entry point. Forwards the request to either a basic
// renderer or a path tracer with basic indirect light. Version without TAA.
__kernel void renderNoTAA( write_only image2d_t outimg, __constant struct RenderParams* params,
	__read_only image3d_t grid, __global float4* sky, __global const uint* blueNoise,
	__global const unsigned char* uberGrid, __global const PAYLOAD* brick0
#if ONEBRICKBUFFER == 0
	, __global const PAYLOAD* brick1, __global const PAYLOAD* brick2, __global const PAYLOAD* brick3
#endif
)
{
	// produce primary ray for pixel
	const int x = get_global_id( 0 ), y = get_global_id( 1 );
#if GIRAYS == 0
	float4 pixel = render_whitted( (float2)(x, y), params, grid, BRICKPARAMS, sky, blueNoise, uberGrid );
#else
	float4 pixel = render_gi( (float2)(x, y), params, grid, BRICKPARAMS, sky, blueNoise, uberGrid );
#endif
	// finalize pixel
	write_imagef( outimg, (int2)(x, y), (float4)(LinearToSRGB( ToneMapFilmic_Hejl2015( pixel.xyz, 1 ) ), 1) );
}

// finalize: implementation of the Temporal Anti Aliasing algorithm.
__kernel void finalize( __global float4* prevFrameIn, __global float4* prevFrameOut,
	__global float4* frame, __constant struct RenderParams* params )
{
	const int x = get_global_id( 0 );
	const int y = get_global_id( 1 );
	// get history and current frame pixel - calculate u_prev, v_prev
	const float4 pixelData = frame[x + y * SCRWIDTH];
	const float2 uv = (float2)((float)x / (float)SCRWIDTH, (float)y / (float)SCRHEIGHT);
	float3 pixelPos = params->p0 + (params->p1 - params->p0) * uv.x + (params->p2 - params->p0) * uv.y;
	const float3 D = normalize( pixelPos - params->E );
	pixelPos = params->E + pixelData.w * D;
	const float dl = dot( pixelPos, params->Nleft.xyz ) - params->Nleft.w;
	const float dr = dot( pixelPos, params->Nright.xyz ) - params->Nright.w;
	const float dt = dot( pixelPos, params->Ntop.xyz ) - params->Ntop.w;
	const float db = dot( pixelPos, params->Nbottom.xyz ) - params->Nbottom.w;
	const float u_prev = SCRWIDTH * (dl / (dl + dr));
	const float v_prev = SCRHEIGHT * (dt / (dt + db));
	float3 hist = bilerpSample( prevFrameIn, u_prev, v_prev );
	const float3 pixel = RGBToYCoCg( pixelData.xyz );
	// determine color neighborhood
	int x1 = max( 0, x - 1 ), y1 = max( 0, y - 1 ), x2 = min( SCRWIDTH - 1, x + 1 ), y2 = min( SCRHEIGHT - 1, y + 1 );
	// advanced neighborhood determination
	float3 colorAvg = (float3)0;
	float3 colorVar = colorAvg;
	for (int y = y1; y <= y2; y++) for (int x = x1; x <= x2; x++)
	{
		const float3 p = RGBToYCoCg( frame[x + y * SCRWIDTH].xyz );
		colorAvg += p, colorVar += p * p;
	}
	colorAvg *= 1.0f / 9.0f;
	colorVar *= 1.0f / 9.0f;
	float3 sigma = max( (float3)0, colorVar - (colorAvg * colorAvg) );
	sigma.x = sqrt( sigma.x );
	sigma.y = sqrt( sigma.y );
	sigma.z = sqrt( sigma.z );
	const float3 minColor = colorAvg - 1.25f * sigma;
	const float3 maxColor = colorAvg + 1.25f * sigma;
	hist = clamp( RGBToYCoCg( hist ), minColor, maxColor );
	// final blend
	const float3 color = YCoCgToRGB( 0.9f * hist + 0.1f * pixel );
	prevFrameOut[x + y * SCRWIDTH] = (float4)(color, 1);
}

// unsharpen: this kernel finalizes the output of the TAA kernel. Unsharpen is
// applied because TAA tends to blur the screen; unsharpen (despite its name) remedies this.
__kernel void unsharpen( write_only image2d_t outimg, __global float4* pixels )
{
	const int x = get_global_id( 0 );
	const int y = get_global_id( 1 );
	if (x == 0 || y == 0 || x >= SCRWIDTH - 1 || y >= SCRHEIGHT - 1) return;
	const float4 p0 = pixels[x - 1 + (y - 1) * SCRWIDTH];
	const float4 p1 = pixels[x + (y - 1) * SCRWIDTH];
	const float4 p2 = pixels[x + 1 + (y - 1) * SCRWIDTH];
	const float4 p3 = pixels[x + 1 + y * SCRWIDTH];
	const float4 p4 = pixels[x + 1 + (y + 1) * SCRWIDTH];
	const float4 p5 = pixels[x + (y + 1) * SCRWIDTH];
	const float4 p6 = pixels[x - 1 + (y + 1) * SCRWIDTH];
	const float4 p7 = pixels[x - 1 + y * SCRWIDTH];
	const float4 color = max( pixels[x + y * SCRWIDTH], pixels[x + y * SCRWIDTH] *
		2.7f - 0.5f * (0.35f * p0 + 0.5f * p1 + 0.35f * p2 +
			0.5f * p3 + 0.35f * p4 + 0.5f * p5 + 0.35f * p6 + 0.5f * p7) );
	write_imagef( outimg, (int2)(x, y), (float4)(LinearToSRGB( ToneMapFilmic_Hejl2015( color.xyz, 1 ) ), 1) );
}

// traceBatch: trace a batch of rays to the first non-empty voxel.
__kernel void traceBatch(
	__read_only image3d_t grid,
	__global const PAYLOAD* brick0, __global const PAYLOAD* brick1,
	__global const PAYLOAD* brick2, __global const PAYLOAD* brick3,
	const int batchSize, __global const float4* rayData, __global uint* hitData,
	__global const unsigned char* uberGrid
)
{
	// sanity check
	const uint taskId = get_global_id( 0 );
	if (taskId >= batchSize) return;
	// fetch ray from buffer
	const float4 O4 = rayData[taskId * 2 + 0];
	const float4 D4 = rayData[taskId * 2 + 1];
	// trace ray
	float3 N;
	float dist;
	const uint voxel = TraceRay( (float4)(O4.x, O4.y, O4.z, 0), (float4)(D4.x, D4.y, D4.z, 1),
		&dist, &N, grid, uberGrid, BRICKPARAMS, 999999 );
	// store query result
	hitData[taskId * 2 + 0] = as_uint( dist < O4.w ? dist : 1e34f );
	uint Nval = ((int)N.x + 1) + (((int)N.y + 1) << 2) + (((int)N.z + 1) << 4);
	hitData[taskId * 2 + 1] = (voxel == 0 ? 0 : Nval) + (voxel << 16);
}

// traceBatchToVoid: trace a batch of rays to the first empty voxel.
// TODO: probably broken; needs to be synchronized witht the regular traversal kernel.
__kernel void traceBatchToVoid(
	__read_only image3d_t grid,
	__global const PAYLOAD* brick0, __global const PAYLOAD* brick1,
	__global const PAYLOAD* brick2, __global const PAYLOAD* brick3,
	const int batchSize, __global const float4* rayData, __global uint* hitData,
	__global const unsigned char* uberGrid
)
{
	// sanity check
	const uint taskId = get_global_id( 0 );
	if (taskId >= batchSize) return;
	// fetch ray from buffer
	const float4 O4 = rayData[taskId * 2 + 0];
	const float4 D4 = rayData[taskId * 2 + 1];
	// trace ray
	float3 N;
	float dist;
	TraceRayToVoid( (float4)(O4.x, O4.y, O4.z, 0), (float4)(D4.x, D4.y, D4.z, 1),
		&dist, &N, grid, BRICKPARAMS, uberGrid );
	// store query result
	hitData[taskId * 2 + 0] = as_uint( dist < O4.w ? dist : 1e34f );
	uint Nval = ((int)N.x + 1) + (((int)N.y + 1) << 2) + (((int)N.z + 1) << 4);
	hitData[taskId * 2 + 1] = Nval;
}

// commit: this kernel moves changed bricks which have been transfered to the on-device
// staging buffer to their final location.
__kernel void commit( const int taskCount, __global uint* staging,
	__global uint* brick0, __global uint* brick1, __global uint* brick2, __global uint* brick3 )
{
	// put bricks in place
	int task = get_global_id( 0 );
	if (task < taskCount)
	{
	#if ONEBRICKBUFFER == 0
		__global uint* bricks[4] = { brick0, brick1, brick2, brick3 };
	#endif
		int brickId = staging[task + GRIDSIZE];
		__global uint* src = staging + MAXCOMMITS + GRIDSIZE + task * (BRICKSIZE * PAYLOADSIZE) / 4;
		const uint offset = brickId * BRICKSIZE * PAYLOADSIZE / 4; // in dwords
	#if ONEBRICKBUFFER == 1
	#if MORTONBRICKS == 1
		__global PAYLOAD* dst = (__global PAYLOAD*)(brick0 + offset);
		for (int z = 0; z < 8; z++) for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
			dst[Morton3Bit( x, y, z )] = ((__global PAYLOAD*)src)[x + y * 8 + z * 64];
	#else
		for (int i = 0; i < (BRICKSIZE * PAYLOADSIZE) / 4; i++) brick0[offset + i] = src[i];
	#endif
	#else
		__global uint* page = bricks[(offset / (CHUNKSIZE / 4)) & 3];
		for (int i = 0; i < (BRICKSIZE * PAYLOADSIZE) / 4; i++) page[(offset & (CHUNKSIZE / 4 - 1)) + i] = src[i];
	#endif
	}
}

#if MORTONBRICKS == 1
// encodeBricks: this kernel shuffles the voxels in a brick to Morton order.
// On NVidia, despite more coherent memory access, this is significantly slower.
__kernel void encodeBricks( const int taskCount, __global PAYLOAD* brick )
{
	// change brick layout to morton order
	int task = get_global_id( 0 );
	if (task < taskCount)
	{
		PAYLOAD tmp[512];
		for (int i = 0; i < 512; i++) tmp[i] = brick[task * 512 + i];
		for (int z = 0; z < 8; z++) for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
			brick[task * 512 + Morton3Bit( x, y, z )] = tmp[x + y * 8 + z * 64];
	}
}
#endif

// updateUberGrid: this kernel creates the 32x32x32 'ubergrid', which contains a '0' for
// a group of empty 4x4x4 bricks; '1' otherwise.
__kernel void updateUberGrid( const __global unsigned int* grid, __global unsigned char* uber )
{
	const int task = get_global_id( 0 );
	const int x = task & (UBERWIDTH - 1);
	const int z = (task / UBERWIDTH) & (UBERDEPTH - 1);
	const int y = (task / (UBERWIDTH * UBERHEIGHT)) & (UBERHEIGHT - 1);
	// check grid
	bool empty = true;
	for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++) for (int c = 0; c < 4; c++)
	{
		const int gx = x * 4 + a, gy = y * 4 + b, gz = z * 4 + c;
		if (grid[gx + gz * GRIDWIDTH + gy * GRIDWIDTH * GRIDHEIGHT]) { empty = false; break; }
	}
	// write result
	uber[x + z * UBERWIDTH + y * UBERWIDTH * UBERHEIGHT] = empty ? 0 : 1;
}