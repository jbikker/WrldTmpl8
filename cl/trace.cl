// Optimizations / plans:
// 1. Automate finding the optimal workgroup size
// 2. Keep trying with fewer registers
// 3. Try a 1D job and turn it into tiles in the render kernel
// 4. If all threads enter the same brick, this brick can be in local mem
// 5. Create a website with a library of vox files
// 6. Have a nicer benchmark scene: planet with asteroid debris?
// 7. Trace does not respect initial t
// 8. Add occlusion ray query

// DONE:
// - Figure out how to detect 1080/2080/3080/AMD/other
// - Optimize for 2080 and 1080
// - Try some unrolling on the 2nd loop?
// - Optimize the world: combine 8x8x8 solid voxels
// - Improve readme.md
// - Add UI to obj2vox


// internal stuff
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
#define UBERMASK3	((1020 << 20) + (1020 << 10) + 1020)

// fix ray directions that are too close to 0
float4 FixZeroDeltas( float4 V )
{
	if (fabs( V.x ) < EPS) V.x = V.x < 0 ? -EPS : EPS;
	if (fabs( V.y ) < EPS) V.y = V.y < 0 ? -EPS : EPS;
	if (fabs( V.z ) < EPS) V.z = V.z < 0 ? -EPS : EPS;
	return V;
}

#if ONEBRICKBUFFER == 1

#define BRICKSTEP(exitLabel)															\
	v = o + (p >> 20) + ((p >> 7) & (BMSK * BRICKDIM)) + (p & BMSK) * BDIM2;			\
	v = brick0[v]; if (v) { *dist = t + to, * side = last; return v; }					\
	t = min( tm.x, min( tm.y, tm.z ) ), last = 0;										\
	if (t == tm.x) tm.x += td.x, p += dx;												\
	if (t == tm.y) tm.y += td.y, p += dy, last = 1;										\
	if (t == tm.z) tm.z += td.z, p += dz, last = 2;										\
	if (p & TOPMASK3) goto exitLabel;

#else

#define BRICKSTEP(exitLabel)															\
	v = o + (p >> 20) + ((p >> 7) & (BMSK * BRICKDIM)) + (p & BMSK) * BDIM2;			\
	if (p != lp) page = (__global const PAYLOAD*)bricks[v / (CHUNKSIZE / PAYLOADSIZE)], lp = p;	\
	v = page[v & ((CHUNKSIZE / PAYLOADSIZE) - 1)];										\
	if (v) { *dist = t + to, * side = last; return v; }									\
	t = min( tm.x, min( tm.y, tm.z ) ), last = 0;										\
	if (t == tm.x) tm.x += td.x, p += dx;												\
	if (t == tm.y) tm.y += td.y, p += dy, last = 1;										\
	if (t == tm.z) tm.z += td.z, p += dz, last = 2;										\
	if (p & TOPMASK3) goto exitLabel;

#endif

#define GRIDSTEP(exitX)																			\
	if (!--steps) break;																		\
	if (o != 0) if (!(o & 1)) { *dist = (t + to) * 8.0f, *side = last; return o >> 1; } else	\
	{																							\
		const float4 tm_ = tm;																	\
		const uint4 p4 = convert_uint4( A + V * (t *= 8) );										\
		uint v, p = (clamp( p4.x, tp >> 17, (tp >> 17) + 7 ) << 20) +							\
			(clamp( p4.y, (tp >> 7) & 1023, ((tp >> 7) & 1023) + 7 ) << 10) +					\
			clamp( p4.z, (tp << 3) & 1023, ((tp << 3) & 1023) + 7 ), lp = ~1;					\
		tm = (convert_float4( (uint4)((p >> 20) + OFFS_X, ((p >> 10) & 1023) +					\
			OFFS_Y, (p & 1023) + OFFS_Z, 0) ) - A) * rV;										\
		p &= 7 + (7 << 10) + (7 << 20), o = --o << 8;											\
		BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX );			\
		BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX );			\
		BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX );			\
		BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX );			\
		BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX );			\
		BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX ); BRICKSTEP( exitX );			\
	exitX: tm = tm_;																			\
	}																							\
	t = min( tm.x, min( tm.y, tm.z ) ), last = 0;												\
	if (t == tm.x) tm.x += td.x, tp += dx;														\
	if (t == tm.y) tm.y += td.y, tp += dy, last = 1;											\
	if (t == tm.z) tm.z += td.z, tp += dz, last = 2;											\
	if ((tp & UBERMASK3) - tq) break;															\
	o = read_imageui( grid, (int4)(tp >> 20, tp & 127, (tp >> 10) & 127, 0) ).x;

