#include "template/common.h"
#include "cl/trace.h"
#include "cl/tools.h"

float3 render_whitted( const float2 screenPos, __constant struct RenderParams* params, __read_only image3d_t grid,
	__global const unsigned char* brick0, __global const unsigned char* brick1,
	__global const unsigned char* brick2, __global const unsigned char* brick3, __global float4* sky, __global const uint* blueNoise )
{
	// basic AA
	float3 pixel  = (float3)(0);
	for( int u = 0; u < 3; u++ ) for( int v = 0; v < 3; v++ )
	{
		// trace primary ray
		float dist;
		float3 N;
		const float3 D = GenerateCameraRay( screenPos + (float2)((float)u * 0.333f, (float)v * 0.333f), params );
		const uint voxel = TraceRay( (float4)(params->E, 1), (float4)(D, 1), &dist, &N, grid, brick0, brick1, brick2, brick3, 999999 /* no cap needed */ );
		// simple hardcoded directional lighting using arbitrary unit vector
		if (voxel == 0) return SampleSky( (float3)(D.x, D.z, D.y), sky );
		const float3 BRDF1 = INVPI * ToFloatRGB( voxel );
		pixel += BRDF1 * 1.6f * (1.0f + 0.7f * dot( N, (float3)(0.2357f, 0.9428f, 0.2357f) ) );
	}
	return pixel * (1.0f / 9.0f);
}

float3 render_gi( const float2 screenPos, __constant struct RenderParams* params, __read_only image3d_t grid,
	__global const unsigned char* brick0, __global const unsigned char* brick1,
	__global const unsigned char* brick2, __global const unsigned char* brick3, __global float4* sky, __global const uint* blueNoise )
{
	// trace primary ray
	float dist;
	float3 N;
	const float3 D = GenerateCameraRay( screenPos, params );
	const uint voxel = TraceRay( (float4)(params->E, 1), (float4)(D, 1), &dist, &N, grid, brick0, brick1, brick2, brick3, 999999 /* no cap needed */ );

	// visualize result: simple hardcoded directional lighting using arbitrary unit vector
	if (voxel == 0) return SampleSky( (float3)(D.x, D.z, D.y), sky );
	const float3 BRDF1 = INVPI * ToFloatRGB( voxel );
	float3 incoming = (float3)(0, 0, 0);
	const int x = (int)screenPos.x, y = (int)screenPos.y;
	uint seed = WangHash( x * 171 + y * 1773 + params->R0 );
	const float4 I = (float4)(params->E + D * dist, 1);
	for (int i = 0; i < GIRAYS; i++)
	{
		const float r0 = blueNoiseSampler( blueNoise, x, y, i + GIRAYS * params->frame, 0 );
		const float r1 = blueNoiseSampler( blueNoise, x, y, i + GIRAYS * params->frame, 1 );
		const float4 R = (float4)(DiffuseReflectionCosWeighted( r0, r1, N ), 1);
		float3 N2;
		float dist2;
		const uint voxel2 = TraceRay( I + 0.1f * (float4)(N, 1), R, &dist2, &N2, grid, brick0, brick1, brick2, brick3, GRIDWIDTH / 12 /* cap on GI ray length */ );
		if (voxel2 == 0) incoming += 8 * SampleSky( (float)(R.x, R.z, R.y), sky ); else /* secondary hit */
			incoming += INVPI * ToFloatRGB( voxel2 ) * 1.6f * (1.0f + 0.7f * dot( N, (float3)(0.2357f, 0.9428f, 0.2357f) ) );
	}
	return BRDF1 * incoming * (1.0f / GIRAYS);
}

__kernel void render( write_only image2d_t outimg, __constant struct RenderParams* params, __read_only image3d_t grid,
	__global const unsigned char* brick0, __global const unsigned char* brick1,
	__global const unsigned char* brick2, __global const unsigned char* brick3, __global float4* sky, __global const uint* blueNoise )
{
	// produce primary ray for pixel
	const int x = get_global_id( 0 );
	const int y = get_global_id( 1 );
#if GIRAYS == 0
	const float3 pixel = render_whitted( (float2)(x,y), params, grid, brick0, brick1, brick2, brick3, sky, blueNoise );
#else
	const float3 pixel = render_gi( (float2)(x,y), params, grid, brick0, brick1, brick2, brick3, sky, blueNoise );
#endif
	write_imagef( outimg, (int2)(x, y), (float4)(pixel, 1) );
}

__kernel void commit( const int taskCount, __global uint* commit,
	__global uint* brick0, __global uint* brick1, __global uint* brick2, __global uint* brick3 )
{
	// put bricks in place
	int task = get_global_id( 0 );
	if (task < taskCount)
	{
		__global uint* bricks[4] = { brick0, brick1, brick2, brick3 };
		int brickId = commit[task + GRIDSIZE];
		__global uint* src = commit + MAXCOMMITS + GRIDSIZE + task * (BRICKSIZE * PAYLOADSIZE) / 4;
		const uint offset = brickId * BRICKSIZE * PAYLOADSIZE / 4; // in dwords
		uint* page = bricks[(offset / (CHUNKSIZE / 4)) & 3];
		for (int i = 0; i < (BRICKSIZE * PAYLOADSIZE) / 4; i++) page[(offset & (CHUNKSIZE / 4 - 1)) + i] = src[i];
	}
}