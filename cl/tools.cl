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

__constant float halton[32] = { 
	0, 0, 0.5f, 0.333333f, 0, 0.6666666f, 0.75f, 0.111111111f, 0, 0.44444444f, 
	0.5f, 0.7777777f, 0.25f, 0.222222222f, 0.75f, 0.55555555f, 0, 0.88888888f, 
	0.5f, 0.03703703f, 0.25f, 0.37037037f, 0.75f, 0.70370370f, 0.125f, 0.148148148f,
	0.625f, 0.481481481f, 0.375f, 0.814814814f, 0.875f, 0.259259259f 
};

// produce a camera ray direction for a position in screen space
float3 GenerateCameraRay( const float2 pixelPos, __constant struct RenderParams* params )
{
#if TAA
	const uint px = (uint)pixelPos.x;
	const uint py = (uint)pixelPos.y;
	const uint h = (params->frame + (px & 3) + 4 * (py & 3)) & 15;
	const float2 uv = (float2)(
		(pixelPos.x + halton[h * 2 + 0]) * params->oneOverRes.x, 
		(pixelPos.y + halton[h * 2 + 1]) * params->oneOverRes.y
	);
#else
	const float2 uv = (float2)(
		pixelPos.x * params->oneOverRes.x, 
		pixelPos.y * params->oneOverRes.y
	);
#endif
#if PANINI
	const float3 V = PaniniProjection( (float2)(uv.x * 2 - 1, (uv.y * 2 - 1) * ((float)SCRHEIGHT / SCRWIDTH)), PI / 5, 0.15f );
	// multiply by improvised camera matrix
	return V.z * normalize( (params->p1 + params->p2) * 0.5f - params->E ) +
		V.x * normalize( params->p1 - params->p0 ) + V.y * normalize( params->p2 - params->p0 );
#else
	const float3 P = params->p0 + (params->p1 - params->p0) * uv.x + (params->p2 - params->p0) * uv.y;
	return normalize( P - params->E );
#endif
}

// sample the HDR sky dome texture (bilinear)
float3 SampleSky( const float3 T, __global float4* sky, uint w, uint h )
{
	const float u = w * SphericalPhi( T ) * INV2PI - 0.5f;
	const float v = h * SphericalTheta( T ) * INVPI - 0.5f;
	const float fu = u - floor( u ), fv = v - floor( v );
	const int iu = (int)u, iv = (int)v;
	const uint idx1 = (iu + iv * w) % (w * h);
	const uint idx2 = (iu + 1 + iv * w) % (w * h);
	const uint idx3 = (iu + (iv + 1) * w) % (w * h);
	const uint idx4 = (iu + 1 + (iv + 1) * w) % (w * h);
	const float4 s =
		sky[idx1] * (1 - fu) * (1 - fv) + sky[idx2] * fu * (1 - fv) +
		sky[idx3] * (1 - fu) * fv + sky[idx4] * fu * fv;
	return s.xyz;
}

// convert a voxel color to floating point rgb
float3 ToFloatRGB( const uint v )
{
#if PAYLOADSIZE == 1
	return (float3)((v >> 5) * (1.0f / 7.0f), ((v >> 2) & 7) * (1.0f / 7.0f), (v & 3) * (1.0f / 3.0f));
#else
	return (float3)(((v >> 8) & 15) * (1.0f / 15.0f), ((v >> 4) & 15) * (1.0f / 15.0f), (v & 15) * (1.0f / 15.0f));
#endif
}

