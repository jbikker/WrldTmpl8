#include "template/common.h"
#include "cl/trace.h"
#include "cl/tools.h"

__kernel void render( write_only image2d_t outimg, __constant struct RenderParams* params, __read_only image3d_t grid,
	__global const unsigned char* brick0, __global const unsigned char* brick1,
	__global const unsigned char* brick2, __global const unsigned char* brick3, __global float4* sky, __global const uint* blueNoise )
{
	// produce primary ray for pixel
	const int column = get_global_id( 0 );
	const int line = get_global_id( 1 );
	const float2 uv = (float2)((float)column * params->oneOverRes.x, (float)line * params->oneOverRes.y);
#if PANINI
	const float3 V = PaniniProjection( (float2)(uv.x * 2 - 1, (uv.y * 2 - 1) * ((float)SCRHEIGHT / SCRWIDTH)), PI / 5, 0.15f );
	// multiply by improvised camera matrix
	const float3 D = V.z * normalize( (params->p1 + params->p2) * 0.5f - params->E ) +
		V.x * normalize( params->p1 - params->p0 ) +
		V.y * normalize( params->p2 - params->p0 );
#else
	const float3 P = params->p0 + (params->p1 - params->p0) * uv.x + (params->p2 - params->p0) * uv.y;
	const float3 D = normalize( P - params->E );
#endif

	// trace primary ray
	float dist;
	float3 N;
	const uint voxel = TraceRay( (float4)(params->E, 1), (float4)(D, 1), &dist, &N, grid, brick0, brick1, brick2, brick3, 999999 /* no cap needed */ );

	// visualize result
	float3 pixel;
	if (voxel == 0)
	{
		// sky
		const float3 T = (float3)(D.x, D.z, D.y);
		const float u = 5000 * SphericalPhi( T ) * INV2PI - 0.5f;
		const float v = 2500 * SphericalTheta( T ) * INVPI - 0.5f;
		const float fu = u - floor( u ), fv = v - floor( v );
		const int iu = (int)u, iv = (int)v;
		const uint idx1 = (iu + iv * 5000) % (5000 * 2500);
		const uint idx2 = (iu + 1 + iv * 5000) % (5000 * 2500);
		const uint idx3 = (iu + (iv + 1) * 5000) % (5000 * 2500);
		const uint idx4 = (iu + 1 + (iv + 1) * 5000) % (5000 * 2500);
		const float4 s =
			sky[idx1] * (1 - fu) * (1 - fv) + sky[idx2] * fu * (1 - fv) +
			sky[idx3] * (1 - fu) * fv + sky[idx4] * fu * fv;
		pixel = s.xyz;
	}
	else
	{
	#if PAYLOADSIZE == 1
		const float3 BRDF1 = INVPI * (float3)((voxel >> 5) * (1.0f / 7.0f), ((voxel >> 2) & 7) * (1.0f / 7.0f), (voxel & 3) * (1.0f / 3.0f));
	#else
		const float3 BRDF1 = INVPI * (float3)(((voxel >> 8) & 15) * (1.0f / 15.0f), ((voxel >> 4) & 15) * (1.0f / 15.0f), (voxel & 15) * (1.0f / 15.0f));
	#endif
	#if GIRAYS > 0
		float3 incoming = (float3)(0, 0, 0);
		uint seed = WangHash( column * 171 + line * 1773 + params->R0 );
		const float4 I = (float4)(params->E + D * dist, 1);
		for (int i = 0; i < GIRAYS; i++)
		{
			const float r0 = blueNoiseSampler( blueNoise, column, line, i + GIRAYS * params->frame, 0 );
			const float r1 = blueNoiseSampler( blueNoise, column, line, i + GIRAYS * params->frame, 1 );
			const float4 R = (float4)(DiffuseReflectionCosWeighted( r0, r1, N ), 1);
			float3 N2;
			float dist2;
			const uint voxel2 = TraceRay( I + 0.1f * (float4)(N, 1), R, &dist2, &N2, grid, brick0, brick1, brick2, brick3, GRIDWIDTH / 12 /* cap on GI ray length */ );
			if (voxel2 == 0)
			{
				// sky
				const float3 T = (float3)(R.x, R.z, R.y);
				const float u = 5000 * SphericalPhi( T ) * INV2PI - 0.5f;
				const float v = 2500 * SphericalTheta( T ) * INVPI - 0.5f;
				const float fu = u - floor( u ), fv = v - floor( v );
				const uint idx1 = (u + v * 5000) % (5000 * 2500);
				const uint idx2 = (u + 1 + v * 5000) % (5000 * 2500);
				const uint idx3 = (u + (v + 1) * 5000) % (5000 * 2500);
				const uint idx4 = (u + 1 + (v + 1) * 5000) % (5000 * 2500);
				const float4 s =
					sky[idx1] * (1 - fu) * (1 - fv) + sky[idx2] * fu * (1 - fv) +
					sky[idx3] * (1 - fu) * fv + sky[idx4] * fu * fv;
				incoming += 4 * s.xyz;
			}
			else
			{
				float3 BRDF2 = INVPI * (float3)((voxel2 >> 5) * (1.0f / 7.0f), ((voxel2 >> 2) & 7) * (1.0f / 7.0f), (voxel2 & 3) * (1.0f / 3.0f));
				// secondary hit
				incoming += BRDF2 * 2 * (
					(N2.x * N2.x) * ((-N2.x + 1) * (float3)(NX0) + (N2.x + 1) * (float3)(NX1)) +
					(N2.y * N2.y) * ((-N2.y + 1) * (float3)(NY0) + (N2.y + 1) * (float3)(NY1)) +
					(N2.z * N2.z) * ((-N2.z + 1) * (float3)(NZ0) + (N2.z + 1) * (float3)(NZ1))
				);
			}
		}
		pixel = BRDF1 * incoming * (1.0f / GIRAYS);
	#else
		// hardcoded lights - image based lighting, no visibility test
	#if 1
		pixel = BRDF1 * 1.6f * (1.0f + 0.7f * dot( N, normalize( (float3)(0.5f, 2.0f, 0.5f) ) ));
	#else
		pixel = BRDF1 * 2.5f * (
			(N.x * N.x) * ((-N.x + 1) * (float3)(NX0) + (N.x + 1) * (float3)(NX1)) +
			(N.y * N.y) * ((-N.y + 1) * (float3)(NY0) + (N.y + 1) * (float3)(NY1)) +
			(N.z * N.z) * ((-N.z + 1) * (float3)(NZ0) + (N.z + 1) * (float3)(NZ1))
		);
	#endif
	#endif
	}
	write_imagef( outimg, (int2)(column, line), (float4)(pixel, 1) );
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