// mighty two-level grid traversal
uint TraceRay( float4 A, const float4 B, float* dist, uint* side, __read_only image3d_t grid,
	__global const unsigned char* uberGrid,
	__global const PAYLOAD* brick0,
#if ONEBRICKBUFFER == 0
	__global const PAYLOAD* brick1,
	__global const PAYLOAD* brick2,
	__global const PAYLOAD* brick3,
#endif
	int steps
)
{
#if ONEBRICKBUFFER == 0
	__global const PAYLOAD* bricks[4] = { brick0, brick1, brick2, brick3 };
#endif
	const float4 V = FixZeroDeltas( B ), rV = (float4)(1.0 / V.x, 1.0 / V.y, 1.0 / V.z, 1);
	float to = 0; // distance to travel to get into grid
	const int bits = select( 4, 34, V.x > 0 ) + select( 3072, 10752, V.y > 0 ) + select( 1310720, 3276800, V.z > 0 ); // magic
	uint last = 0, dx = DIR_X << 20, dy = DIR_Y << 10, dz = DIR_Z;
	if (A.x < 0 || A.y < 0 || A.z < 0 || A.x > MAPWIDTH || A.y > MAPHEIGHT || A.z > MAPDEPTH)
	{
		// use slab test to clip ray origin against scene AABB
		const float tx1 = -A.x * rV.x, tx2 = (MAPWIDTH - A.x) * rV.x;
		float tmin = min( tx1, tx2 ), tmax = max( tx1, tx2 );
		const float ty1 = -A.y * rV.y, ty2 = (MAPHEIGHT - A.y) * rV.y;
		tmin = max( tmin, min( ty1, ty2 ) ), tmax = min( tmax, max( ty1, ty2 ) );
		const float tz1 = -A.z * rV.z, tz2 = (MAPDEPTH - A.z) * rV.z;
		tmin = max( tmin, min( tz1, tz2 ) ), tmax = min( tmax, max( tz1, tz2 ) );
		if (tmax < tmin || tmax <= 0) return 0; // ray misses scene 
		A += tmin * V, to = tmin; // new ray entry point
		// update 'last', for correct handling of hits on the border of the map
		if (A.y < 0.01f || A.y >( MAPHEIGHT - 1.01f )) last = 1;
		if (A.z < 0.01f || A.z >( MAPDEPTH - 1.01f )) last = 2;
	}
	uint up = (clamp( (uint)A.x >> 5, 0u, 31u ) << 20) + (clamp( (uint)A.y >> 5, 0u, 31u ) << 10) +
		clamp( (uint)A.z >> 5, 0u, 31u );
	float4 tm = ((float4)((up >> 20) + OFFS_X, ((up >> 10) & 31) + OFFS_Y, (up & 31) + OFFS_Z, 0) - A * 0.03125f) * rV;
	float t = 0;
	const float4 td = (float4)(DIR_X, DIR_Y, DIR_Z, 0) * rV;
	uint o = uberGrid[(up >> 20) + ((up & 31) << 5) + (((up >> 10) & 31) << 10)];
	while (1)
	{
		if ((steps -= 4) <= 0) break;
		if (o)
		{
			// backup ubergrid traversal state
			const float4 tm_ = tm;
			// intialize topgrid traversal
			const uint4 p4 = convert_uint4( 0.125f * A + V * (t *= 4) );
			uint tp = (clamp( p4.x, up >> 18, (up >> 18) + 3 ) << 20) +
				(clamp( p4.y, (up >> 8) & 1023, ((up >> 8) & 1023) + 3 ) << 10) +
				clamp( p4.z, (up << 2) & 1023, ((up << 2) & 1023) + 3 ), tq = tp & UBERMASK3;
			tm = (convert_float4( (uint4)((tp >> 20) + OFFS_X, ((tp >> 10) & 127) + OFFS_Y,
				(tp & 127) + OFFS_Z, 0) ) - A * 0.125f) * rV;
			o = read_imageui( grid, (int4)(tp >> 20, tp & 127, (tp >> 10) & 127, 0) ).x;
			while (1)
			{
			#if ONEBRICKBUFFER == 0
				__global const PAYLOAD* page;
			#endif
			#ifdef ISAMPERE
				GRIDSTEP(exit1); GRIDSTEP(exit2); // Ampere has massive L1I$;
				GRIDSTEP(exit3); GRIDSTEP(exit4); // unroll 4x for best performance.
			#else
				GRIDSTEP(exit1); // Turing and older have smaller L1I$, don't unroll
			#endif
			}
			// restore ubergrid traversal state
			tm = tm_;
		}
		t = min( tm.x, min( tm.y, tm.z ) ), last = 0;
		if (t == tm.x) tm.x += td.x, up += dx;
		if (t == tm.y) tm.y += td.y, up += dy, last = 1;
		if (t == tm.z) tm.z += td.z, up += dz, last = 2;
		if (up & 0xfe0f83e0) break;
		o = uberGrid[(up >> 20) + ((up & 31) << 5) + (((up >> 10) & 31) << 10)];
	}
	return 0U;
}

