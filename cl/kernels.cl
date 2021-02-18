#include "template/common.h"

// internal stuff
#define dot3(A,B)	A.x*B.x+A.y*B.y+A.z*B.z		// used in case A or B is stored in a vec4
#define OFFS_X		((bits >> 5) & 1)			// extract grid plane offset over x (0 or 1)
#define OFFS_Y		((bits >> 13) & 1)			// extract grid plane offset over y (0 or 1)
#define OFFS_Z		(bits >> 21)				// extract grid plane offset over z (0 or 1)
#define DIR_X		((bits & 3) - 1)			// ray dir over x (-1 or 1)
#define DIR_Y		(((bits >> 8) & 3) - 1)		// ray dir over y (-1 or 1)
#define DIR_Z		(((bits >> 16) & 3) - 1)	// ray dir over z (-1 or 1)
#define EPS			1e-8
#define BMSK		(BRICKDIM - 1)
#define BDIM2		(BRICKDIM * BRICKDIM)
#define BPMX		(MAPWIDTH - BRICKDIM)
#define BPMY		(MAPHEIGHT - BRICKDIM)
#define BPMZ		(MAPDEPTH - BRICKDIM)
#define TOPMASK3	(((1023 - BMSK) << 20) + ((1023 - BMSK) << 10) + (1023 - BMSK))

// fix ray directions that are too close to 0
float4 FixZeroDeltas( float4 V )
{
	if (fabs( V.x ) < EPS) V.x = V.x < 0 ? -EPS : EPS;
	if (fabs( V.y ) < EPS) V.y = V.y < 0 ? -EPS : EPS;
	if (fabs( V.z ) < EPS) V.z = V.z < 0 ? -EPS : EPS;
	return V;
}

// mighty two-level grid traversal
uint TraceRay( float4 A, const float4 B, float* dist, float3* N, __read_only image3d_t grid, __global const unsigned char* brick, int steps )
{
	const float4 V = FixZeroDeltas( B ), rV = (float4)(1.0 / V.x, 1.0 / V.y, 1.0 / V.z, 1);
	const bool originOutsideGrid = A.x < 0 || A.y < 0 || A.z < 0 || A.x > MAPWIDTH || A.y > MAPHEIGHT || A.z > MAPDEPTH;
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
	}
	uint4 pos = (uint4)(clamp( (int)A.x, 0, MAPWIDTH - 1 ), clamp( (int)A.y, 0, MAPHEIGHT - 1 ), clamp( (int)A.z, 0, MAPDEPTH - 1 ), 0);
	const int bits = select( 4, 34, V.x > 0 ) + select( 3072, 10752, V.y > 0 ) + select( 1310720, 3276800, V.z > 0 ); // magic
	float tmx = ((pos.x & BPMX) + ((bits >> (5 - BDIMLOG2)) & (1 << BDIMLOG2)) - A.x) * rV.x;
	float tmy = ((pos.y & BPMY) + ((bits >> (13 - BDIMLOG2)) & (1 << BDIMLOG2)) - A.y) * rV.y;
	float tmz = ((pos.z & BPMZ) + ((bits >> (21 - BDIMLOG2)) & (1 << BDIMLOG2)) - A.z) * rV.z, t = 0;
	const float tdx = DIR_X * rV.x, tdy = DIR_Y * rV.y, tdz = DIR_Z * rV.z;
	uint last = 0;
	while (true)
	{
		// check main grid
		const uint o = read_imageui( grid, (int4)(pos.x / BRICKDIM, pos.z / BRICKDIM, pos.y / BRICKDIM, 0) ).x;
		if (o != 0) if ((o & 1) == 0) /* solid */
		{
			*dist = t, * N = -(float3)((last == 0) * DIR_X, (last == 1) * DIR_Y, (last == 2) * DIR_Z);
			return o >> 1;
		}
		else // brick
		{
			const float4 I = A + V * t;
			uint p = (clamp( (uint)I.x, pos.x & BPMX, (pos.x & BPMX) + BMSK ) << 20) +
					 (clamp( (uint)I.y, pos.y & BPMY, (pos.y & BPMY) + BMSK ) << 10) +
					  clamp( (uint)I.z, pos.z & BPMZ, (pos.z & BPMZ) + BMSK );
			const uint pn = p & TOPMASK3;
			float dmx = (float)((p >> 20) + OFFS_X - A.x) * rV.x;
			float dmy = (float)(((p >> 10) & 1023) + OFFS_Y - A.y) * rV.y;
			float dmz = (float)((p & 1023) + OFFS_Z - A.z) * rV.z, d = t;
			do
			{
				const uint idx = (o >> 1) * BRICKSIZE + ((p >> 20) & BMSK) + ((p >> 10) & BMSK) * BRICKDIM + (p & BMSK) * BDIM2;
				const unsigned int color = brick[idx];
				if (color != 0U)
				{
					*dist = d, * N = -(float3)((last == 0) * DIR_X, (last == 1) * DIR_Y, (last == 2) * DIR_Z);
					return color;
				}
				d = min( dmx, min( dmy, dmz ) );
				if (d == dmx) dmx += tdx, p += DIR_X << 20, last = 0;
				if (d == dmy) dmy += tdy, p += DIR_Y << 10, last = 1;
				if (d == dmz) dmz += tdz, p += DIR_Z, last = 2;
			} while ((p & TOPMASK3) == pn);
		}
		if (!--steps) break;
		t = min( tmx, min( tmy, tmz ) );
		if (t == tmx) tmx += tdx * BRICKDIM, pos.x += DIR_X * BRICKDIM, last = 0;
		if (t == tmy) tmy += tdy * BRICKDIM, pos.y += DIR_Y * BRICKDIM, last = 1;
		if (t == tmz) tmz += tdz * BRICKDIM, pos.z += DIR_Z * BRICKDIM, last = 2;
		if ((pos.x & (65536 - MAPWIDTH)) + (pos.y & (65536 - MAPWIDTH)) + (pos.z & (65536 - MAPWIDTH))) break;
	}
	return 0U;
}

