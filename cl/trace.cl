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
uint TraceRay( float4 A, const float4 B, float* dist, float3* N, __read_only image3d_t grid,
	__global const unsigned char* brick0, __global const unsigned char* brick1,
	__global const unsigned char* brick2, __global const unsigned char* brick3, int steps )
{
	__global const unsigned char* bricks[4] = { brick0, brick1, brick2, brick3 };
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
	uint pos = (clamp( (uint)A.x >> 3, 0u, 127u ) << 20) + (clamp( (uint)A.y >> 3, 0u, 127u ) << 10) +
		clamp( (uint)A.z >> 3, 0u, 127u );
	const int bits = select( 4, 34, V.x > 0 ) + select( 3072, 10752, V.y > 0 ) + select( 1310720, 3276800, V.z > 0 ); // magic
	float4 tm = ((float4)( ((pos >> 20) & 127) + ((bits >> 5) & 1), ((pos >> 10) & 127) + ((bits >> 13) & 1),
		(pos & 127) + ((bits >> 21) & 1), 0 ) - A * 0.125f) * rV;
	float t = 0;
	const float4 td = (float4)( DIR_X, DIR_Y, DIR_Z, 0 ) * rV;
	uint last = 0;
	do
	{
		// check main grid
		uint o = read_imageui( grid, (int4)(pos >> 20, pos & 127, (pos >> 10) & 127, 0) ).x;
		if (o != 0) if ((o & 1) == 0) /* solid */
		{
			*dist = t * 8.0f, * N = -(float3)((last == 0) * DIR_X, (last == 1) * DIR_Y, (last == 2) * DIR_Z);
			return o >> 1;
		}
		else // brick
		{
			float t_ = t;
			t *= 8.0f;
			const float4 I = A + V * t;
			uint p = (clamp( (uint)I.x, pos >> 17, (pos >> 17) + 7 ) << 20) +
				(clamp( (uint)I.y, (pos >> 7) & 1023, ((pos >> 7) & 1023) + 7 ) << 10) +
				clamp( (uint)I.z, (pos << 3) & 1023, ((pos << 3) & 1023) + 7 ), lp = p + 1;
			const uint pn = p & TOPMASK3;
			float4 dm = ((float4)( (p >> 20) + OFFS_X, ((p >> 10) & 1023) + OFFS_Y, (p & 1023) + OFFS_Z, 0 ) - A) * rV;
			const PAYLOAD* page;
			do
			{
				const uint idx = (o >> 1) * BRICKSIZE + ((p >> 20) & BMSK) + ((p >> 10) & BMSK) * BRICKDIM + (p & BMSK) * BDIM2;
				if (p != lp) page = (PAYLOAD*)bricks[(idx / (CHUNKSIZE / PAYLOADSIZE)) & 3], lp = p;
				const unsigned int color = page[idx & ((CHUNKSIZE / PAYLOADSIZE) - 1)];
				if (color)
				{
					*dist = t, * N = -(float3)((last == 0) * DIR_X, (last == 1) * DIR_Y, (last == 2) * DIR_Z);
					return color;
				}
				t = min( dm.x, min( dm.y, dm.z ) );
				if (t == dm.x) dm.x += td.x, p += DIR_X << 20, last = 0;
				if (t == dm.y) dm.y += td.y, p += DIR_Y << 10, last = 1;
				if (t == dm.z) dm.z += td.z, p += DIR_Z, last = 2;
			} while ((p & TOPMASK3) == pn);
			t = t_;
		}
		if (!--steps) break;
		t = min( tm.x, min( tm.y, tm.z ) );
		if (t == tm.x) tm.x += td.x, pos += DIR_X << 20, last = 0;
		if (t == tm.y) tm.y += td.y, pos += DIR_Y << 10, last = 1;
		if (t == tm.z) tm.z += td.z, pos += DIR_Z, last = 2;
	} while (!(pos & 0xf80e0380));
	return 0U;
}