// ACES filmic tonemapping, via https://www.shadertoy.com/view/3sfBWs
float3 ACESFilm( const float3 x )
{
	float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
	return clamp( (x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f );
}

// Reinhard2 tonemapping, via https://www.shadertoy.com/view/WdjSW3
float3 Reinhard2( const float3 x )
{
	const float3 L_white = (float3)4.0f;
	return (x * (1.0f + x / (L_white * L_white))) / (1.0f + x);
}

// https://twitter.com/jimhejl/status/633777619998130176
float3 ToneMapFilmic_Hejl2015(const float3 hdr, float whitePt) 
{
    float4 vh = (float4)(hdr, whitePt);
    float4 va = 1.425f * vh + 0.05f;
    float4 vf = (vh * va + 0.004f) / (vh * (va + 0.55f) + 0.0491f) - 0.0821f;
    return vf.xyz / vf.www;
}

// Linear to SRGB, also via https://www.shadertoy.com/view/3sfBWs
float3 LessThan( const float3 f, float value )
{
	return (float3)(
		(f.x < value) ? 1.0f : 0.0f,
		(f.y < value) ? 1.0f : 0.0f,
		(f.z < value) ? 1.0f : 0.0f);
}
float3 LinearToSRGB( const float3 rgb )
{
#if 0
	return sqrt( rgb );
#else
	const float3 _rgb = clamp( rgb, 0.0f, 1.0f );
	return mix( pow( _rgb * 1.055f, (float3)(1.f / 2.4f) ) - 0.055f,
		_rgb * 12.92f, LessThan( _rgb, 0.0031308f )
	);
#endif
}

// conversions between RGB and YCoCG for TAA
float3 RGBToYCoCg( const float3 RGB )
{
	const float3 rgb = min( (float3)( 4 ), RGB ); // clamp helps AA for strong HDR
	const float Y = dot( rgb, (float3)( 1, 2, 1 ) ) * 0.25f;
	const float Co = dot( rgb, (float3)( 2, 0, -2 ) ) * 0.25f + (0.5f * 256.0f / 255.0f);
	const float Cg = dot( rgb, (float3)( -1, 2, -1 ) ) * 0.25f + (0.5f * 256.0f / 255.0f);
	return (float3)( Y, Co, Cg );
}

float3 YCoCgToRGB( const float3 YCoCg )
{
	const float Y = YCoCg.x;
	const float Co = YCoCg.y - (0.5f * 256.0f / 255.0f);
	const float Cg = YCoCg.z - (0.5f * 256.0f / 255.0f);
	return (float3)( Y + Co - Cg, Y + Cg, Y - Co - Cg );
}

// sample a color buffer with bilinear interpolation
float3 bilerpSample( const __global float4* buffer, const float u, const float v )
{
	// do a bilerp fetch at u_prev, v_prev
	float fu = u - floor( u ), fv = v - floor( v );
	int iu = (int)u, iv = (int)v;
	if (iu == 0 || iv == 0 || iu >= SCRWIDTH - 1 || iv >= SCRHEIGHT - 1) return (float3)0;
	int iu0 = clamp( iu, 0, SCRWIDTH - 1 ), iu1 = clamp( iu + 1, 0, SCRWIDTH - 1 );
	int iv0 = clamp( iv, 0, SCRHEIGHT - 1 ), iv1 = clamp( iv + 1, 0, SCRHEIGHT - 1 );
	float4 p0 = (1 - fu) * (1 - fv) * buffer[iu0 + iv0 * SCRWIDTH];
	float4 p1 = fu * (1 - fv) * buffer[iu1 + iv0 * SCRWIDTH];
	float4 p2 = (1 - fu) * fv * buffer[iu0 + iv1 * SCRWIDTH];
	float4 p3 = fu * fv * buffer[iu1 + iv1 * SCRWIDTH];
	return (p0 + p1 + p2 + p3).xyz;
}

// inefficient morton code for bricks (3 bit per axis, x / y / z)
int Morton3Bit( const int x, const int y, const int z )
{	
	return (x & 1) + 2 * (y & 1) + 4 * (z & 1) +
		4 * (x & 2) + 8 * (y & 2) + 16 * (z & 2) +
		16 * (x & 4) + 32 * (y & 4) + 64 * (z & 4);
}
int Morton3( const int xyz )
{	
	return (xyz & 1) + ((xyz & 8) >> 2) + ((xyz & 64) >> 4) +
		((xyz & 2) << 2) + (xyz & 16) + ((xyz & 128) >> 2) + 
		((xyz & 4) << 4) + ((xyz & 32) << 2) + (xyz & 256);
}

// building a normal from an axis and a ray direction
float3 VoxelNormal( const uint side, const float3 D )
{
	if (side == 0) return (float3)(D.x > 0 ? -1 : 1, 0, 0 );
	if (side == 1) return (float3)(0, D.y > 0 ? -1 : 1, 0 );
	if (side == 2) return (float3)(0, 0, D.z > 0 ? -1 : 1 );
}