float SphericalTheta( const float3 v )
{
	return acos( clamp( v.z, -1.f, 1.f ) );
}

float SphericalPhi( const float3 v )
{
	const float p = atan2( v.y, v.x );
	return (p < 0) ? (p + 2 * PI) : p;
}

uint WangHash( uint s ) { s = (s ^ 61) ^ (s >> 16), s *= 9, s = s ^ (s >> 4), s *= 0x27d4eb2d, s = s ^ (s >> 15); return s; }
uint RandomInt( uint* s ) { *s ^= *s << 13, * s ^= *s >> 17, * s ^= *s << 5; return *s; }
float RandomFloat( uint* s ) { return RandomInt( s ) * 2.3283064365387e-10f; }

float3 DiffuseReflectionCosWeighted( const float r0, const float r1, const float3 N )
{
	const float3 T = normalize( cross( N, fabs( N.y ) > 0.99f ? (float3)(1, 0, 0) : (float3)(0, 1, 0) ) );
	const float3 B = cross( T, N );
	const float term1 = TWOPI * r0, term2 = sqrt( 1 - r1 );
	float c, s = sincos( term1, &c );
	return (c * term2 * T) + (s * term2) * B + sqrt( r1 ) * N;
}

float blueNoiseSampler( const __global uint* blueNoise, int x, int y, int sampleIndex, int sampleDimension )
{
	// Adapated from E. Heitz. Arguments:
	// sampleIndex: 0..255
	// sampleDimension: 0..255
	x &= 127, y &= 127, sampleIndex &= 255, sampleDimension &= 255;
	// xor index based on optimized ranking
	int rankedSampleIndex = (sampleIndex ^ blueNoise[sampleDimension + (x + y * 128) * 8 + 65536 * 3]) & 255;
	// fetch value in sequence
	int value = blueNoise[sampleDimension + rankedSampleIndex * 256];
	// if the dimension is optimized, xor sequence value based on optimized scrambling
	value ^= blueNoise[(sampleDimension & 7) + (x + y * 128) * 8 + 65536];
	// convert to float and return
	float retVal = (0.5f + value) * (1.0f / 256.0f) /* + noiseShift (see LH2) */;
	if (retVal >= 1) retVal -= 1;
	return retVal;
}

// tc ∈ [-1,1]² | fov ∈ [0, π) | d ∈ [0,1] -  via https://www.shadertoy.com/view/tt3BRS
float3 PaniniProjection( float2 tc, const float fov, const float d )
{
	const float d2 = d * d;
	{
		const float fo = PI * 0.5f - fov * 0.5f;
		const float f = cos( fo ) / sin( fo ) * 2.0f;
		const float f2 = f * f;
		const float b = (native_sqrt( max( 0.f, (d + d2) * (d + d2) * (f2 + f2 * f2) ) ) - (d * f + f)) / (d2 + d2 * f2 - 1);
		tc *= b;
	}
	const float h = tc.x, v = tc.y, h2 = h * h;
	const float k = h2 / ((d + 1) * (d + 1)), k2 = k * k;
	const float discr = max( 0.f, k2 * d2 - (k + 1) * (k * d2 - 1) );
	const float cosPhi = (-k * d + native_sqrt( discr )) / (k + 1.f);
	const float S = (d + 1) / (d + cosPhi), tanTheta = v / S;
	float sinPhi = native_sqrt( max( 0.f, 1 - cosPhi * cosPhi ) );
	if (tc.x < 0.0) sinPhi *= -1;
	const float s = native_rsqrt( 1 + tanTheta * tanTheta );
	return (float3)(sinPhi, tanTheta, cosPhi) * s;
}

