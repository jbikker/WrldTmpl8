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