// mighty two-level grid traversal
uint TraceRay2( float4 A, const float4 B, float* dist, float3* N,
#if GRID_IN_3DIMAGE == 1
	__read_only image3d_t grid,
#else
	__global const unsigned int* grid,
#endif
	__global const unsigned char* brick0, __global const unsigned char* brick1,
	__global const unsigned char* brick2, __global const unsigned char* brick3, int steps )
{
	return TraceRay2( A, B, dist, N, grid, brick0, brick1, brick2, brick3, steps );

	__global const unsigned char* bricks[4] = { brick0, brick1, brick2, brick3 };
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
	uint last = 0, o = 0, oo;
	while (true)
	{
		// check main grid
	#if GRID_IN_3DIMAGE == 1
	#if CELLSKIPPING == 1
		if (o & (1 << 30)) o = oo; else 
	#endif
		o = read_imageui( grid, (int4)(pos.x / BRICKDIM, pos.z / BRICKDIM, pos.y / BRICKDIM, 0) ).x;
	#else
	#if CELLSKIPPING == 1
		if (o & (1 << 30)) o = oo; else 
	#endif
		o = grid[pos.x / BRICKDIM + ((pos.z / BRICKDIM) << 7) + ((pos.y / BRICKDIM) << 14)];
	#endif
		oo = o & 0xffffff;
		if (oo != 0) if ((oo & 1) == 0) /* solid */
		{
			*dist = t, * N = -(float3)((last == 0) * DIR_X, (last == 1) * DIR_Y, (last == 2) * DIR_Z);
			return oo >> 1;
		}
		else // brick
		{
			const float4 I = A + V * t;
			uint p = (clamp( (uint)I.x, pos.x & BPMX, (pos.x & BPMX) + BMSK ) << 20) +
				(clamp( (uint)I.y, pos.y & BPMY, (pos.y & BPMY) + BMSK ) << 10) +
				clamp( (uint)I.z, pos.z & BPMZ, (pos.z & BPMZ) + BMSK ), lp = p + 1;
			const uint pn = p & TOPMASK3;
			float dmx = (float)((p >> 20) + OFFS_X - A.x) * rV.x;
			float dmy = (float)(((p >> 10) & 1023) + OFFS_Y - A.y) * rV.y;
			float dmz = (float)((p & 1023) + OFFS_Z - A.z) * rV.z, d = t;
			const PAYLOAD* page;
			do
			{
				const uint idx = (oo >> 1) * BRICKSIZE + ((p >> 20) & BMSK) + ((p >> 10) & BMSK) * BRICKDIM + (p & BMSK) * BDIM2;
				if (p != lp) page = (PAYLOAD*)bricks[(idx / (CHUNKSIZE / PAYLOADSIZE)) & 3], lp = p;
				const unsigned int color = page[idx & ((CHUNKSIZE / PAYLOADSIZE) - 1)];
				if (color)
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

// BELOW THIS LINE:
// -------------------------------------------------------------------------------------------------------
// UNUSED REFERENCE MATERIAL

bool rayBoxIntersection( float4 O, float4 rD, float* tmin, float* tmax )
{
	// 0-tolerant version, from https://github.com/cgyurgyik/fast-voxel-traversal-algorithm
	float t0 = 0.0f, t1 = 1e34f, tymin, tymax, tzmin, tzmax;
	if (rD.x >= 0) t0 = -O.x * rD.x, t1 = (MAPWIDTH - O.x) * rD.x;
	else t1 = -O.x * rD.x, t0 = (MAPWIDTH - O.x) * rD.x;
	if (rD.y >= 0) tymin = -O.y * rD.y, tymax = (MAPHEIGHT - O.y) * rD.y;
	else tymax = -O.y * rD.y, tymin = (MAPHEIGHT - O.y) * rD.y;
	if (t0 > tymax || tymin > t1) return false;
	if (tymin > t0) t0 = tymin;
	if (tymax < t1) t1 = tymax;
	if (rD.z >= 0) tzmin = -O.z * rD.z, tzmax = (MAPDEPTH - O.z) * rD.z;
	else tzmax = -O.z * rD.z, tzmin = (MAPDEPTH - O.z) * rD.z;
	if (t0 > tzmax || tzmin > t1) return false;
	if (tzmin > t0) t0 = tzmin;
	if (tzmax < t1) t1 = tzmax;
	*tmin = max( 0.0f, t0 ), * tmax = t1;
	return (t1 > 0.0f);
}

// refactored two-level grid traversal
uint TraceRayRef( float4 A, const float4 B, float* dist, float3* N,
#if GRID_IN_3DIMAGE == 1
	__read_only image3d_t grid,
#else
	__global const unsigned int* grid,
#endif
	__global const unsigned char* brick0, __global const unsigned char* brick1,
	__global const unsigned char* brick2, __global const unsigned char* brick3, int steps )
{
	__global const unsigned char* bricks[4] = { brick0, brick1, brick2, brick3 };
	const float4 V = FixZeroDeltas( B ), rV = (float4)(1.0f / V.x, 1.0f / V.y, 1.0f / V.z, 1.0f);
	float tmin, tmax, tDeltaX, tMaxX, tDeltaY, tMaxY, tDeltaZ, tMaxZ;
	int stepX, stepY, stepZ;

	if (!rayBoxIntersection( A, rV, &tmin, &tmax )) return 0;

	float4 ray_start = A + tmin * B;

	int current_X = (int)max( 0.0f, floor( ray_start.x ) ) / BRICKDIM;
	if (B.x > 0.0f) stepX = 1, tDeltaX = BRICKDIM / B.x, tMaxX = tmin + (current_X * BRICKDIM + BRICKDIM - ray_start.x) / B.x;
	else if (B.x < 0.0) stepX = -1, tDeltaX = -BRICKDIM / B.x, tMaxX = tmin + (current_X * BRICKDIM - ray_start.x) / B.x;
	else stepX = 0, tDeltaX = tMaxX = tmax;

	int current_Y = (int)max( 0.0f, floor( ray_start.y ) ) / BRICKDIM;
	if (B.y > 0.0f) stepY = 1, tDeltaY = BRICKDIM / B.y, tMaxY = tmin + (current_Y * BRICKDIM + BRICKDIM - ray_start.y) / B.y;
	else if (B.y < 0.0) stepY = -1, tDeltaY = -BRICKDIM / B.y, tMaxY = tmin + (current_Y * BRICKDIM - ray_start.y) / B.y;
	else stepY = 0, tDeltaY = tMaxY = tmax;

	int current_Z = (int)max( 0.0f, floor( ray_start.z ) ) / BRICKDIM;
	if (B.z > 0.0f) stepZ = 1, tDeltaZ = BRICKDIM / B.z, tMaxZ = tmin + (current_Z * BRICKDIM + BRICKDIM - ray_start.z) / B.z;
	else if (B.z < 0.0) stepZ = -1, tDeltaZ = -BRICKDIM / B.z, tMaxZ = tmin + (current_Z * BRICKDIM - ray_start.z) / B.z;
	else stepZ = 0, tDeltaZ = tMaxZ = tmax;

	int last = 0;

	while (current_X >= 0 && current_Y >= 0 && current_Z >= 0 && current_X < 128 && current_Y < 128 && current_Z < 128)
	{
		// check main grid
	#if GRID_IN_3DIMAGE == 1
		const uint o = read_imageui( grid, (int4)(current_X, current_Z, current_Y, 0) ).x;
	#else
		const uint o = grid[current_X + (current_Z << 7) + (current_Y << 14)]; // verify
	#endif
		if (o != 0) /* if ((o & 1) == 0) */ /* solid */
		{
			*dist = tmin, * N = -(float3)((last == 0) * 1, (last == 1) * 1, (last == 2) * 1);
			return 255;
		}

		if (tMaxX < tMaxY && tMaxX < tMaxZ) current_X += stepX, tmin = tMaxX, tMaxX += tDeltaX, last = 0;
		else if (tMaxY < tMaxZ) current_Y += stepY, tmin = tMaxY, tMaxY += tDeltaY, last = 1;
		else current_Z += stepZ, tmin = tMaxZ, tMaxZ += tDeltaZ, last = 2;
	}

	return 0;
}