__kernel void render( write_only image2d_t outimg, __constant struct RenderParams* params,
	__read_only image3d_t grid, __global unsigned char* brick, __global float4* sky, __global const uint* blueNoise )
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
	const uint voxel = TraceRay( (float4)(params->E, 1), (float4)(D, 1), &dist, &N, grid, brick, 999999 /* no cap needed */ );

	// visualize result
	float3 pixel;
	if (voxel == 0)
	{
		// sky
		const float3 T = (float3)(D.x, D.z, D.y);
		const uint u = (uint)(5000 * SphericalPhi( T ) * INV2PI - 0.5f);
		const uint v = (uint)(2500 * SphericalTheta( T ) * INVPI - 0.5f);
		const uint idx = u + v * 5000;
		pixel = (idx < 5000 * 2500) ? sky[idx].xyz : (float3)(1);
	}
	else
	{
		const float3 BRDF1 = INVPI * (float3)((voxel >> 5) * (1.0f / 7.0f), ((voxel >> 2) & 7) * (1.0f / 7.0f), (voxel & 3) * (1.0f / 3.0f));
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
			const uint voxel2 = TraceRay( I + 0.1f * (float4)(N, 1), R, &dist2, &N2, grid, brick, GRIDWIDTH / 12 /* cap on GI ray length */ );
			if (voxel2 == 0)
			{
				// sky
				const float3 T = (float3)(R.x, R.z, R.y);
				const uint u = (uint)(5000 * SphericalPhi( T ) * INV2PI - 0.5f);
				const uint v = (uint)(2500 * SphericalTheta( T ) * INVPI - 0.5f);
				const uint idx = u + v * 5000;
				incoming += 4 * ((idx < 5000 * 2500) ? sky[idx].xyz : (float3)(1));
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
		pixel = BRDF1 * 2 * (
			(N.x * N.x) * ((-N.x + 1) * (float3)(NX0) + (N.x + 1) * (float3)(NX1)) +
			(N.y * N.y) * ((-N.y + 1) * (float3)(NY0) + (N.y + 1) * (float3)(NY1)) +
			(N.z * N.z) * ((-N.z + 1) * (float3)(NZ0) + (N.z + 1) * (float3)(NZ1))
		);
	#endif
	}
	write_imagef( outimg, (int2)(column, line), (float4)(pixel, 1) );
}

__kernel void commit( const int taskCount, __global uint* commit, __global uint* brick )
{
	// put bricks in place
	int task = get_global_id( 0 );
	if (task < taskCount)
	{
		int brickId = commit[task + GRIDSIZE];
		__global uint* src = commit + MAXCOMMITS + GRIDSIZE + task * BRICKSIZE / 4;
		__global uint* dst = brick + brickId * BRICKSIZE / 4;
		for (int i = 0; i < BRICKSIZE / 4; i++) dst[i] = src[i];
	}
}

// Notes

// Reprojection, according to Lighthouse 2:
// float3 D = normalize( localWorldPos - prevPos );
// float3 S = prevPos + D * (prevPos.w / dot( prevE, D ));
// prevPixelPos = make_float2( dot( S, prevRight ) - prevRight.w - j0, dot( S, prevUp ) - prevUp.w - j1 );
// data:
// -----------
// PREP:
// float3 centre = 0.5f * (prevView.p2 + prevView.p3);
// float3 direction = normalize( centre - prevView.pos );
// float3 right = normalize( prevView.p2 - prevView.p1 );
// float3 up = normalize( prevView.p3 - prevView.p1 );
// float focalDistance = length( centre - prevView.pos );
// float screenSize = length( prevView.p3 - prevView.p1 );
// float lenReci = h / screenSize;
// -----------
// float4 prevPos = make_float4( prevView.pos, -(dot( prevView.pos, direction ) - dot( centre, direction )) )
// float4 prevE = make_float4( direction, 0 )
// float4 prevRight = make_float4( right * lenReci, dot( prevView.p1, right ) * lenReci )
// float4 prevUp = make_float4( up * lenReci, dot( prevView.p1, up ) * lenReci )

// Faster empty space skipping:
// bits for empty top-level grid cells didn't work.
// Try instead: https://www.kalojanov.com/data/irregular_grid.pdf
// as suggested by Guillaume Boissé.