void TraceRayToVoid( float4 A, float4 V, float* dist, float3* N, __read_only image3d_t grid,
#if ONEBRICKBUFFER == 1
	__global const PAYLOAD* brick,
#else
	__global const PAYLOAD* brick0, __global const PAYLOAD* brick1,
	__global const PAYLOAD* brick2, __global const PAYLOAD* brick3,
#endif
	__global const unsigned char* uber
)
{
#if 0 // TODO
#if ONEBRICKBUFFER == 0
	__global const PAYLOAD* bricks[4] = { brick0, brick1, brick2, brick3 };
#endif
	V = FixZeroDeltas( V );
	const float4 rV = (float4)(1.0 / V.x, 1.0 / V.y, 1.0 / V.z, 1);
	if (A.x < 0 || A.y < 0 || A.z < 0 || A.x > MAPWIDTH || A.y > MAPHEIGHT || A.z > MAPDEPTH)
	{
		*dist = 0; // we start outside the grid, and thus in empty space: don't do that
		return;
	}
	uint tp = (clamp( (uint)A.x >> 3, 0u, 127u ) << 20) + (clamp( (uint)A.y >> 3, 0u, 127u ) << 10) +
		clamp( (uint)A.z >> 3, 0u, 127u );
	const int bits = select( 4, 34, V.x > 0 ) + select( 3072, 10752, V.y > 0 ) + select( 1310720, 3276800, V.z > 0 ); // magic
	float4 tm = ((float4)(((tp >> 20) & 127) + ((bits >> 5) & 1), ((tp >> 10) & 127) + ((bits >> 13) & 1),
		(tp & 127) + ((bits >> 21) & 1), 0) - A * 0.125f) * rV;
	float4 td = (float4)(DIR_X, DIR_Y, DIR_Z, 0) * rV;
	float t = 0;
	uint last = 0;
	do
	{
		// fetch brick from top grid
		uint o = read_imageui( grid, (int4)(tp >> 20, tp & 127, (tp >> 10) & 127, 0) ).x;
		if (o == 0) /* empty brick: done */
		{
			*dist = t * 8.0f;
			*N = -(float3)((last == 0) * DIR_X, (last == 1) * DIR_Y, (last == 2) * DIR_Z);
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
			tm = ((float4)((p >> 20) + OFFS_X, ((p >> 10) & 1023) + OFFS_Y, (p & 1023) + OFFS_Z, 0) - A) * rV;
			p &= 7 + (7 << 10) + (7 << 20), o = (o >> 1) * BRICKSIZE;
			__global const PAYLOAD* page;
			do // traverse brick
			{
				o += (p >> 20) + ((p >> 7) & (BMSK * BRICKDIM)) + (p & BMSK) * BDIM2;
			#if ONEBRICKBUFFER == 1
				if (!((__global const PAYLOAD*)brick0)[o])
				#else
				if (p != lp) page = (__global const PAYLOAD*)bricks[o / (CHUNKSIZE / PAYLOADSIZE)], lp = p;
				if (!page[o & ((CHUNKSIZE / PAYLOADSIZE) - 1)])
				#endif
				{
					*dist = t;
					*N = -(float3)((last == 0) * DIR_X, (last == 1) * DIR_Y, (last == 2) * DIR_Z);
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
#endif
}